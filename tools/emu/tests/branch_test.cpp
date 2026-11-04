// Branching evolutions and the HATCHED Pokedex.
//
// 1. Every species in EVO_BRANCHES offers all of its forms (art and pack
//    permitting), every alternative is evolution-only (GALLADE no longer hatches),
//    and evolveTo() lands exactly on the form asked for -- and refuses one that is
//    not an option, so a stale choice can never hand out an unreachable form.
// 2. The origin of a creature: hatched from the player's egg vs caught. HATCHED
//    is filled by a hatch and by evolving a hatched creature; a capture made the
//    live creature fills RAISED but never HATCHED, and neither does its evolution.
// 3. The new bitmaps survive a save/load, and the origin survives a trip through
//    the party (PartyMon padding) without changing the stored stride.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "dex.h"
#include "noart.h"
#include <cstdio>
#include <cstring>
#ifndef TAMA_NATIVE_TEST
uint32_t g_seed = 4242;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0, g_touchY=0; volatile bool g_touchDown=false;
bool wasPressed=false;
uint32_t millis(){ return 0; }
void FakeESP::restart(){ exit(0); }
int FakeSerial::available(){ return 0; }
String FakeSerial::readStringUntil(char){ return String(""); }
void sfxPlay(uint8_t){}
#endif

static int bad=0;
static void ck(bool ok,const char*w){ printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++; }

static void readyToEvolve(Pet &q) {
  q.ageMinutes = (uint32_t)99 * MINUTES_PER_LEVEL;   // past every gate
  q.fullness = q.joy = q.energy = q.hygiene = 100;
}

