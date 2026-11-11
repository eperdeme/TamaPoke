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

#ifdef TAMA_NATIVE_TEST
static void seedDuplicateLegacyCopies() {
  Preferences p;
  ck(p.begin("tamapoke", false), "open active save for duplicate-source fixture");
  uint8_t oldParty[PARTY_SLOTS * sizeof(PartyMon)] = {};
  uint8_t oldBox[36 * sizeof(PartyMon)] = {};
  uint8_t oldCheckpoint[20 + (PARTY_SLOTS + 36) * sizeof(PartyMon)] = {};
  ck(p.putBytes("party", oldParty, sizeof(oldParty)) == sizeof(oldParty) &&
     p.putBytes("box", oldBox, sizeof(oldBox)) == sizeof(oldBox) &&
     p.putBytes("pbA", oldCheckpoint, sizeof(oldCheckpoint)) == sizeof(oldCheckpoint) &&
     p.putBytes("pbB", oldCheckpoint, sizeof(oldCheckpoint)) == sizeof(oldCheckpoint),
     "seed the historical duplicate collection footprint");
}

static void seedLowHeadroom() {
  Preferences p;
  ck(p.begin("tamapoke", false), "open historical save for headroom fixture");
  uint8_t pet[124] = {}, player[368] = {};
  ck(p.putBytes("petA", pet, sizeof(pet)) == sizeof(pet) &&
     p.putBytes("petB", pet, sizeof(pet)) == sizeof(pet) &&
     p.putBytes("plyA", player, sizeof(player)) == sizeof(player) &&
     p.putBytes("plyB", player, sizeof(player)) == sizeof(player),
     "seed existing active-pet and player checkpoints");
  nvs_stats_t stats{};
  if (nvs_get_stats(nullptr, &stats) != ESP_OK) {
    ck(false, "read original NVS stats for the low-headroom fixture");
    return;
  }
  for (unsigned i = 0; stats.used_entries < 399 && i < 1000; ++i) {
    char key[16];
    snprintf(key, sizeof(key), "old%03u", i);
    if (!p.putUChar(key, uint8_t(i)) || nvs_get_stats(nullptr, &stats) != ESP_OK) {
      ck(false, "fill historical NVS fixture to its measured used-entry count");
      return;
    }
  }
  ck(stats.used_entries == 399 && stats.available_entries < 126,
     "historical customer footprint reproduces the original low-headroom warning");
}
#endif

static bool legacySourcesPresent() {
  Preferences p;
  if (!p.begin("tamapoke", true)) return false;
  return p.isKey("party") || p.isKey("box") ||
         p.isKey("pbA") || p.isKey("pbB");
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
  ck(kept && !legacySourcesPresent(),
     "widening the collection preserves every record before retiring legacy copies");
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
#ifdef TAMA_NATIVE_TEST
  {
    Party migrated;
    migrated.begin();
    ck(migrated.saveHealthy() && migrated.boxCount() == 36 && !legacySourcesPresent(),
       "native migration first verifies the expanded collection");
  }
  seedDuplicateLegacyCopies();
  seedLowHeadroom();
  nvs_stats_t beforePrune{};
  const bool measuredBefore = nvs_get_stats(nullptr, &beforePrune) == ESP_OK;
#endif
  Party p;
  p.begin();
  ck(p.saveHealthy() && p.writable() && p.boxCount() == 36,
     "36-slot collection migrates to writable verified storage");
  ck(!legacySourcesPresent(),
     "migration retires the old collection only after both replacement copies verify");
#ifdef TAMA_NATIVE_TEST
  nvs_stats_t afterPrune{};
  Preferences preserved;
  const bool petPlayerKept =
      preserved.begin("tamapoke", true) &&
      preserved.getBytesLength("petA") == 124 &&
      preserved.getBytesLength("petB") == 124 &&
      preserved.getBytesLength("plyA") == 368 &&
      preserved.getBytesLength("plyB") == 368;
  ck(measuredBefore && nvs_get_stats(nullptr, &afterPrune) == ESP_OK &&
     beforePrune.available_entries < 126 && afterPrune.available_entries >= 126 &&
     afterPrune.available_entries > beforePrune.available_entries && petPlayerKept,
     "verified pruning restores original NVS headroom without touching pet/player checkpoints");
  printf("      original NVS available=%u -> %u entries\n",
         unsigned(beforePrune.available_entries), unsigned(afterPrune.available_entries));
#endif
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
    ck(legacySourcesPresent(),
       "an interrupted migration keeps its source until replacement checkpoints verify");
    nvsResumeWrites();
    Party resumed;
    resumed.begin();
    ck(resumed.saveHealthy() && resumed.boxCount() == 36 && !legacySourcesPresent(),
       "interrupted migration resumes before retiring its verified legacy source");
  }

  Party activeCollection;
  activeCollection.begin();
  PartyMon expectedSlots[PARTY_SLOTS];
  memcpy(expectedSlots, activeCollection.slots, sizeof(expectedSlots));
  const uint16_t expectedBoxCount = activeCollection.boxCount();
  std::vector<PartyMon> expectedBox(BOX_SLOTS);
  memcpy(expectedBox.data(), activeCollection.box.data(), sizeof(PartyMon) * BOX_SLOTS);
  auto &collectionStore = nvsNamed("tpcollect:collection");
  const std::vector<uint8_t> goodA = collectionStore["cbA"];
  for (int budget = 0; budget < 4; ++budget) {
    Preferences old;
    old.begin("tamapoke", false);
    uint8_t stale[4] = {1, 2, 3, 4};
    ck(old.putBytes("party", expectedSlots, sizeof(expectedSlots)) == sizeof(expectedSlots) &&
       old.putBytes("box", expectedBox.data(), sizeof(PartyMon) * BOX_V323_SLOTS) ==
           sizeof(PartyMon) * BOX_V323_SLOTS &&
       old.putBytes("pbA", stale, sizeof(stale)) == sizeof(stale) &&
       old.putBytes("pbB", stale, sizeof(stale)) == sizeof(stale),
       "seed obsolete migration copies beside an active collection");
    old.end();

    collectionStore["cbA"].back() ^= 1;
    Party gated;
    gated.begin();
    ck(gated.saveHealthy() && legacySourcesPresent(),
       "one valid destination copy is not enough to retire the legacy source");
    collectionStore["cbA"] = goodA;

    nvsFailWritesAfter(budget);
    Party interruptedCleanup;
    interruptedCleanup.begin();
    nvsResumeWrites();
    Party resumedCleanup;
    resumedCleanup.begin();
    bool same = resumedCleanup.saveHealthy() &&
                resumedCleanup.boxCount() == expectedBoxCount &&
                !legacySourcesPresent();
    for (int i = 0; i < PARTY_SLOTS; ++i)
      same &= !memcmp(&resumedCleanup.slots[i], &expectedSlots[i], sizeof(PartyMon));
    for (int i = 0; i < BOX_SLOTS; ++i)
      same &= !memcmp(&resumedCleanup.box[i], &expectedBox[i], sizeof(PartyMon));
    ck(same, "interrupted legacy cleanup retries without changing any saved Pokemon");
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
  ck(!corrupt.writable() && !corrupt.saveHealthy(),
     "recovery failure never falls back to retired legacy copies");
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
