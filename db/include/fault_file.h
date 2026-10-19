#pragma once 

#include "file.h"
#include "status.h"
#include <cstddef>
#include <cstdint>

typedef struct FaultSettings {
  int64_t fail_after_writes = -1;
  int64_t crash_at_write = -1;
  uint64_t seed = 0;
  size_t max_bytes_per_write = 0;
} FaultSettings;

Status open_fault_file(File *inner, const FaultSettings *settings, File *out);
Status fault_power_loss(File *f);
