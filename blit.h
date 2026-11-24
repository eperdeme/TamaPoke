#pragma once
#include <stdint.h>

// Merge contiguous same-colour pixels in a sprite row into one fillRect per run
// instead of one per pixel (identical pixels on screen, far fewer draw calls).
// Ported from ViezeVingertjes (socquique/TamaPoke PR #25).
// key(c) returns the colour key for pixel c, or -1 if transparent;
// emit(c0, len, key) receives each opaque run.
template <class Key, class Emit>
inline void forRuns(int w, Key key, Emit emit) {
  int c = 0, k = w > 0 ? key(0) : -1;
  while (c < w) {
    int c0 = c, kn = k;
    while (++c < w && (kn = key(c)) == k) {}
    if (k >= 0) emit(c0, c - c0, k);
    k = kn;  // the key that ended the run opens the next: no duplicate read
  }
}

// Key for an 8-bit indexed pixel: -1 = transparent (0xFF); in silhouette mode
// all opaque pixels share key 0 so they merge across palette indices.
inline int indexedKey(uint8_t idx, bool sil) { return idx == 0xFF ? -1 : (sil ? 0 : idx); }

// Same for ASCII character maps: '.' is transparent, everything else passes
// through unsigned so spriteColor((char)k) gets the exact char back.
inline int charKey(char ch, bool sil) { return ch == '.' ? -1 : (sil ? 0 : (unsigned char)ch); }
