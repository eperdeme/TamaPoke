#pragma once
#include <stddef.h>
#include <stdint.h>

// CRC-32/ISO-HDLC, the zlib one: what paks.json and the web installer compute, so
// SUM can be compared with them directly. Chain by passing the previous result.
inline uint32_t crc32Update(uint32_t crc, const uint8_t *p, size_t n) {
  static uint32_t table[256];
  if (!table[1]) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
      table[i] = c;
    }
  }
  crc = ~crc;
  while (n--) crc = table[(crc ^ *p++) & 0xFFu] ^ (crc >> 8);
  return ~crc;
}
