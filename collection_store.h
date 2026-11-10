#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include "ckpt.h"

#define COLLECTION_PARTITION "tpcollect"
#define COLLECTION_NAMESPACE "collection"

static constexpr size_t COLLECTION_PARTITION_BYTES = 0x80000;
static constexpr size_t COLLECTION_MAX_BYTES = 96 * 1024;
static constexpr uint32_t COLLECTION_MAGIC = 0x3343424BUL; // "KBC3"
static constexpr uint32_t COLLECTION_LEGACY_MAGIC = 0x3243424BUL; // "KBC2"
static constexpr size_t COLLECTION_MIN_HEADER = 16;
static constexpr size_t COLLECTION_HEADER = COLLECTION_MIN_HEADER;
static constexpr size_t COLLECTION_FIXED = COLLECTION_HEADER + 6;

constexpr size_t collectionSize(size_t stride, size_t partyCount, size_t boxCount) {
  return COLLECTION_FIXED + stride * (partyCount + boxCount) + CKPT_CRC;
}

struct CollectionLayout {
  size_t offset = 0, stride = 0, partyCount = 0, boxCount = 0;
  uint32_t generation = 0;
  bool wide = false;
};

inline bool collectionInspect(const uint8_t *buf, size_t n, CollectionLayout &layout,
                              uint16_t maxParty, uint16_t maxBox, int16_t maxDex) {
  if (!buf || n < 20 || n > COLLECTION_MAX_BYTES) return false;
  const uint32_t magic = ckptRd32(buf);
  size_t dims;
  if (magic == COLLECTION_MAGIC) {
    if (n < COLLECTION_MIN_HEADER + 6 + CKPT_CRC || ckptRd32(buf + 8) != n) return false;
    dims = ckptRd16(buf + 6);
    if (dims < COLLECTION_MIN_HEADER || dims > n - 6 - CKPT_CRC) return false;
    layout.generation = ckptRd32(buf + 12);
    layout.wide = true;
  } else if (magic == COLLECTION_LEGACY_MAGIC) {
    if (ckptRd16(buf + 6) != n) return false;
    dims = CKPT_HDR;
    layout.generation = ckptRd32(buf + 8);
    layout.wide = false;
  } else return false;
  if (ckptRd16(buf + n - CKPT_CRC) != ckptCrc(buf, n - CKPT_CRC)) return false;
  layout.offset = dims + 6;
  layout.stride = ckptRd16(buf + dims);
  layout.partyCount = ckptRd16(buf + dims + 2);
  layout.boxCount = ckptRd16(buf + dims + 4);
  const size_t records = layout.partyCount + layout.boxCount;
  if (layout.stride < sizeof(int16_t) || !layout.partyCount || !layout.boxCount ||
      layout.partyCount > maxParty || layout.boxCount > maxBox ||
      layout.stride > (n - layout.offset - CKPT_CRC) / records) return false;
  for (size_t i = 0; i < records; ++i) {
    int16_t dex;
    memcpy(&dex, buf + layout.offset + i * layout.stride, sizeof(dex));
    if (dex < 0 || dex > maxDex) return false;
  }
  return true;
}

inline uint32_t collectionSeal(uint8_t *buf, size_t bodyEnd, uint16_t stride,
                               uint16_t partyCount, uint16_t boxCount,
                               uint32_t generation) {
  if (!buf || bodyEnd < COLLECTION_FIXED || bodyEnd > COLLECTION_MAX_BYTES - CKPT_CRC ||
      !stride || !partyCount || !boxCount ||
      stride > (bodyEnd - COLLECTION_FIXED) / (size_t(partyCount) + boxCount)) return 0;
  const uint32_t total = uint32_t(bodyEnd + CKPT_CRC);
  ckptWr32(buf, COLLECTION_MAGIC);
  ckptWr16(buf + 4, 1);
  ckptWr16(buf + 6, COLLECTION_HEADER);
  ckptWr32(buf + 8, total);
  ckptWr32(buf + 12, generation);
  ckptWr16(buf + COLLECTION_HEADER, stride);
  ckptWr16(buf + COLLECTION_HEADER + 2, partyCount);
  ckptWr16(buf + COLLECTION_HEADER + 4, boxCount);
  ckptWr16(buf + bodyEnd, ckptCrc(buf, bodyEnd));
  return total;
}

inline uint32_t collectionRead(Preferences &prefs, const char *key, uint8_t *buf,
                               size_t cap, CollectionLayout &layout,
                               uint16_t maxParty, uint16_t maxBox, int16_t maxDex) {
  const size_t n = prefs.getBytesLength(key);
  if (!n || n > cap || n > COLLECTION_MAX_BYTES ||
      prefs.getBytes(key, buf, cap) != n ||
      !collectionInspect(buf, n, layout, maxParty, maxBox, maxDex)) return 0;
  return uint32_t(n);
}

inline uint32_t collectionReadNewest(Preferences &prefs, uint8_t *buf, size_t cap,
                                     CollectionLayout &layout, uint16_t maxParty,
                                     uint16_t maxBox, int16_t maxDex) {
  const size_t limit = cap < COLLECTION_MAX_BYTES ? cap : COLLECTION_MAX_BYTES;
  if (prefs.getBytesLength("cbA") > limit || prefs.getBytesLength("cbB") > limit) {
    Serial.println("save: collection needs a larger reader; NOT rolling back");
    return 0;
  }
  // Select the newest intact frame before applying this build's content limits.
  // Otherwise a newer firmware's data could be silently replaced by an old copy.
  CollectionLayout a, b;
  const uint32_t nA = collectionRead(prefs, "cbA", buf, cap, a, UINT16_MAX, UINT16_MAX, INT16_MAX);
  const uint32_t nB = collectionRead(prefs, "cbB", buf, cap, b, UINT16_MAX, UINT16_MAX, INT16_MAX);
  uint32_t newest = nB;
  if (nA && (!nB || generationAfter(a.generation, b.generation)))
    newest = collectionRead(prefs, "cbA", buf, cap, layout, UINT16_MAX, UINT16_MAX, INT16_MAX);
  if (newest && !collectionInspect(buf, newest, layout, maxParty, maxBox, maxDex)) {
    Serial.println("save: newest collection exceeds this build's limits; NOT rolling back");
    return 0;
  }
  return newest;
}

#ifdef ESP32
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <nvs_flash.h>
#endif

inline void *saveAlloc(size_t bytes) {
#ifdef ESP32
  return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return malloc(bytes);
#endif
}

inline bool collectionStorageReady() {
#ifdef ESP32
  const esp_partition_t *p = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS, COLLECTION_PARTITION);
  if (!p || p->address != 0xF70000 || p->size != COLLECTION_PARTITION_BYTES) {
    Serial.println("save: collection partition missing or incompatible; NOT writing");
    return false;
  }
  const esp_err_t err = nvs_flash_init_partition(COLLECTION_PARTITION);
  if (err != ESP_OK) {
    Serial.printf("save: collection NVS init failed (%d); NOT erasing\n", int(err));
    return false;
  }
#endif
  return true;
}

inline bool collectionOpen(Preferences &p, bool readOnly = false) {
  if (!collectionStorageReady()) return false;
  if (!p.begin(COLLECTION_NAMESPACE, readOnly, COLLECTION_PARTITION)) {
    Serial.println("save: collection namespace would not open");
    return false;
  }
  return true;
}

inline bool collectionReset() {
  Preferences p;
  if (!collectionOpen(p)) return false;
  if (!p.clear()) {
    Serial.println("save: collection reset failed");
    return false;
  }
  return true;
}
