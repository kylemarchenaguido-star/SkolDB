#include "file.h"
#include "status.h"
#include <cstddef>
#include <cstdint>
typedef struct settings {
  int64_t fail_after_writes;
  int64_t crash_at_write;
  uint64_t seed;
  size_t max_bytes_per_write;
} settings;

Status open_fault_file(File *set, File *out);
Status fault_power_loss(File *f);
