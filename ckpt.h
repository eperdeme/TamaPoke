#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

// CHECKPOINTED RECORDS: how a save survives losing power halfway through.
//
// NVS writes one key at a time and Preferences commits on every put(), so a
// save spread over ~50 keys is not atomic. A cut in the middle leaves the early
// fields new and the later ones old, and what loads next boot is a creature
// assembled from two different lives -- current Attack training beside older
// Defence, Speed and species. That is issue #3.
//
// The fix is to write each logical record as ONE blob, guarded by a CRC, into
// TWO keys used alternately. The newest complete blob wins; if the write that
// was in flight never landed, the previous one is still whole. The legacy
// scalar keys are NOT written any more -- see the comment above Pet::save()
// in pet.h -- but they are still READ once, as the migration path for a save
// that predates the checkpoint. A backup and a downgrade both read/write the
// checkpoint keys now; save.cpp's SAVE_FIELDS carries only those plus the
// handful of keys (settings, the bag) that never had a checkpoint of their own.
//
// Every record here shares one layout:
//
//   off 0    u32  magic        -- identifies the record AND its wire format
//   off 4    u16  version      -- informational; see the append-only rule
//   off 6    u16  size         -- the WHOLE blob, trailing crc included
//   off 8    u32  generation   -- monotonic; picks the newer of the two slots
//   off 12   ...  body
//   size-2   u16  crc          -- CRC-16/CCITT-FALSE over bytes [0, size-2)
//
// THE CRC SITS AT THE END, not in the header, so that `size` alone locates it.
// That is what lets a record be read across a layout change: a reader takes the
// body prefix it understands and leaves any field it has that the stored record
// did not at its initialiser -- the same rule loadBlob() applies to the dex
// bitmaps, for the same reason.
//
// SO THE BODY IS APPEND-ONLY. New fields go at the end, never inserted, exactly
// as with move indices and the badge arrays (CLAUDE.md § "wearing an INDEX").
// Inserting one silently reinterprets every save already on a device.
//
// If a change ever CANNOT be expressed that way, bump the MAGIC rather than the
// version. That invalidates the record cleanly and loudly instead of quietly
// misreading it, and it is one rule rather than a version check somebody has to
// remember to relax. `version` is carried for diagnostics only.
//
// Why this matters more than it looks: rejecting a checkpoint means falling
// back to the legacy keys, which is precisely the torn-write path all of this
// exists to replace. A tolerant reader is what stops the next field anybody
// adds from silently reintroducing issue #3 on every device in the field.
static constexpr size_t CKPT_HDR = 12;   // magic, version, size, generation
static constexpr size_t CKPT_CRC = 2;    // the trailing crc

// Unaligned little-endian accessors. The blob is addressed by byte offset
// rather than cast to a struct, so no field's placement depends on how the
// compiler chose to pad anything.
static inline uint16_t ckptRd16(const uint8_t *p) {
  uint16_t v; memcpy(&v, p, 2); return v;
}
static inline uint32_t ckptRd32(const uint8_t *p) {
  uint32_t v; memcpy(&v, p, 4); return v;
}
static inline void ckptWr16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }
static inline void ckptWr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

// CRC-16/CCITT-FALSE: init 0xFFFF, poly 0x1021, MSB first, no final xor.
// save.cpp has its own copy for the EXPORT blob; that one is a wire format
// shared with the host tools, so the duplication is deliberate.
static uint16_t ckptCrc(const uint8_t *data, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                           : (uint16_t)(crc << 1);
  }
  return crc;
}

// Fills in the header and the trailing crc over a body the caller has already
// written. Returns the total length.
static uint16_t ckptSeal(uint8_t *buf, size_t bodyEnd, uint32_t magic,
                         uint16_t version, uint32_t generation) {
  uint16_t total = (uint16_t)(bodyEnd + CKPT_CRC);
  ckptWr32(buf + 0, magic);
  ckptWr16(buf + 4, version);
  ckptWr16(buf + 6, total);
  ckptWr32(buf + 8, generation);
  ckptWr16(buf + bodyEnd, ckptCrc(buf, bodyEnd));
  return total;
}

// Reads and validates one checkpoint slot into `buf`. Returns the stored length
// on success, 0 if the key is absent, too short, not this record, inconsistent
// about its own size, or fails its CRC. `cap` must leave room for a record
// written by a LATER build than this one -- getBytes() copies nothing at all
// when the stored blob is larger than the buffer, so a tight buffer would turn
// every downgrade into a legacy-key fallback.
static uint16_t ckptRead(Preferences &prefs, const char *key, uint32_t magic,
                         uint8_t *buf, size_t cap) {
  size_t stored = prefs.getBytesLength(key);
  if (stored < CKPT_HDR + CKPT_CRC) return 0;   // absent, or too short to be one
  if (stored > cap) {
    Serial.printf("save: %s is %u bytes, this build reads at most %u\n", key,
                  (unsigned)stored, (unsigned)cap);
    return 0;
  }
  if (prefs.getBytes(key, buf, cap) != stored) return 0;
  if (ckptRd32(buf + 0) != magic) return 0;
  if (ckptRd16(buf + 6) != stored) return 0;    // self-describing length must agree
  if (ckptRd16(buf + stored - CKPT_CRC) != ckptCrc(buf, stored - CKPT_CRC)) return 0;
  return (uint16_t)stored;
}

// Wrap-safe "is lhs newer than rhs": the counter is uint32_t and comparing it
// directly would invert after 4 billion saves.
static bool generationAfter(uint32_t lhs, uint32_t rhs) {
  return (int32_t)(lhs - rhs) > 0;
}

// Reads whichever of the two slots is newest AND complete. Returns its length,
// or 0 if neither validates. The winner is re-read rather than kept in a second
// buffer: these blobs are hundreds of bytes and the loop task's stack is not
// somewhere to spend that twice.
static uint16_t ckptReadNewest(Preferences &prefs, const char *keyA,
                               const char *keyB, uint32_t magic, uint8_t *buf,
                               size_t cap) {
  uint16_t nA = ckptRead(prefs, keyA, magic, buf, cap);
  uint32_t genA = nA ? ckptRd32(buf + 8) : 0;
  uint16_t nB = ckptRead(prefs, keyB, magic, buf, cap);
  uint32_t genB = nB ? ckptRd32(buf + 8) : 0;
  if (nB && (!nA || !generationAfter(genA, genB))) return nB;   // buf already holds B
  if (!nA) return 0;
  return ckptRead(prefs, keyA, magic, buf, cap);
}

// Which of the two slots a given generation belongs in. Odd -> A, even -> B, so
// consecutive saves alternate and each one overwrites the OLDER copy. Since the
// next generation is always (loaded + 1), its parity is always the opposite of
// the slot it was loaded from: the fallback copy can never be the one being
// overwritten. That invariant is the whole design, and powerloss_test pins it.
static const char *ckptSlot(uint32_t generation, const char *a, const char *b) {
  return (generation & 1) ? a : b;
}


struct CkptCursor {
  uint8_t *buf;
  size_t at;
  size_t end;
  bool bad = false;
  void put(const void *src, size_t n) {
    if (at + n > end) { bad = true; return; }
    memcpy(buf + at, src, n);
    at += n;
  }
  // Copies `stored` bytes out of the blob into a destination of `n`, keeping the
  // PREFIX they share and stepping over the whole stored length either way.
  void take(void *dst, size_t n, size_t stored) {
    if (at + stored > end) { bad = true; return; }
    memcpy(dst, buf + at, stored < n ? stored : n);
    at += stored;
  }
};
