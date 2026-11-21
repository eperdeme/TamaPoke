// Separated Pokedexes (ALL / HATCHED / CAUGHT) and creature origin tracking.
//
// 1. A hatched creature fills HATCHED (and its evolutions stay HATCHED).
// 2. A wild capture fills CAUGHT at capture time, and raising/evolving it keeps
//    it CAUGHT (never HATCHED).
// 3. Origin travels through PartyMon's two-byte signature (origin + ~origin);
//    stray bytes from older builds safely read as ORIGIN_UNKNOWN.
// 4. Shiny status is tracked per origin so a caught shiny never falsely marks
//    HATCHED as shiny.
// 5. Both v4.3.1 player checkpoints (no origin bitmaps) and PR #7 player
//    checkpoints (fixed=44 + 4 appended bitmaps) migrate cleanly.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "dex.h"
#include "ckpt.h"
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef TAMA_NATIVE_TEST
uint32_t g_seed = 4242;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
#endif

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

static void readyToEvolve(Pet &q) {
  q.ageMinutes = (uint32_t)99 * MINUTES_PER_LEVEL;
  q.fullness = q.joy = q.energy = q.hygiene = 100;
}

int main() {
  gRegionArt = 0xFFFF;

  // --- origin: hatched vs caught across evolutions and swaps -----------------
  {
    Pet q; q.begin();
    q.dbgHatchAs(280, false);                       // RALTS from an egg
    ck(q.hatchedLine(), "a creature out of the player's egg is hatched");
    ck(q.isHatchedRegistered(280), "and its species is in HATCHED");
    q.speciesId = 281; readyToEvolve(q);
    q.evolveTo(282);
    ck(q.isHatchedRegistered(282), "evolving a hatched creature fills HATCHED too");

    PartyMon wild; wild.dex = 361; wild.level = 30; wild.stateVersion = PARTYMON_STATE_VERSION;
    wild.ageMinutes = 29UL * MINUTES_PER_LEVEL;
    wild.setOrigin(ORIGIN_WILD);
    ck(!wild.hatched() && wild.originKind() == ORIGIN_WILD, "a capture record carries ORIGIN_WILD");
    q.switchTo(wild);
    ck(!q.hatchedLine() && q.originKind == ORIGIN_WILD, "making a capture live keeps ORIGIN_WILD");
    ck(q.isRegistered(361), "it counts as raised once active");
    ck(!q.isHatchedRegistered(361) && q.isCaughtRegistered(361), "in CAUGHT, NOT in HATCHED");
    readyToEvolve(q);
    q.evolveTo(478);                                // FROSLASS
    ck(q.isCaughtRegistered(478) && !q.isHatchedRegistered(478),
       "and what it evolves into is CAUGHT too, not HATCHED");

    PartyMon back = q.toPartyMon();
    ck(back.originKind() == ORIGIN_WILD, "banking the capture keeps ORIGIN_WILD");
    Pet r; r.begin(); r.dbgHatchAs(1, false);
    PartyMon h = r.toPartyMon();
    ck(h.hatched(), "banking a hatched creature marks the record hatched");
    q.switchTo(h);
    ck(q.hatchedLine(), "and switching it back in restores hatchedLine");

    // garbage in the old padding must never read as a valid origin
    PartyMon g; g.origin = ORIGIN_SIG_EGG; g.originChk = 0x00;
    ck(g.originKind() == ORIGIN_UNKNOWN, "a single stray signature byte is not an origin");
    g.origin = 0xAA; g.originChk = (uint8_t)~0xAA;
    ck(g.originKind() == ORIGIN_UNKNOWN, "an unknown complemented value is not an origin");
  }

  // --- three Pokedexes and per-origin shiny separation -----------------------
  {
    Pet q; q.begin();
    q.dbgHatchAs(25, false);                        // non-shiny hatched PIKACHU
    q.noteCaught(25, true);                         // shiny caught PIKACHU
    ck(q.inDex(DEXV_HATCHED, false, 25) && !q.inDex(DEXV_HATCHED, true, 25),
       "a caught shiny does not falsely mark HATCHED PIKACHU as shiny");
    ck(q.inDex(DEXV_CAUGHT, false, 25) && q.inDex(DEXV_CAUGHT, true, 25),
       "but CAUGHT PIKACHU is shiny");
    ck(q.inDex(DEXV_COMPLETE, false, 25) && q.inDex(DEXV_COMPLETE, true, 25),
       "and ALL PIKACHU is shiny");

    q.dbgHatchAs(133, true);                        // shiny hatched EEVEE
    ck(q.inDex(DEXV_HATCHED, true, 133) && !q.inDex(DEXV_CAUGHT, false, 133),
       "a shiny hatched creature is in HATCHED SHINY only, not CAUGHT");

    q.noteCaught(129, false);                       // non-shiny caught MAGIKARP
    ck(q.inDex(DEXV_CAUGHT, false, 129) && !q.inDex(DEXV_CAUGHT, true, 129),
       "a non-shiny capture is in CAUGHT only, not CAUGHT SHINY");
    ck(q.inDex(DEXV_COMPLETE, false, 129) && !q.isRegistered(129),
       "catching alone counts in ALL/CAUGHT without marking it raised");

    PartyMon old; old.dex = 147; old.level = 20; old.stateVersion = 1;
    old.ageMinutes = 19UL * MINUTES_PER_LEVEL;
    q.switchTo(old);
    ck(q.inDex(DEXV_COMPLETE, false, 147) && !q.inDex(DEXV_HATCHED, false, 147) &&
       !q.inDex(DEXV_CAUGHT, false, 147),
       "an unknown-origin pre-update record counts in ALL only");
  }

  // --- persistence & upgrade from PR #7 player checkpoint (fixed=44) ---------
  {
    {
      Pet q; q.begin();
      q.dbgHatchAs(280, false);
      q.noteCaught(700, true);
      q.saveNow();
    }
    Pet q2; q2.begin();
    ck(q2.isHatchedRegistered(280), "HATCHED survives a save and reload");
    ck(q2.inDex(DEXV_CAUGHT, true, 700), "a caught shiny survives a save and reload");
    ck(q2.hatchedLine(), "and so does the live creature's origin");

    // Simulate a PR #7 player checkpoint: fixed=44 (PLAYER_FIXED_V2) + 6 bitmaps
    const size_t FIXED_V2 = 44;
    const size_t dexBytes = sizeof(q2.dexReg);
    const size_t badgeN = GYM_REGIONS - 1;
    const size_t nameN = sizeof(q2.trainerName);
    const size_t body = FIXED_V2 + 6 * dexBytes + 4 * badgeN + 2 * REGION_COUNT + nameN;
    std::vector<uint8_t> pr7(body + CKPT_CRC, 0);
    ckptWr16(pr7.data() + 12, (uint16_t)dexBytes);
    pr7[14] = REGION_COUNT;
    pr7[15] = (uint8_t)badgeN;
    pr7[16] = (uint8_t)nameN;
    pr7[17] = 1;
    pr7[18] = 0;
    pr7[19] = (uint8_t)FIXED_V2;
    ckptWr16(pr7.data() + 42, 19); // memoHi
    size_t at = FIXED_V2;
    pr7[at + ((4 - 1) >> 3)] |= (uint8_t)(1 << ((4 - 1) & 7));       // dexReg: CHARMANDER
    at += dexBytes;
    at += dexBytes;                                                   // dexShinyReg
    at += 4 * badgeN + 2 * REGION_COUNT;
    memcpy(pr7.data() + at, "PEDRO", 5);                              // trainerName
    at += nameN;
    pr7[at + ((152 - 1) >> 3)] |= (uint8_t)(1 << ((152 - 1) & 7));    // dexHatched: CHIKORITA
    at += dexBytes;
    pr7[at + ((25 - 1) >> 3)] |= (uint8_t)(1 << ((25 - 1) & 7));      // dexShinyCaught: PIKACHU
    at += dexBytes;
    pr7[at + ((25 - 1) >> 3)] |= (uint8_t)(1 << ((25 - 1) & 7));      // dexCaught: PIKACHU
    at += dexBytes;
    pr7[at + ((152 - 1) >> 3)] |= (uint8_t)(1 << ((152 - 1) & 7));    // dexHatchedShiny: CHIKORITA
    ckptSeal(pr7.data(), body, 0x31594B54UL, 2, 99);

    Preferences pr; pr.begin("tamapoke", false);
    pr.remove("plyA"); pr.remove("plyB");
    pr.putBytes("plyA", pr7.data(), pr7.size());
    pr.end();

    Pet migrated; migrated.begin();
    ck(!strcmp(migrated.trainerName, "PEDRO") && migrated.memoHi == 19,
       "a PR #7 player checkpoint keeps trainer name and memoHi");
    ck(migrated.isRegistered(4), "dexReg aligns");
    ck(migrated.inDex(DEXV_HATCHED, false, 152) && migrated.inDex(DEXV_HATCHED, true, 152),
       "PR #7 dexHatched and dexHatchedShiny migrate cleanly");
    ck(migrated.inDex(DEXV_CAUGHT, false, 25) && migrated.inDex(DEXV_CAUGHT, true, 25),
       "PR #7 dexCaught and dexShinyCaught migrate cleanly");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
