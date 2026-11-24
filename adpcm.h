#pragma once
#include <stdint.h>
#include <stddef.h>

// 16 kHz 4-bit IMA-ADPCM sampled cry container ("TCRY").
// Header (12 bytes little-endian):
//   0..3  "TCRY"
//   4..5  sampleRate (16000)
//   6..7  initial predictor (int16)
//   8     initial IMA step index (0..88)
//   9     flags (0)
//   10..11 reserved (0)
//   12..  packed 4-bit IMA nibbles, low nibble first per byte
#define CRY_ADPCM_HDR 12u
#define CRY_ADPCM_MAX 65536u
#define CRY_SAMPLE_RATE 16000u

static const int8_t IMA_INDEX_TBL[16] = {
  -1, -1, -1, -1, 2, 4, 6, 8,
  -1, -1, -1, -1, 2, 4, 6, 8,
};

static const int16_t IMA_STEP_TBL[89] = {
  7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
  34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
  157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
  724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
  2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630,
  9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
  27086, 29794, 32767
};

static inline bool adpcmValidHeader(const uint8_t *data, uint32_t len) {
  if (!data || len < CRY_ADPCM_HDR || len > CRY_ADPCM_MAX) return false;
  if (data[0] != 'T' || data[1] != 'C' || data[2] != 'R' || data[3] != 'Y') return false;
  uint16_t rate = (uint16_t)(data[4] | ((uint16_t)data[5] << 8));
  if (rate != CRY_SAMPLE_RATE) return false;
  if (data[8] > 88) return false;
  return true;
}

struct AdpcmDec {
  const uint8_t *p = nullptr;
  const uint8_t *end = nullptr;
  int32_t pred = 0;
  int8_t idx = 0;
  bool high = false;
  bool active = false;

  bool init(const uint8_t *data, uint32_t len) {
    active = false;
    if (!adpcmValidHeader(data, len)) return false;
    pred = (int16_t)((uint16_t)data[6] | ((uint16_t)data[7] << 8));
    idx = (int8_t)data[8];
    p = data + CRY_ADPCM_HDR;
    end = data + len;
    high = false;
    active = (p < end);
    return active;
  }

  bool done() const { return !active; }

  // Logarithmic scale matched to GbSynth::MASTER_SCALE (0 = silent, 10 = full).
  size_t decode(int16_t *out, size_t maxSamples, uint8_t master) {
    static const int32_t SCALE[11] = {
      0, 16, 32, 57, 97, 157, 247, 373, 547, 760, 1000
    };
    if (!active || !out || !maxSamples) return 0;
    if (master > 10) master = 10;
    const int32_t scale = SCALE[master];
    size_t n = 0;
    while (n < maxSamples && p < end) {
      uint8_t nib = high ? (uint8_t)((*p++ >> 4) & 0x0F) : (uint8_t)(*p & 0x0F);
      high = !high;
      int32_t step = IMA_STEP_TBL[idx];
      int32_t diff = step >> 3;
      if (nib & 1) diff += step >> 2;
      if (nib & 2) diff += step >> 1;
      if (nib & 4) diff += step;
      if (nib & 8) pred -= diff;
      else pred += diff;
      if (pred > 32767) pred = 32767;
      else if (pred < -32768) pred = -32768;
      int ni = idx + IMA_INDEX_TBL[nib];
      if (ni < 0) ni = 0;
      else if (ni > 88) ni = 88;
      idx = (int8_t)ni;
      out[n++] = (int16_t)((pred * scale) / 1000);
    }
    if (p >= end) active = false;
    return n;
  }
};

static inline size_t adpcmEncode(const int16_t *pcm, size_t count,
                                 uint8_t *out, size_t maxOut,
                                 uint16_t rate = CRY_SAMPLE_RATE) {
  if (!pcm || !count || !out) return 0;
  size_t bodyBytes = (count + 1) / 2;
  if (CRY_ADPCM_HDR + bodyBytes > maxOut) return 0;
  int32_t pred = pcm[0];
  int8_t idx = 0;
  out[0] = 'T'; out[1] = 'C'; out[2] = 'R'; out[3] = 'Y';
  out[4] = (uint8_t)(rate & 0xFF); out[5] = (uint8_t)(rate >> 8);
  uint16_t up = (uint16_t)(int16_t)pred;
  out[6] = (uint8_t)(up & 0xFF); out[7] = (uint8_t)(up >> 8);
  out[8] = (uint8_t)idx; out[9] = 0; out[10] = 0; out[11] = 0;
  size_t at = CRY_ADPCM_HDR;
  uint8_t curByte = 0;
  bool hi = false;
  for (size_t i = 0; i < count; i++) {
    int32_t diff = pcm[i] - pred;
    uint8_t nib = 0;
    if (diff < 0) { nib = 8; diff = -diff; }
    int32_t step = IMA_STEP_TBL[idx];
    int32_t vpdiff = step >> 3;
    if (diff >= step) { nib |= 4; diff -= step; vpdiff += step; }
    step >>= 1;
    if (diff >= step) { nib |= 2; diff -= step; vpdiff += step; }
    step >>= 1;
    if (diff >= step) { nib |= 1; vpdiff += step; }
    if (nib & 8) pred -= vpdiff;
    else pred += vpdiff;
    if (pred > 32767) pred = 32767;
    else if (pred < -32768) pred = -32768;
    int ni = idx + IMA_INDEX_TBL[nib];
    if (ni < 0) ni = 0;
    else if (ni > 88) ni = 88;
    idx = (int8_t)ni;
    if (!hi) {
      curByte = (uint8_t)(nib & 0x0F);
      hi = true;
    } else {
      out[at++] = (uint8_t)(curByte | ((nib & 0x0F) << 4));
      hi = false;
    }
  }
  if (hi) out[at++] = curByte;
  return at;
}
