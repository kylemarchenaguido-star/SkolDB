#pragma once 

#include "status.h"
#include <cstddef>
#include <sys/types.h> 


typedef struct File {
  void *state;
  Status (*read_at)(struct File *ctx, void *buf, size_t count, off_t offset, size_t *n);
  Status (*write_at)(struct File *ctx, const void *buf, size_t count, off_t offset, size_t *n);
  Status (*close_file)(struct File *ctx);
  Status (*fdatasync_file)(struct File *ctx);
  Status (*ftruncate_file)(struct File *ctx, off_t len);
  Status (*size_file)(struct File *ctx, off_t *n_size);
} File;

Status read_fully(File *ctx, void *buf, size_t count, off_t offset, size_t *n);
Status write_fully(File *ctx, const void *buf, size_t count, off_t offset, size_t *n);
Status open_posix_file(const char *path, int flags, mode_t mode, File *out);
Status sync_dir(const char *path);

