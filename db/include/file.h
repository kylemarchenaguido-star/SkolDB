#pragma once 

#include "status.h"
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <sys/types.h> 

typedef struct File {
  void *state;
  // pread wrapper
  Status (*read_at)(struct File *ctx, void *buf, size_t count, off_t offset);
  Status (*write_at)(struct File *ctx, const void *buf, size_t count, off_t offset);
  Status (*close_fd)(struct File *ctx);
} File;

