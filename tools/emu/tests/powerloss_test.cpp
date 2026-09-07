// A logical pet save spans many legacy NVS keys. A power cut between them must
// load either the complete previous checkpoint or the complete new one, never
// a creature assembled from both.
//
// Every block here starts from a cleared NVS deliberately. A test at the end of
// a long suite runs in whatever state the suite left behind, and this file is
// all about what is on disk -- so nothing may inherit a store it did not write.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>
#include <vector>

uint32_t g_seed = 31;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
// A clock the test can push forward. A ceremony ends when millis() passes
// ceremonyUntil, so a frozen clock would leave every farewell running forever
// and the handover would never happen at all.
static uint32_t gNow = 1;
uint32_t millis() { return gNow; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

// ---------------------------------------------------------------------------
// Fixture helpers.
//
// The CRC lives here as well as in pet.cpp. That duplication is unavoidable and
// deliberate: these tests have to MANUFACTURE blobs the firmware will accept --
// a record from an older version, a generation near the wrap -- and there is no
// way to do that without the sealing algorithm. It is not a second opinion about
// the rule, because every fixture it builds is then handed to the real reader.
static uint16_t fixtureCrc(const uint8_t *p, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= (uint16_t)p[i] << 8;
    for (int b = 0; b < 8; b++)
      c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}

static void reseal(std::vector<uint8_t> &b) {
  uint16_t c = fixtureCrc(b.data(), b.size() - 2);
  b[b.size() - 2] = (uint8_t)(c & 0xFF);
  b[b.size() - 1] = (uint8_t)(c >> 8);
}

static uint32_t genOf(const std::vector<uint8_t> &b) {
  if (b.size() < 12) return 0;
  uint32_t g;
  memcpy(&g, b.data() + 8, 4);
  return g;
}

static std::vector<uint8_t> slotBytes(const char *key) {
  auto it = nvs().find(key);
  return it == nvs().end() ? std::vector<uint8_t>() : it->second;
}

// Which of the two slots holds the newer complete record -- i.e. the one the
// firmware would load, and therefore the one a torn write would have produced.
static const char *newerSlot(const char *a, const char *b) {
  std::vector<uint8_t> x = slotBytes(a), y = slotBytes(b);
  if (x.size() < 12) return b;
  if (y.size() < 12) return a;
  return genOf(x) > genOf(y) ? a : b;
}

static bool dexBitSet(const std::vector<uint8_t> &blob, int16_t dex) {
  size_t idx = (size_t)((dex - 1) >> 3);
  return idx < blob.size() && (blob[idx] & (1 << ((dex - 1) & 7)));
}

// Poisons the legacy keys so that anything the CHECKPOINT fails to supply reads
// back obviously wrong.
//
// This is load-bearing, not tidying. Almost every field in the pet record is
// also mirrored to a legacy key, so an assertion like "speciesId == 143" is
// satisfied either way -- and every one of the fixture tests below passed with
// the checkpoint reader disabled entirely until this existed. That is CLAUDE.md
// § "A test that proves the transcription rather than the firmware", exactly.
static void poisonLegacy() {
  auto put8 = [](const char *k, uint8_t v) { nvs()[k] = std::vector<uint8_t>{ v }; };
  uint16_t notASpecies = 999;
  nvs()["dexn"] = std::vector<uint8_t>(2);
  memcpy(nvs()["dexn"].data(), &notASpecies, 2);
  nvs()["age"] = std::vector<uint8_t>(4, 0);
  put8("tatk", 0);
  put8("tdef", 0);
  put8("tspe", 0);
  nvs()["nick"] = std::vector<uint8_t>{ 'X', 0 };
  // The player-wide keys have no sensible poison value, so they simply go: an
  // absent key leaves the array at its zero initialiser.
  for (const char *k : { "dexreg", "dexsh", "badgX", "badhX", "strk", "ghi", "tnam" })
    nvs().erase(k);
}

static void seedDratini(Pet &pet) {
  pet.begin();
  pet.dbgHatchAs(147, false);
  pet.ivAtk = pet.ivDef = pet.ivSpe = pet.ivHp = 31;
  pet.ageMinutes = 28UL * MINUTES_PER_LEVEL;
  pet.trAtk = 100;
  pet.trDef = 7;
  pet.trSpe = 9;
  pet.lastSeenEpoch = 100000;
  pet.saveNow();
}

static void raiseDragonair(Pet &pet) {
  pet.speciesId = 148;
  pet.ageMinutes = 35UL * MINUTES_PER_LEVEL;
  pet.trAtk = pet.trDef = pet.trSpe = 100;
  pet.lastSeenEpoch = 200000;
}

int main() {
  {
    Pet pet;
    seedDratini(pet);
    raiseDragonair(pet);

    // In the legacy write order, eleven successful writes stop immediately
    // after tatk. This is the field pattern reported in issue #3.
    nvsFailWritesAfter(11);
    pet.saveNow();
    nvsResumeWrites();

    Pet loaded;
    loaded.begin();
    printf("      loaded dex=%d age=%lu training=%u/%u/%u\n", loaded.speciesId,
           (unsigned long)loaded.ageMinutes, loaded.trAtk, loaded.trDef, loaded.trSpe);
    ck(loaded.speciesId == 148 && loaded.ageMinutes == 35UL * MINUTES_PER_LEVEL,
       "a cut after the Attack write does not devolve the creature");
    ck(loaded.trAtk == 100 && loaded.trDef == 100 && loaded.trSpe == 100,
       "a cut after the Attack write keeps all three training values together");
  }

  {
    nvs().clear();
    Pet pet;
    seedDratini(pet);
    raiseDragonair(pet);

    nvsFailWritesAfter(0);
    pet.saveNow();
    nvsResumeWrites();
    ck(pet.savePending(), "a failed checkpoint remains pending for retry");

    Pet loaded;
    loaded.begin();
    ck(loaded.speciesId == 147 && loaded.ageMinutes == 28UL * MINUTES_PER_LEVEL,
       "a cut before commit keeps the complete previous creature");
    ck(loaded.trAtk == 100 && loaded.trDef == 7 && loaded.trSpe == 9,
       "a cut before commit keeps the complete previous training state");
  }

  {
    nvs().clear();
    Pet pet;
    seedDratini(pet);
    raiseDragonair(pet);
    pet.saveNow();

    const char *newest = newerSlot("petA", "petB");
    bool hasNewest = !slotBytes(newest).empty();
    ck(hasNewest, "the newer alternating checkpoint was written");
    if (hasNewest) nvs()[newest][0] ^= 0xFF;

    Pet loaded;
    loaded.begin();
    ck(loaded.speciesId == 147 && loaded.ageMinutes == 28UL * MINUTES_PER_LEVEL,
       "a corrupt newest checkpoint falls back to the complete previous one");
    ck(loaded.trAtk == 100 && loaded.trDef == 7 && loaded.trSpe == 9,
       "checksum fallback never assembles a pet from legacy key fragments");
  }

  // --- the PLAYER's progress, which the first fix did not cover -------------
  //
  // Badges, the Pokedex, the streak and the records were ~25 more keys written
  // one after another. A cut between the badge arrays and the dex bitmap leaves
  // a badge won with no record of the creature that earned it: issue #3's exact
  // shape, one layer out.
  {
    nvs().clear();
    Pet pet;
    pet.begin();
    pet.dbgHatchAs(25, false);
    pet.saveNow();                       // baseline: no badges, empty dex

    const int16_t FAR_DEX = 400;         // nowhere near the live creature
    pet.badgesX[0] = 0x0F;               // four Johto badges
    pet.dexReg[(FAR_DEX - 1) >> 3] |= 1 << ((FAR_DEX - 1) & 7);

    // Two checkpoint writes, then twenty legacy ones: badgX is the 19th and
    // badhX the 20th, so this stops with the badges written and dexreg still
    // holding the baseline. The exact count is not the point -- the companion
    // assertion below is what proves the cut landed where it was meant to.
    nvsFailWritesAfter(2 + 20);
    pet.saveNow();
    nvsResumeWrites();

    // PROVE THE MECHANISM ENGAGED. Without this the assertion after it would
    // pass just as happily with the player checkpoint removed, and if a key is
    // ever added to the legacy run and the cut moves, this fails loudly rather
    // than the test quietly proving nothing. CLAUDE.md § "A test that proves the
    // transcription rather than the firmware" is a list of what that costs.
    bool legacyBadge = slotBytes("badgX").size() >= 2 && nvs()["badgX"][0] == 0x0F;
    bool legacyDex = dexBitSet(slotBytes("dexreg"), FAR_DEX);
    ck(legacyBadge && !legacyDex,
       "the cut really did land between the badge keys and the Pokedex");

    Pet loaded;
    loaded.begin();
    ck(loaded.badgesX[0] == 0x0F && loaded.isRegistered(FAR_DEX),
       "the player checkpoint keeps badges and the Pokedex in step across a cut");
  }

  // Growing the dex, the region table or the gym ladder must not shift anything
  // already stored. The player record carries its own dimensions for that, so a
  // shorter stored array lands in the front and dex bit n keeps its meaning.
  {
    nvs().clear();
    Pet pet;
    pet.begin();
    pet.dbgHatchAs(7, false);
    pet.dexReg[0] = 0x05;                // dex 1 and 3
    pet.badgesHardX[0] = 0x20;
    pet.streak = 9;
    pet.gameHi = 1234;
    strncpy(pet.trainerName, "MISTY", sizeof(pet.trainerName) - 1);
    pet.saveNow();

    // Truncate the record as a build with a smaller dex would have written it:
    // fewer dex bytes, and the header saying so.
    const char *slot = newerSlot("plyA", "plyB");
    std::vector<uint8_t> small = slotBytes(slot);
    ck(small.size() > 60, "the player checkpoint was written");
    if (small.size() > 60) {
      const size_t FIXED = 42;
      uint16_t storedDex;
      memcpy(&storedDex, small.data() + 12, 2);
      const size_t shrinkTo = 19;                    // dex 1..152, the Kanto-era size
      std::vector<uint8_t> out(small.begin(), small.begin() + FIXED);
      // dexReg prefix, then dexShinyReg prefix, then the rest unchanged
      out.insert(out.end(), small.begin() + FIXED, small.begin() + FIXED + shrinkTo);
      out.insert(out.end(), small.begin() + FIXED + storedDex,
                 small.begin() + FIXED + storedDex + shrinkTo);
      out.insert(out.end(), small.begin() + FIXED + 2 * storedDex, small.end() - 2);
      uint16_t newDex = (uint16_t)shrinkTo;
      memcpy(out.data() + 12, &newDex, 2);
      uint16_t total = (uint16_t)(out.size() + 2);
      memcpy(out.data() + 6, &total, 2);
      out.push_back(0);
      out.push_back(0);
      reseal(out);
      nvs()[slot] = out;
      nvs().erase(std::string(slot) == "plyA" ? "plyB" : "plyA");
      poisonLegacy();          // only the player record can answer any of this

      Pet loaded;
      loaded.begin();
      ck(loaded.isRegistered(1) && loaded.isRegistered(3) && !loaded.isRegistered(2),
         "a player record from a smaller dex keeps every bit's meaning");
      ck(loaded.badgesHardX[0] == 0x20 && loaded.streak == 9 && loaded.gameHi == 1234,
         "and every field after the shortened arrays still reads correctly");
      ck(!strcmp(loaded.trainerName, "MISTY"), "including the trainer name at the very end");
    }
  }

  // --- the handover, which was only ever in RAM -----------------------------
  //
  // update() hands the creature over and then newEgg() saves the egg. The party
  // write does not happen until the player accepts a slot, which may be a banner
  // or a whole chooser screen later -- and a 4-second hold on the power key in
  // that window used to lose the creature outright, with the save that erased it
  // already committed.
  {
    nvs().clear();
    Pet pet;
    pet.begin();
    pet.dbgHatchAs(6, false);                  // CHARIZARD
    pet.ageMinutes = 80UL * MINUTES_PER_LEVEL;
    strncpy(pet.nick, "BLAZE", sizeof(pet.nick) - 1);
    pet.saveNow();

    pet.startFarewell();
    gNow += CEREMONY_MS + 1000;
    pet.update(gNow);                          // snapshotForParty(), then newEgg()
    ck(pet.endedKind != CER_NONE, "the farewell hands the creature over");
    ck(pet.isEgg(), "and leaves a new egg in its place");

    // Power goes away here, before the player has tapped the banner.
    Pet rebooted;
    rebooted.begin();
    ck(rebooted.endedKind != CER_NONE,
       "a creature waiting for a party slot survives losing power");
    ck(rebooted.endedMon.dex == 6 && !strcmp(rebooted.endedMon.nick, "BLAZE"),
       "and it is the same creature, not a blank record");
    ck(rebooted.isEgg(),
       "with the new egg still in place -- the handover did not undo the ceremony");

    // And exactly once: taking the slot has to clear the durable copy too.
    rebooted.clearEnded();
    Pet again;
    again.begin();
    ck(again.endedKind == CER_NONE,
       "once it has a slot it is not offered again on the next boot");
  }

  // A runaway is not banked, and an early retire gives the creature up for good.
  // Neither may leave anything waiting for a slot on the next boot.
  {
    nvs().clear();
    Pet pet;
    pet.begin();
    pet.dbgHatchAs(19, false);
    pet.ageMinutes = 30UL * MINUTES_PER_LEVEL;
    pet.saveNow();
    pet.startRunaway();
    gNow += CEREMONY_MS + 1000;
    pet.update(gNow);

    Pet rebooted;
    rebooted.begin();
    ck(rebooted.endedKind == CER_NONE,
       "a runaway leaves nothing waiting for a slot, across a reboot too");
  }

  // --- a record written by an EARLIER build ---------------------------------
  //
  // Rejecting a checkpoint means falling back to the legacy keys, which is the
  // torn-write path all of this replaces. So the reader has to accept the v1
  // layout, and this asserts the compatibility CLAIM -- that v1's body is a
  // prefix of the current one -- by deriving the fixture from what the current
  // writer produced rather than restating a layout the test would then own.
  {
    nvs().clear();
    Pet pet;
    pet.begin();
    pet.dbgHatchAs(143, false);                // SNORLAX
    pet.trAtk = 55;
    pet.ageMinutes = 40UL * MINUTES_PER_LEVEL;
    strncpy(pet.nick, "TUBBY", sizeof(pet.nick) - 1);
    pet.saveNow();

    const char *slot = newerSlot("petA", "petB");
    std::vector<uint8_t> v2 = slotBytes(slot);
    ck(v2.size() > 72, "the current record is longer than v1's 72 bytes");
    if (v2.size() > 72) {
      const size_t V1_BODY = 70;
      std::vector<uint8_t> v1(v2.begin(), v2.begin() + V1_BODY);
      v1[4] = 1; v1[5] = 0;                    // version 1
      v1[6] = 72; v1[7] = 0;                   // size 72
      v1.push_back(0);
      v1.push_back(0);
      reseal(v1);
      nvs()[slot] = v1;
      nvs().erase(std::string(slot) == "petA" ? "petB" : "petA");
      poisonLegacy();          // so a rejected v1 record cannot pass on the legacy keys

      Pet loaded;
      loaded.begin();
      ck(loaded.speciesId == 143 && loaded.trAtk == 55 &&
             loaded.ageMinutes == 40UL * MINUTES_PER_LEVEL,
         "a v1 checkpoint from an earlier build still loads");
      ck(!strcmp(loaded.nick, "TUBBY"), "including the last field of the v1 body");
      ck(loaded.endedKind == CER_NONE,
         "and having no v2 tail reads as nothing waiting for a slot");
    }
  }

  // A record from a LATER build: longer than this one understands, but the same
  // magic and a valid CRC, so the prefix must still be read rather than thrown
  // away in favour of the legacy keys.
  {
    nvs().clear();
    Pet pet;
    pet.begin();
    pet.dbgHatchAs(94, false);
    pet.trSpe = 33;
    pet.saveNow();

    const char *slot = newerSlot("petA", "petB");
    std::vector<uint8_t> future = slotBytes(slot);
    if (future.size() > 12) {
      future.insert(future.end() - 2, 16, 0xA5);   // a field this build has never heard of
      future[4] = 99;                              // ... from version 99
      uint16_t total = (uint16_t)future.size();
      memcpy(future.data() + 6, &total, 2);
      reseal(future);
      nvs()[slot] = future;
      nvs().erase(std::string(slot) == "petA" ? "petB" : "petA");
      poisonLegacy();

      Pet loaded;
      loaded.begin();
      ck(loaded.speciesId == 94 && loaded.trSpe == 33,
         "a record from a later build is read as far as this one understands it");
    }
  }

  // --- the invariant the whole pair rests on -------------------------------
  //
  // A save must never touch the slot a reload would fall back to. It holds
  // because the next generation is always (loaded + 1) and the slot is chosen by
  // parity, so the two always differ -- but nothing asserted it, and it is the
  // single assumption everything else here depends on. Tested by destroying the
  // slot the save just wrote, which is exactly what a power cut during that write
  // would leave behind.
  {
    nvs().clear();
    Pet pet;
    pet.begin();
    pet.dbgHatchAs(133, false);
    pet.trDef = 5;
    pet.saveNow();
    bool held = true;
    for (int i = 0; i < 8 && held; i++) {
      const uint8_t prev = pet.trDef;          // what the fallback slot still holds
      std::vector<uint8_t> before = slotBytes("petA");
      pet.trDef = (uint8_t)(10 + i);
      pet.saveNow();
      const char *wrote = slotBytes("petA") != before ? "petA" : "petB";
      NvsStore keep = nvs();
      nvs()[wrote].clear();                    // as if that write never landed
      // A value only the surviving CHECKPOINT can supply -- comparing speciesId
      // alone would pass even if both slots were gone, since the legacy keys
      // carry it too.
      nvs()["tdef"] = std::vector<uint8_t>{ 0xEE };
      Pet fallback;
      fallback.begin();
      if (fallback.speciesId != 133 || fallback.trDef != prev) held = false;
      nvs() = keep;
    }
    ck(held, "a save never overwrites the slot a reload would fall back to");
  }

  // The generation counter is a uint32_t compared by signed subtraction, so it
  // survives wrapping. Crafted by hand, because four billion saves is not a test.
  {
    nvs().clear();
    Pet a;
    a.begin();
    a.dbgHatchAs(151, false);                  // MEW: the older record
    a.saveNow();
    std::vector<uint8_t> older = slotBytes(newerSlot("petA", "petB"));

    nvs().clear();
    Pet b;
    b.begin();
    b.dbgHatchAs(25, false);                   // PIKACHU: the newer one
    b.saveNow();
    std::vector<uint8_t> newer = slotBytes(newerSlot("petA", "petB"));

    ck(older.size() > 12 && newer.size() > 12, "two records to compare");
    if (older.size() > 12 && newer.size() > 12) {
      uint32_t top = 0xFFFFFFFFu, wrapped = 0u;
      memcpy(older.data() + 8, &top, 4);
      memcpy(newer.data() + 8, &wrapped, 4);
      reseal(older);
      reseal(newer);
      nvs().clear();
      nvs()["init"] = std::vector<uint8_t>{ 1 };
      nvs()["petA"] = older;                   // a plain unsigned compare picks this
      nvs()["petB"] = newer;

      Pet loaded;
      loaded.begin();
      ck(loaded.speciesId == 25,
         "the generation counter wraps without falling back to a stale record");
    }
  }

  // Flip every byte of the newer slot in turn. Whatever loads must be ONE
  // complete creature -- the new one or the previous one -- and never a mixture,
  // which is the only thing the reported bug ever looked like.
  {
    nvs().clear();
    Pet pet;
    pet.begin();
    pet.dbgHatchAs(147, false);
    pet.trAtk = 7; pet.trDef = 8; pet.trSpe = 9;
    pet.saveNow();
    pet.speciesId = 148;
    pet.trAtk = 70; pet.trDef = 80; pet.trSpe = 90;
    pet.saveNow();

    const char *newest = newerSlot("petA", "petB");
    NvsStore good = nvs();
    size_t n = slotBytes(newest).size();
    int mixtures = 0, sawOld = 0, sawNew = 0;
    for (size_t i = 0; i < n; i++) {
      nvs() = good;
      nvs()[newest][i] ^= 0xFF;
      Pet r;
      r.begin();
      bool isNew = r.speciesId == 148 && r.trAtk == 70 && r.trDef == 80 && r.trSpe == 90;
      bool isOld = r.speciesId == 147 && r.trAtk == 7 && r.trDef == 8 && r.trSpe == 9;
      if (isNew) sawNew++;
      else if (isOld) sawOld++;
      else mixtures++;
    }
    nvs() = good;
    printf("      %u bytes flipped: %d kept the new creature, %d fell back, %d mixed\n",
           (unsigned)n, sawNew, sawOld, mixtures);
    ck(mixtures == 0,
       "no single-byte corruption of a checkpoint ever yields a mixed creature");
    // The companion: the sweep has to have actually broken records, or "never
    // mixed" would be vacuously true of a sweep that changed nothing.
    ck(sawOld > 0, "and the corruption really did force the fallback");
  }

  // --- saying so when saving is broken -------------------------------------
  {
    nvs().clear();
    Pet pet;
    pet.begin();
    pet.dbgHatchAs(4, false);
    pet.saveNow();
    ck(pet.saveHealthy(), "a working save reports healthy");

    nvsFailWritesAfter(0);
    pet.saveNow();
    ck(pet.saveHealthy(), "one refused write is not yet a broken save");
    pet.saveNow();
    pet.saveNow();
    nvsResumeWrites();
    ck(!pet.saveHealthy(), "three refused in a row reports broken, so the panel can say so");
    ck(pet.savePending(), "and the save is still pending rather than dropped");

    pet.saveNow();
    ck(pet.saveHealthy(), "and one success clears it again");
    ck(!pet.savePending(), "leaving nothing pending");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
