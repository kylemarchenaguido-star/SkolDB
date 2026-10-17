#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <system_error>
#include <unistd.h>
#include <sys/stat.h>
#include "file.h"
#include "status.h"

struct PosixFile {
  int fd;
};

static Status posix_read(File *ctx, void *buf, size_t count, off_t offset, size_t *n_read){
  ssize_t bytes_read;
  
  auto *state = static_cast<struct PosixFile*>(ctx->state);
  int raw_fd = state->fd;
  do {
    bytes_read = pread(raw_fd, buf, count, offset);
  } while (bytes_read < 0 && errno == EINTR);

  int err = errno;
  if (bytes_read < 0){
    *n_read = 0;
    return status_ioerror("pread failed at offset: " + std::to_string(offset) + ": " + std::generic_category().message(err));
  }

  *n_read = static_cast<size_t>(bytes_read);
  return status_ok();
}


Status read_fully(File *ctx, void *buf, size_t count, off_t offset, size_t *n_read){
  assert(ctx != nullptr);
  if (n_read == nullptr){
    return status_invalidargument("n_read is null");
  }

  size_t total_read = 0;
  unsigned char *ptr = static_cast<unsigned char *>(buf);
  
  while (total_read < count) {
    size_t bytes_read = 0;

    Status ok_read = ctx->read_at(ctx,
                                     ptr + total_read,
                                     count - total_read,
                                     offset + static_cast<off_t>(total_read),
                                     &bytes_read);
    if (!is_ok(ok_read)){
      *n_read = total_read;
      return ok_read;
    }

    if (bytes_read == 0){
      *n_read = total_read;
      return status_eof("EOF error, expected read count: " + std::to_string(count) + " , count read: " + std::to_string(total_read));
    }

    total_read += bytes_read;
  }

  *n_read = total_read;
  return status_ok();
}

static Status posix_write(File *ctx, const void *buf, size_t count, off_t offset, size_t *n_write){
  ssize_t bytes_written;

  auto *state = static_cast<struct PosixFile*>(ctx->state);
  int raw_fd = state->fd;

  do {
    bytes_written = pwrite(raw_fd, buf, count, offset);
  } while (bytes_written < 0 && errno == EINTR);

  int err = errno;
  if (bytes_written < 0){
    *n_write = 0;
    return status_ioerror("pwrite failed at offset: " + std::to_string(offset) + ": " + std::generic_category().message(err));
  }

  *n_write = static_cast<size_t>(bytes_written);
  return status_ok();
}

Status write_fully(File *ctx, const void*buf, size_t count, off_t offset, size_t *n_write){
  assert(ctx != nullptr);
  if (n_write == nullptr){
    return status_invalidargument("n_write is null");
  }

  size_t total_written = 0;
  const unsigned char *ptr = static_cast<const unsigned char *>(buf);

  while (total_written < count){
    size_t bytes_wrote = 0;

    Status ok_write = ctx->write_at(ctx,
                                       ptr + total_written,
                                       count - total_written,
                                       offset + static_cast<off_t>(total_written),
                                       &bytes_wrote);

  if (!is_ok(ok_write)){
      *n_write = total_written;
      return ok_write;
    }

    if (bytes_wrote == 0){
      *n_write = total_written;
      return status_ioerror("Write made not progress, expected write count: " + std::to_string(count) + " , count wrote: " + std::to_string(total_written));
    }

    total_written += bytes_wrote;
  }

  *n_write = total_written;
  return status_ok();
}

static Status posix_close(File *ctx){
  assert(ctx != nullptr);
  auto *state = static_cast<struct PosixFile*>(ctx->state);

  if (state == nullptr){
    return status_ok();
  }

  int raw_fd = state->fd;

  if (close(raw_fd) == 0){
    free(ctx->state);
    ctx->state = nullptr;
    return status_ok();
  }

  int curr_errno = errno;
  free(ctx->state);
  ctx->state = nullptr;

  if (curr_errno == EINTR){
    return status_ok();
  }

  return status_ioerror("Critical Failure at fd: " + std::to_string(raw_fd) + " , reason: " + std::generic_category().message(curr_errno));
}

static Status posix_fdatasync(File *ctx){
  assert(ctx != nullptr);
  auto *state = static_cast<struct PosixFile*>(ctx->state);
  if (state == nullptr){
    return status_ioerror("file is closed");
  }

  int result;
  do {
    result = fdatasync(state->fd);
  } while (result < 0 && errno == EINTR);

  if (result < 0){
    int err = errno;
    return status_ioerror("fdatasync failed: " + std::generic_category().message(err));
  }

  return status_ok();
}

static Status posix_ftruncate(File *ctx, off_t len){
  assert(ctx != nullptr);
  auto* state = static_cast<PosixFile*>(ctx->state);

  int result;
  do {
    result = ftruncate(state->fd, len);
  } while (result < 0 && errno == EINTR);

  if (result < 0){
    int err = errno;
    return status_ioerror("ftruncate failed: " + std::generic_category().message(err));
  }

  return status_ok();
}

static Status posix_size_fstat(File *ctx, size_t *n_size){
  assert(ctx != nullptr);
  auto *state = static_cast<struct PosixFile*>(ctx->state);
  if (state == nullptr){
    return status_ioerror("file is closed");
  }

  struct stat statbuf;
  int result;

  do {
    result = fstat(state->fd, &statbuf);
  } while (result < 0 && errno == EINTR);

  if (result < 0) {
    int err = errno;
    return status_ioerror("fstat failed: " + std::generic_category().message(err));
  }
  statbuf.st_size = *n_size;
  return status_ok();
}

// Not file after an error, the pointers to function aren't set 
Status open_posix_file(const char *path, int flags, mode_t mode, File *out){
  if (out == nullptr || path == nullptr){
    return status_invalidargument("NULL arguments passed to posix_open");
  }

  int file_fd = open(path, flags | O_CLOEXEC, mode); 

  if (file_fd < 0){
    int err = errno;
    out->state = nullptr;
    return status_ioerror("Open failed at path: " + std::string(path) + ": " + std::generic_category().message(err));
  }

  auto* state = static_cast<struct PosixFile*>(malloc(sizeof(struct PosixFile)));

  if (state == nullptr){
    int err = errno;
    close(file_fd);
    out->state = nullptr;
  
    return status_ioerror("Failed to grab the File descriptor: " + std::generic_category().message(err));
  }

  state->fd = file_fd;
  out->state = state;
  
  out->read_at = posix_read;
  out->write_at = posix_write;
  out->close_file = posix_close;

  return status_ok();
}

// TO DO functions, fsync, ftruncate, size, sync_dir

