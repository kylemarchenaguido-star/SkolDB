#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

#include "coding.h"

static int failures = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

int main (){
  char buf[8];

  // 32-bit round-trip
  for (uint32_t v : {0u, 1u, 0x12345678u, ~0u}){
    encode_u32(buf, v);
    CHECK(decode_u32(buf) == v);
  }

  // 64-bit round-trip
  for (uint64_t v : {0ull, 1ull, 0x12345678ABCDEFull, ~0ull}){
    encode_u64(buf, v); 
    CHECK(decode_u64(buf) == v);
  }

  encode_u32(buf, 0x04030201u);
  CHECK(static_cast<uint8_t>(buf[0]) == 0x01);
  CHECK(static_cast<uint8_t>(buf[3]) == 0x04);

  encode_u64(buf, 0x0807060504030201ull);
  CHECK(static_cast<uint8_t>(buf[0]) == 0x01);
  CHECK(static_cast<uint8_t>(buf[7]) == 0x08);

  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
