
#include "fault_file.h"
#include "file.h"
#include "status.h"
#include <cstring>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cassert>
#include <cstring>


static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

int main () {

  char path[] = "/tmp/tempfile_XXXXXX";

  int fd = mkstemp(path);
  CHECK(fd != -1);
  
  File posix, fault;
  Status op = open_posix_file(path, O_RDWR | O_CREAT | O_TRUNC, 0644, &posix);
  CHECK(is_ok(op));

  FaultSettings s;
  op = open_fault_file(&posix, &s, &fault);
  CHECK(is_ok(op));

  size_t n;
  op = write_fully(&fault, "Hello", 5, 0, &n);
  CHECK(is_ok(op) && n == 5);

  char out[5];
  op = read_fully(&fault, out, 5, 0, &n);
  CHECK(memcmp(out, "Hello", 5) == 0);
  CHECK(is_ok(op) && n == 5);

  op = fault.close_file(&fault);
  CHECK(is_ok(op));

  close(fd);
  unlink(path);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
