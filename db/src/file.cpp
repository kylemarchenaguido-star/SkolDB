#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <system_error>
#include <unistd.h>
#include "file.h"
#include "status.h"


static Status read_at_partial(File *ctx, void *buf, size_t count, off_t offset, size_t *n_read){
  ssize_t bytes_read;
  do {
    bytes_read = pread(ctx->fd, buf, count, offset);
  } while (bytes_read < 0 && errno == EINTR);

  int err = errno;
  if (bytes_read < 0){
    *n_read = 0;
    return status_ioerror("pread failed at offset: " + std::to_string(offset) + ": " + std::generic_category().message(err));
  }

  *n_read = static_cast<size_t>(bytes_read);
  return status_ok();
}


Status read_at(File *ctx, void *buf, size_t count, off_t offset, size_t *n_read){
  assert(ctx != nullptr);
  if (n_read == nullptr){
    return status_invalidargument("n_read is null");
  }

  size_t total_read = 0;
  unsigned char *ptr = static_cast<unsigned char *>(buf);
  
  while (total_read < count) {
    size_t bytes_read = 0;

    Status ok_read = read_at_partial(ctx,
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

static Status write_at_partial(File *ctx, const void *buf, size_t count, off_t offset, size_t *n_write){
  ssize_t bytes_written;
  do {
    bytes_written = pwrite(ctx->fd, buf, count, offset);
  } while (bytes_written < 0 && errno == EINTR);

  int err = errno;
  if (bytes_written < 0){
    *n_write = 0;
    return status_ioerror("pwrite failed at offset: " + std::to_string(offset) + ": " + std::generic_category().message(err));
  }

  *n_write = static_cast<size_t>(bytes_written);
  return status_ok();
}

Status write_at(File *ctx, const void *buf, size_t count, off_t offset, size_t *n_write){
  assert(ctx != nullptr);
  if (n_write == nullptr){
    return status_invalidargument("n_write is null");
  }

  size_t total_written = 0;
  const unsigned char *ptr = static_cast<const unsigned char *>(buf);

  while (total_written < count){
    size_t bytes_wrote = 0;

    Status ok_write = write_at_partial(ctx,
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
      return status_ioerror("EOF error, expected write count: " + std::to_string(count) + " , count wrote: " + std::to_string(total_written));
    }

    total_written += bytes_wrote;
  }

  *n_write = total_written;
  return status_ok();
}

Status close_fd(File *ctx){
  assert(ctx != nullptr);
  if (close(ctx->fd) == 0){
    ctx->fd = -1;
    return status_ok();
  }

  int curr_errno = errno;
  
  if (curr_errno == EINTR){
    return status_ok();
  }

  return status_ioerror("Critical Failure at fd: " + std::to_string(ctx->fd) + " , reason: " + std::generic_category().message(curr_errno));
}
