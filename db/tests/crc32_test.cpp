#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
// #include <initializer_list>

#include "crc32.h"

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

int main (){

  const char* str1 = "123456789";
  CHECK(crc32_compute(reinterpret_cast<const uint8_t*>(str1), strlen(str1)) == 0xCBF43926);

  const char* str2 = "987654321";
  CHECK(crc32_compute(reinterpret_cast<const uint8_t*>(str2), strlen(str2)) == 0x015F0201);

  const char* str3 = "Hey!";
  CHECK(crc32_compute(reinterpret_cast<const uint8_t*>(str3), strlen(str3)) == 0x836C74E4);

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
