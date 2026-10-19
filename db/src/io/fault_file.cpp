#include "file.h"
#include "status.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <new>
#include <random>
#include <vector>
#include "fault_file.h"

struct PendingOp{
  enum class type { write, truncate } type;
  off_t offset = 0; // for write 
  off_t length = 0; // for the truncate 
  std::vector<unsigned char> bytes; // write only 
};

struct FaultFile {
  File *inner;
  FaultSettings settings;
  std::vector<PendingOp> pending;
  std::mt19937_64 g_rng;
  int64_t write_count = 0;
  bool crashed = false;
};

static Status usable(File *ctx, FaultFile **out){
  assert(ctx != nullptr);
  auto *st = static_cast<FaultFile*>(ctx->state);
  if (st == nullptr){ return status_ioerror("file is closed"); }
  if (st->crashed){ return status_ioerror("simulated crash"); }
  *out = st;
  return status_ok();
}

static Status fault_read(struct File *ctx, void *buf, size_t count, off_t offset, size_t *n);
static Status fault_write(struct File *ctx, const void *buf, size_t count, off_t offset, size_t *n);
static Status fault_close(struct File *ctx);
static Status fault_fdatasync(struct File *ctx);
static Status fault_ftruncate(struct File *ctx, off_t len);
static Status fault_size(struct File *ctx, off_t *n_size);

Status open_fault_file(File *inner, const FaultSettings *settings, File *out){
  if (inner == nullptr || settings == nullptr ||out == nullptr){
    return status_invalidargument("null argument to open_fault_file");
  }
  auto *st = new (std::nothrow) FaultFile;
  if (st == nullptr){ return status_ioerror("out of memory"); }

  out->state = st;
  out->read_at = fault_read;
  out->write_at = fault_write;
  out->close_file = fault_close;
  out->fdatasync_file = fault_fdatasync;
  out->ftruncate_file = fault_ftruncate;
  out->size_file = fault_size;
  return status_ok();
}
