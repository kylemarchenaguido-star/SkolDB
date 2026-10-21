#include "file.h"
#include "status.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <new>
#include <random>
#include <utility>
#include <vector>
#include "fault_file.h"

struct PendingOp{
  enum class Type { Write, Truncate } type;
  off_t offset = 0; // for write 
  off_t length = 0; // for the truncate 
  std::vector<unsigned char> bytes; // write only 
};

struct FaultFile {
  File *inner = nullptr;
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

static Status fault_read(struct File *ctx, void *buf, size_t count, off_t offset, size_t *n){
  FaultFile *st;
  Status s = usable(ctx, &st);
  if (!is_ok(s)){ return s; }
  return st->inner->read_at(st->inner, buf, count, offset, n);
}

static Status fault_write(struct File *ctx, const void *buf, size_t count, off_t offset, size_t *n){
  FaultFile *st;
  Status s = usable(ctx, &st);
  if (!is_ok(s)){ return s; }

  PendingOp op;
  op.type = PendingOp::Type::Write;
  op.offset = offset;
  const auto *p = static_cast<const unsigned char*>(buf);
  op.bytes.assign(p, p + count);
  st->pending.push_back(std::move(op));

  *n = count;
  return status_ok();
}

static Status apply_pending(File *ctx){
  FaultFile *st;
  Status s = usable(ctx, &st);

}

static Status fault_fdatasync(struct File *ctx){
  FaultFile *st;
  Status s = usable(ctx, &st);
  if (!is_ok(s)){ return s; }
  return st->inner->fdatasync_file(st->inner);
}

static Status fault_ftruncate(struct File *ctx, off_t len){
  FaultFile *st;
  Status s = usable(ctx, &st);
  if (!is_ok(s)){ return s; }
  return st->inner->ftruncate_file(st->inner, len);
}

static Status fault_size(struct File *ctx, off_t *n_size){
  FaultFile *st;
  Status s = usable(ctx, &st);
  if (!is_ok(s)){ return s; }
  return st->inner->size_file(st->inner, n_size);
}

static Status fault_close(struct File *ctx){
  assert(ctx != nullptr);
  auto *st = static_cast<FaultFile*>(ctx->state);
  if (st == nullptr){ return status_ok(); }

  Status s = st->inner->close_file(st->inner);
  delete st;
  ctx->state = nullptr;
  return s;
}

Status open_fault_file(File *inner, const FaultSettings *settings, File *out){
  if (inner == nullptr || settings == nullptr ||out == nullptr){
    return status_invalidargument("null argument to open_fault_file");
  }
  auto *st = new (std::nothrow) FaultFile;
  if (st == nullptr){ return status_ioerror("out of memory"); }

  out->state = st;
  st->inner = inner;
  st->settings = *settings;
  st->g_rng.seed(settings->seed);
  out->read_at = fault_read;
  out->write_at = fault_write;
  out->close_file = fault_close;
  out->fdatasync_file = fault_fdatasync;
  out->ftruncate_file = fault_ftruncate;
  out->size_file = fault_size;
  return status_ok();
}
