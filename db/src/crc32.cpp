#include <cstdint>
#include <cstddef>

#include "crc32.h"

// CRC32 
static uint32_t g_crc32_table[256];
static bool g_crc32_ready = false;

static void crc32_init(){
  for (uint32_t i = 0; i < 256; ++i){
    uint32_t crc = i;
    for (int j = 0; j < 8; ++j){
      crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : (crc >> 1);
    }
    g_crc32_table[i] = crc;
  }
  g_crc32_ready = true;
}

uint32_t crc32_compute(const uint8_t *data, size_t len){
  if (!g_crc32_ready){ crc32_init(); }
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; ++i){
    crc = (crc >> 8) ^ g_crc32_table[(crc ^ data[i]) & 0xFF];
  }
  return crc ^ 0xFFFFFFFF;
}