int main(){
  gRegionArt = 0xFFFF;

  // --- the table itself -----------------------------------------------------
  {
    bool firstIsTable = true, allEvo = true;
    for (int i = 0; i < EVO_BRANCH_COUNT; i++) {
      const EvoBranch &b = EVO_BRANCHES[i];
      if (b.to[0] != (int16_t)DEX_TBL[b.from].evolvesTo) firstIsTable = false;
      for (int k = 0; k < b.count; k++)
        if (DEX_TBL[b.to[k]].rarity != R_EVO) {
          allEvo = false;
          printf("      %s (%d) can still hatch\n", DEX_TBL[b.to[k]].name, b.to[k]);
        }
    }
    ck(firstIsTable, "each branch's first form is the table's own evolvesTo");
    ck(allEvo, "every branch target is evolution-only (none hatches from an egg)");
    ck(DEX_TBL[475].rarity == R_EVO && DEX_TBL[478].rarity == R_EVO,
       "GALLADE and FROSLASS in particular no longer hatch");
  }

  Pet p; p.begin();
  // --- options --------------------------------------------------------------
  {
    int16_t o[EVO_BRANCH_MAX];
    uint8_t n = p.evoOptions(281, o);   // KIRLIA
    ck(n == 2 && o[0] == 282 && o[1] == 475, "KIRLIA offers GARDEVOIR and GALLADE");
    n = p.evoOptions(236, o);           // TYROGUE
    ck(n == 3, "TYROGUE offers all three Hitmon");
    n = p.evoOptions(4, o);             // CHARMANDER: linear
    ck(n == 1 && o[0] == 5, "a linear line still offers exactly its one target");
    n = p.evoOptions(6, o);             // CHARIZARD: final
    ck(n == 0, "a final form offers nothing");
    gRegionArt = 1u << 2;               // HOENN only
    n = p.evoOptions(281, o);
    ck(n == 1 && o[0] == 282, "without the Sinnoh pack, KIRLIA only offers GARDEVOIR");
    gRegionArt = 0xFFFF;
  }

  // --- evolveTo picks exactly the form asked for -----------------------------
  {
    Pet q; q.begin(); q.dbgHatchAs(280, false);   // RALTS
    q.speciesId = 281;                            // straight to KIRLIA
    readyToEvolve(q);
    ck(q.canEvolveNow(), "a cared-for KIRLIA past its gate can evolve");
    ck(!q.evolveTo(6), "evolveTo refuses a form that is not an option (CHARIZARD)");
    ck(q.speciesId == 281, "and nothing changed");
    ck(q.evolveTo(475), "evolveTo accepts GALLADE");
    ck(q.speciesId == 475, "and it is a GALLADE now");
    ck(q.isRegistered(475), "GALLADE is registered as raised");
  }
  {
    Pet q; q.begin(); q.dbgHatchAs(133, false);   // EEVEE
    readyToEvolve(q);
    ck(q.evolveTo(197), "EEVEE can be told to become UMBREON");
    ck(q.speciesId == 197, "and becomes exactly UMBREON");
  }

  // --- origin: hatched vs caught --------------------------------------------
  {
    Pet q; q.begin();
    q.dbgHatchAs(280, false);                     // RALTS from an egg
    ck(q.hatchedLine(), "a creature out of the player's egg is hatched");
    ck(q.isHatchedRegistered(280), "and its species is in HATCHED");
    q.speciesId = 281; readyToEvolve(q);
    q.evolveTo(282);
    ck(q.isHatchedRegistered(282), "evolving a hatched creature fills HATCHED too");

    // a wild capture becomes the live creature
    PartyMon wild; wild.dex = 361; wild.level = 30; wild.stateVersion = 1;
    wild.ageMinutes = 29UL * MINUTES_PER_LEVEL;   // SNORUNT
    wild.setOrigin(ORIGIN_WILD);
    ck(!wild.hatched(), "a capture record is not hatched");
    q.switchTo(wild);
    ck(!q.hatchedLine(), "making a capture the live creature keeps it a capture");
    ck(q.isRegistered(361), "it counts as raised");
    ck(!q.isHatchedRegistered(361) && q.isCaughtRegistered(361), "in CAUGHT, NOT in HATCHED");
    readyToEvolve(q);
    q.evolveTo(478);                              // FROSLASS
    ck(q.isCaughtRegistered(478) && !q.isHatchedRegistered(478),
       "and what it evolves into is CAUGHT too, not HATCHED");

    // the origin travels through the party record
    PartyMon back = q.toPartyMon();
    ck(!back.hatched(), "banking the capture keeps it a capture");
    Pet r; r.begin(); r.dbgHatchAs(1, false);
    PartyMon h = r.toPartyMon();
    ck(h.hatched(), "banking a hatched creature marks the record hatched");
    q.switchTo(h);
    ck(q.hatchedLine(), "and switching it back in restores hatchedLine");

    // garbage in the old padding must never read as hatched
    PartyMon g; g.origin = 0x48; g.originChk = 0x00;
    ck(g.originKind() == ORIGIN_UNKNOWN, "a single stray signature byte is not an origin");
  }

  // --- the three Pokedexes --------------------------------------------------
  {
    Pet q; q.begin();
    q.dbgHatchAs(25, true);                       // a SHINY PIKACHU from an egg
    ck(q.inDex(DEXV_HATCHED, false, 25) && q.inDex(DEXV_HATCHED, true, 25),
       "a shiny hatched one is in HATCHED and HATCHED SHINY");
    ck(q.inDex(DEXV_COMPLETE, false, 25) && q.inDex(DEXV_COMPLETE, true, 25),
       "and in COMPLETE and COMPLETE SHINY");
    ck(!q.inDex(DEXV_CAUGHT, false, 25), "but not in CAUGHT");
    q.noteCaught(129, false);                     // a plain MAGIKARP caught, never raised
    ck(q.inDex(DEXV_CAUGHT, false, 129) && !q.inDex(DEXV_CAUGHT, true, 129),
       "a non-shiny capture is in CAUGHT only, not CAUGHT SHINY");
    ck(q.inDex(DEXV_COMPLETE, false, 129) && !q.inDex(DEXV_COMPLETE, true, 129),
       "and in COMPLETE but not COMPLETE SHINY");
    ck(!q.inDex(DEXV_HATCHED, false, 129), "and not in HATCHED");
    ck(!q.isRegistered(129), "catching alone does not make it 'raised'");
    q.noteCaught(700, true);
    ck(q.inDex(DEXV_CAUGHT, true, 700) && q.inDex(DEXV_COMPLETE, true, 700),
       "a shiny capture is in CAUGHT SHINY and COMPLETE SHINY");
    // an unknown-origin (pre-update) record fills neither origin dex
    PartyMon old; old.dex = 147; old.level = 20; old.stateVersion = 1;
    old.ageMinutes = 19UL * MINUTES_PER_LEVEL;
    q.switchTo(old);
    ck(q.inDex(DEXV_COMPLETE, false, 147) && !q.inDex(DEXV_HATCHED, false, 147) &&
       !q.inDex(DEXV_CAUGHT, false, 147),
       "an unknown-origin creature counts in COMPLETE only");
  }

  // --- persistence ----------------------------------------------------------
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
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
