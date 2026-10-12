#include <cstdio>
#include <cstring>

typedef struct {
  const char *ptr; // points at the beginning of the slice
  size_t len;
} string_slice;
