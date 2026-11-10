#include "Arduino.h"
#include "Preferences.h"
#include "party.h"
#include "dex.h"
#include "ckpt.h"
#include <cstdio>
#include <cstring>
#include <vector>
#ifdef TAMA_NATIVE_TEST
#include <nvs.h>
#else
uint32_t g_seed = 31;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false; bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
#endif

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++bad;
}

static PartyMon mon(int i) {
  PartyMon m;
  memset(&m, 0, sizeof(m));
  m.dex = i % DEX_COUNT + 1;
  m.level = i % 100 + 1;
  m.ivAtk = i % 32;
  m.trDef = i % 71;
  m.shiny = i % 2;
  m.stateVersion = 1;
  m.ageMinutes = i * 60;
  m.bond = i % 101;
  snprintf(m.nick, sizeof(m.nick), "MON%u", unsigned(i));
  return m;
}

static void seedLegacy() {
  ck(collectionReset(), "test collection starts empty");
  Preferences p;
  ck(p.begin("tamapoke", false) && p.clear(), "test legacy namespace starts empty");
  PartyMon members[PARTY_SLOTS];
  PartyMon oldBox[36];
  for (int i = 0; i < PARTY_SLOTS; ++i) members[i] = mon(200 + i);
  for (int i = 0; i < 36; ++i) oldBox[i] = mon(i);
  ck(p.putBytes("party", members, sizeof(members)) == sizeof(members) &&
     p.putBytes("box", oldBox, sizeof(oldBox)) == sizeof(oldBox),
     "real 36-slot legacy source stored");
}

static bool originalIntact() {
  Preferences p;
  if (!p.begin("tamapoke", true)) return false;
  PartyMon oldBox[36];
  if (p.getBytes("box", oldBox, sizeof(oldBox)) != sizeof(oldBox)) return false;
  for (int i = 0; i < 36; ++i) {
    const PartyMon expected = mon(i);
    if (memcmp(&oldBox[i], &expected, sizeof(expected))) return false;
  }
  return !p.isKey("cbA") && !p.isKey("cbB");
}

