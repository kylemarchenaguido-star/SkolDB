#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <sys/types.h>

// Versions of put that write into a buffer directly
inline void encode_u32(char *dst, uint32_t value){
  uint8_t* const buffer = reinterpret_cast<uint8_t*>(dst); 

  buffer[0] = static_cast<uint8_t>(value);
  buffer[1] = static_cast<uint8_t>(value >> 8);
  buffer[2] = static_cast<uint8_t>(value >> 16);
  buffer[3] = static_cast<uint8_t>(value >> 24);
}

inline void encode_u64(char *dst, uint64_t value){
  uint8_t* const buffer = reinterpret_cast<uint8_t*>(dst); 

  buffer[0] = static_cast<uint8_t>(value);
  buffer[1] = static_cast<uint8_t>(value >> 8);
  buffer[2] = static_cast<uint8_t>(value >> 16);
  buffer[3] = static_cast<uint8_t>(value >> 24);
  buffer[4] = static_cast<uint8_t>(value >> 32);
  buffer[5] = static_cast<uint8_t>(value >> 40);
  buffer[6] = static_cast<uint8_t>(value >> 48);
  buffer[7] = static_cast<uint8_t>(value >> 56);
}

// Versions of get that read directly from a character buffer
inline uint32_t decode_u32(const char *ptr){
  const uint8_t* const buffer = reinterpret_cast<const uint8_t*>(ptr);
  
  return static_cast<uint32_t>(buffer[0]) | (static_cast<uint32_t>(buffer[1]) << 8) |
         (static_cast<uint32_t>(buffer[2]) << 16) | (static_cast<uint32_t>(buffer[2]) << 24);
}

inline uint64_t decode_u64(const char *ptr){
  const uint8_t* const buffer = reinterpret_cast<const uint8_t*>(ptr);

  return static_cast<uint64_t>(buffer[0]) | (static_cast<uint64_t>(buffer[1]) << 8) |
         (static_cast<uint64_t>(buffer[1]) << 16) | (static_cast<uint64_t>(buffer[2]) << 24) |
         (static_cast<uint64_t>(buffer[3]) << 32) | (static_cast<uint64_t>(buffer[4]) << 40) |
         (static_cast<uint64_t>(buffer[3]) << 48) | (static_cast<uint64_t>(buffer[4]) << 56);
}

