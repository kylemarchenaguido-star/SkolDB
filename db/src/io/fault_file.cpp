#include "file.h"
#include "status.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
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

static Status logical_size(FaultFile *st, off_t *out){
  off_t size = 0;
  Status s = st->inner->size_file(st->inner, &size);
  if (!is_ok(s)) return s;
  for (const PendingOp &op : st->pending){
    if (op.type == PendingOp::Type::Write){
      size = std::max(size, op.offset +  static_cast<off_t>(op.bytes.size()));
    } else {
      size = op.length;
    }
  }
  *out = size;
  return status_ok();
}

Status fault_power_loss(File *f){
  assert(f != nullptr);
  auto *st = static_cast<FaultFile *>(f->state);
  if (st ==  nullptr) return status_ioerror("file is closed");
  st->pending.clear();
  st->crashed = true;
  return status_ok();
}

static Status fault_read(struct File *ctx, void *buf, size_t count, off_t offset, size_t *n){
  FaultFile *st;
  Status s = usable(ctx, &st);
  if (!is_ok(s)){ return s; }
  if (n == nullptr) return status_invalidargument("n is null");

  off_t size = 0;
  s = logical_size(st, &size);
  if (!is_ok(s)) return s;
  if (offset >= size){ *n = 0; return status_ok(); }
  size_t want = std::min(count, static_cast<size_t>(size - offset));

  memset(buf, 0, want);

  size_t got = 0;
  s = read_fully(st->inner, buf, want, offset, &got);
  if (!is_ok(s) && s.code != Code::EndOfFile) return s;

  off_t end = offset + static_cast<off_t>(want);
  auto *out = static_cast<unsigned char *>(buf);

  for (const PendingOp &op : st->pending){
    if (op.type == PendingOp::Type::Truncate){
      off_t from = std::max(op.length, offset);
      if (from < end) memset(out + (from - offset), 0, static_cast<size_t>(end - from));
    } else {
      off_t op_end = op.offset + static_cast<off_t>(op.bytes.size());
      off_t lo = std::max(op.offset, offset);
      off_t hi = std::min(op_end, end);
      if (lo < hi){
        memcpy(out + (lo - offset), op.bytes.data() + (lo - op.offset), static_cast<size_t>(hi - lo));
      }
    }
  }
  *n = want;
  return status_ok();
}

static Status fault_write(struct File *ctx, const void *buf, size_t count, off_t offset, size_t *n){
  FaultFile *st;
  Status s = usable(ctx, &st);
  if (!is_ok(s)){ return s; }
  if (n == nullptr) return status_invalidargument("n is null");
  *n = 0;

  st->write_count++;

  if (st->settings.fail_after_writes >= 0 && st->write_count > st->settings.fail_after_writes){
    return status_ioerror("simulated write failure");
  }

  size_t max = st->settings.max_bytes_per_write;
  if (max > 0 && count > max) count = max;

  if (st->settings.crash_at_write >= 0 && st->write_count == st->settings.crash_at_write){
    size_t half = count / 2;
    if (half > 0){
      size_t done = 0;
      Status w = write_fully(st->inner, buf, half, offset, &done);
      if (!is_ok(w)) return w;
    }
    st->pending.clear();
    st->crashed = true;
    return status_ioerror("simulated crash during write");
  }
  
  PendingOp op;
  op.type = PendingOp::Type::Write;
  op.offset = offset;
  const auto *p = static_cast<const unsigned char*>(buf);
  op.bytes.assign(p, p + count);
  st->pending.push_back(std::move(op));

  *n = count;
  return status_ok();
}

static Status apply_pending(FaultFile *st){
  for (const PendingOp &op : st->pending){
    if (op.type == PendingOp::Type::Write){
      size_t done = 0;
      Status s = write_fully(st->inner, op.bytes.data(), op.bytes.size(), op.offset, &done);
      if (!is_ok(s)) return s;
    } else {
      Status s = st->inner->ftruncate_file(st->inner, op.length);
      if (!is_ok(s)) return s;
    }
  }
  return status_ok();
}

static Status fault_fdatasync(File *ctx){
  FaultFile *st;
  Status s = usable(ctx, &st);
  if (!is_ok(s)){ return s; }

  s = apply_pending(st);
  if (!is_ok(s)) return s;

  s = st->inner->fdatasync_file(st->inner);
  if (!is_ok(s)) return s;

  st->pending.clear();
  return status_ok();
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