static void checkSnapshotFormats() {
  seedLegacy();
  Party source;
  source.begin();
  for (int i = 0; i < BOX_SLOTS; ++i) source.box[i] = mon(i);
  std::vector<uint8_t> old(20 + sizeof(PartyMon) * (PARTY_SLOTS + BOX_SLOTS), 0);
  ckptWr16(old.data() + 12, sizeof(PartyMon));
  ckptWr16(old.data() + 14, PARTY_SLOTS);
  ckptWr16(old.data() + 16, BOX_SLOTS);
  memcpy(old.data() + 18, source.slots, sizeof(source.slots));
  memcpy(old.data() + 18 + sizeof(source.slots), source.box.data(),
         sizeof(PartyMon) * BOX_SLOTS);
  ckptSeal(old.data(), old.size() - CKPT_CRC, COLLECTION_LEGACY_MAGIC, 1, 100);
  Preferences store;
  ck(collectionOpen(store) &&
     store.putBytes("cbA", old.data(), old.size()) == old.size() &&
     store.putBytes("cbB", old.data(), old.size()) == old.size(),
     "an existing KBC2 collection is stored with its original firmware codec");
  Party upgraded;
  upgraded.begin();
  bool kept = upgraded.saveHealthy() && upgraded.boxCount() == BOX_SLOTS;
  for (int i = 0; i < BOX_SLOTS; ++i)
    kept &= !memcmp(&upgraded.box[i], &source.box[i], sizeof(PartyMon));
  ck(kept && originalIntact(), "widening the collection header preserves every old record");
  std::vector<uint8_t> check(COLLECTION_MAX_BYTES);
  CollectionLayout layout;
  bool bothWide = true;
  for (const char *key : {"cbA", "cbB"}) {
    bothWide &= collectionRead(store, key, check.data(), check.size(), layout,
                               PARTY_SLOTS, BOX_SLOTS, DEX_COUNT) != 0 && layout.wide;
  }
  ck(bothWide, "both replacement copies verify in the wide format");

#ifndef TAMA_NATIVE_TEST
  for (int budget = 0; budget < 3; ++budget) {
    store.putBytes("cbA", old.data(), old.size());
    store.putBytes("cbB", old.data(), old.size());
    nvsFailWritesAfter(budget);
    Party interrupted;
    interrupted.begin();
    nvsResumeWrites();
    Party resumed;
    resumed.begin();
    bool intact = resumed.saveHealthy() && resumed.boxCount() == BOX_SLOTS;
    for (int i = 0; i < BOX_SLOTS; ++i)
      intact &= !memcmp(&resumed.box[i], &source.box[i], sizeof(PartyMon));
    ck(intact, "interrupted header upgrade loads complete old or new copies");
  }
#endif

  const size_t stride = sizeof(PartyMon) + 8;
  std::vector<uint8_t> future(collectionSize(stride, PARTY_SLOTS, BOX_SLOTS), 0xA5);
  size_t at = COLLECTION_FIXED;
  for (int i = 0; i < PARTY_SLOTS + BOX_SLOTS; ++i) {
    const PartyMon &record = i < PARTY_SLOTS ? source.slots[i] : source.box[i - PARTY_SLOTS];
    memcpy(future.data() + at, &record, sizeof(record));
    at += stride;
  }
  const uint32_t length = collectionSeal(future.data(), at, stride, PARTY_SLOTS, BOX_SLOTS, 200);
  ck(length == future.size() && length > UINT16_MAX,
     "a single firmware-sealed checkpoint actually exceeds 65535 bytes without truncation");
  ck(store.putBytes("cbA", future.data(), future.size()) == future.size(),
     "the over-64-KiB checkpoint commits");
  Party extended;
  extended.begin();
  bool fullPrefix = !extended.writable() && !extended.saveHealthy() &&
                    extended.boxCount() == BOX_SLOTS;
  for (int i = 0; i < BOX_SLOTS; ++i)
    fullPrefix &= !memcmp(&extended.box[i], &source.box[i], sizeof(PartyMon));
  ck(fullPrefix && store.getBytesLength("cbA") == future.size(),
     "future record prefixes stay readable but read-only without losing their unknown suffixes");
  ck(!extended.savePair(), "unknown future record suffixes cannot be discarded by saving");

  future.assign(collectionSize(sizeof(PartyMon), PARTY_SLOTS, BOX_SLOTS), 0);
  memcpy(future.data() + COLLECTION_FIXED, source.slots, sizeof(source.slots));
  memcpy(future.data() + COLLECTION_FIXED + sizeof(source.slots), source.box.data(),
         sizeof(PartyMon) * BOX_SLOTS);
  collectionSeal(future.data(), future.size() - CKPT_CRC, sizeof(PartyMon),
                 PARTY_SLOTS, BOX_SLOTS, 201);
  const auto current = future;
  future.insert(future.begin() + COLLECTION_HEADER, 8, 0x5C);
  ckptWr16(future.data() + 6, COLLECTION_HEADER + 8);
  ckptWr32(future.data() + 8, future.size());
  ckptWr32(future.data() + 12, 201);
  ckptWr16(future.data() + future.size() - CKPT_CRC,
           ckptCrc(future.data(), future.size() - CKPT_CRC));
  ck(store.putBytes("cbA", future.data(), future.size()) == future.size(),
     "a later extended header commits without changing its fixed prefix");
  extended.begin();
  bool headerKept = !extended.writable() && !extended.saveHealthy() &&
                    extended.boxCount() == BOX_SLOTS;
  for (int i = 0; i < BOX_SLOTS; ++i)
    headerKept &= !memcmp(&extended.box[i], &source.box[i], sizeof(PartyMon));
  ck(headerKept, "the declared header length, not today's offsets, locates future records");
  ck(!extended.savePair() && store.getBytesLength("cbA") == future.size(),
     "unknown header fields alone prevent an older build from rewriting them");

  future = current;
  future.insert(future.end() - CKPT_CRC, 9, 0x6D);
  ckptWr32(future.data() + 8, future.size());
  ckptWr32(future.data() + 12, 202);
  ckptWr16(future.data() + future.size() - CKPT_CRC,
           ckptCrc(future.data(), future.size() - CKPT_CRC));
  ck(store.putBytes("cbA", future.data(), future.size()) == future.size(),
     "a later top-level suffix commits beside a complete older frame");
  extended.begin();
  ck(!extended.writable() && !extended.savePair() && extended.boxCount() == BOX_SLOTS &&
     !memcmp(&extended.box[BOX_SLOTS - 1], &source.box[BOX_SLOTS - 1], sizeof(PartyMon)),
     "unknown top-level suffixes remain readable but cannot be discarded by saving");

  ckptWr16(future.data() + ckptRd16(future.data() + 6) + 6, DEX_COUNT + 1);
  ckptWr32(future.data() + 12, 203);
  ckptWr16(future.data() + future.size() - CKPT_CRC,
           ckptCrc(future.data(), future.size() - CKPT_CRC));
  ck(store.putBytes("cbA", future.data(), future.size()) == future.size(),
     "a newer valid frame contains a species this build does not know");
  Party unknown;
  unknown.begin();
  ck(!unknown.writable() && !unknown.saveHealthy() && !unknown.savePair(),
     "an unsupported newest frame blocks writes instead of rolling back to an older collection");

  ckptWr16(future.data() + ckptRd16(future.data() + 6) + 6, source.slots[0].dex);
  future.resize(COLLECTION_MAX_BYTES + 1, 0xCC);
  ckptWr32(future.data() + 8, future.size());
  ckptWr32(future.data() + 12, 204);
  ckptWr16(future.data() + future.size() - CKPT_CRC,
           ckptCrc(future.data(), future.size() - CKPT_CRC));
  ck(store.putBytes("cbA", future.data(), future.size()) == future.size(),
     "an oversized future frame remains present beside an older supported copy");
  Party oversized;
  oversized.begin();
  ck(!oversized.writable() && !oversized.saveHealthy() && !oversized.savePair() &&
     store.getBytesLength("cbA") == future.size(),
     "an unreadable future size cannot trigger stale fallback and overwrite its bytes");
}

int main() {
  seedLegacy();
  Party p;
  p.begin();
  ck(p.saveHealthy() && p.writable() && p.boxCount() == 36,
     "36-slot collection migrates to writable verified storage");
  ck(originalIntact(), "migration leaves original save bytes and namespace intact");
  for (int i = 0; i < BOX_SLOTS; ++i) p.box[i] = mon(i);
  ck(p.savePair() && p.savePair(), "both full-size checkpoints commit");
  Party restored;
  restored.begin();
  bool equal = restored.saveHealthy() && restored.boxCount() == BOX_SLOTS;
  for (int i = 0; i < BOX_SLOTS; ++i)
    equal &= memcmp(&p.box[i], &restored.box[i], sizeof(PartyMon)) == 0;
  ck(equal, "every record above and below slot 255 survives fresh-object reload");
  const PartyMon last = p.box[BOX_SLOTS - 1];
  ck(p.swapPartyBox(0, BOX_SLOTS - 1), "last slot swaps without narrowing");
  restored.begin();
  ck(!memcmp(&restored.slots[0], &last, sizeof(last)),
     "last-slot swap actually persists");
  Preferences activation;
  ck(collectionOpen(activation) && activation.remove("active"),
     "simulate activation-marker interruption after expanded collection commits");
  restored.begin();
  ck(restored.saveHealthy() && restored.boxCount() == BOX_SLOTS &&
     !memcmp(&restored.slots[0], &last, sizeof(last)),
     "activation recovery keeps newest expanded records, not the stale original box");

#ifdef TAMA_NATIVE_TEST
  for (int i = 0; i < 48; ++i) {
    p.box[256].level = i % 100 + 1;
    if (!p.savePair()) { ck(false, "collection survives real NVS compaction"); break; }
  }
  nvs_stats_t stats{};
  ck(nvs_get_stats(COLLECTION_PARTITION, &stats) == ESP_OK &&
     stats.available_entries > (sizeof(PartyMon) * (BOX_SLOTS + PARTY_SLOTS) / 32 + 126),
     "full collection retains another rewrite plus compaction headroom");
  printf("      collection used=%u available=%u total=%u\n",
         unsigned(stats.used_entries), unsigned(stats.available_entries),
         unsigned(stats.total_entries));
#else
  for (int budget = 0; budget < 5; ++budget) {
    seedLegacy();
    nvsFailWritesAfter(budget);
    Party interrupted;
    interrupted.begin();
    nvsResumeWrites();
    Party resumed;
    resumed.begin();
    ck(resumed.saveHealthy() && resumed.boxCount() == 36 && originalIntact(),
       "interrupted migration resumes without losing source records");
  }
  seedLegacy();
  Party failed;
  failed.begin();
  const PartyMon before = failed.slots[0];
  nvsFailWritesAfter(0);
  ck(!failed.replaceAt(0, mon(800)) && !failed.saveHealthy() &&
     !memcmp(&failed.slots[0], &before, sizeof(before)),
     "failed replacement is visible and rolls back RAM");
  nvsResumeWrites();
  Party reloaded;
  reloaded.begin();
  ck(!memcmp(&reloaded.slots[0], &before, sizeof(before)),
     "failed replacement leaves durable creature unchanged");
  auto &store = nvsNamed("tpcollect:collection");
  store["cbA"].back() ^= 1;
  Party fallback;
  fallback.begin();
  ck(fallback.saveHealthy(), "one corrupt checkpoint uses the other complete copy");
  store["cbB"].back() ^= 1;
  Party corrupt;
  corrupt.begin();
  ck(!corrupt.writable() && !corrupt.saveHealthy() && !corrupt.savePair(),
     "two corrupt activated copies block writes rather than resurrect stale legacy data");
  ck(originalIntact(), "recovery failure never overwrites the original collection");
  ck(collectionReset(), "simulate complete loss of the dedicated collection namespace");
  Party missing;
  missing.begin();
  ck(!missing.writable() && !missing.saveHealthy(),
     "original activation marker prevents stale rollback even when new namespace is missing");
#endif
  checkSnapshotFormats();
  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
