// EEVEE's branch, which is the one evolution the DexEntry table cannot express.
//
// evolvesTo is a single field, so only VAPOREON (134) is in the data; the other
// seven live in the generated EEVEE_EVOS list. That list is ALSO what marks all
// eight evolution-only, so "can be reached" and "cannot hatch" come from one
// source and cannot drift -- which they had, badly: the branch was hardcoded
// 134..136 for six generations while ESPEON, UMBREON, LEAFEON, GLACEON and
// SYLVEON sat in the dex as rare BASE forms, hatching straight out of eggs.
//
// The part that must not regress is the filter. A branch is only offered if the
// species has art at all AND its region's sprite pack is on the card. Without
// that, a Kanto-only player's Eevee could turn into Sylveon and be a creature
// that can only ever draw as a dex number -- permanently, since evolution is
// one-way.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "dex.h"
#include "noart.h"
#include <cstdio>
#include <cstring>
#ifndef TAMA_NATIVE_TEST
uint32_t g_seed = 99;
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

int main(){
  ck(EEVEE_EVO_COUNT == 8, "all eight Eeveelutions are in the generated list");

  // every branch is evolution-ONLY, so none of them can hatch
  {
    int hatchable=0;
    for (int i=0;i<EEVEE_EVO_COUNT;i++)
      if (DEX_TBL[EEVEE_EVOS[i]].rarity != R_EVO) {
        printf("      %s (%d) rarity %u -- can hatch\n",
               DEX_TBL[EEVEE_EVOS[i]].name, EEVEE_EVOS[i], DEX_TBL[EEVEE_EVOS[i]].rarity);
        hatchable++;
      }
    ck(hatchable==0, "and none of them hatches from an egg any more");
  }

  Pet p; p.begin();
  gRegionArt = 0xFFFF;                       // every pack present

  // with everything available, all eight must be offered
  {
    int16_t opts[EEVEE_EVO_COUNT];
    uint8_t n = p.eeveeOptions(opts);
    ck(n == EEVEE_EVO_COUNT, "with every pack on the card, all eight are offered");
  }

  // THE FILTER: only Kanto installed -> only the three Kanto branches
  {
    gRegionArt = 1u << 0;                    // KANTO only
    int16_t opts[EEVEE_EVO_COUNT];
    uint8_t n = p.eeveeOptions(opts);
    bool onlyKanto = true;
    for (uint8_t i=0;i<n;i++) if (opts[i] > 151) onlyKanto = false;
    ck(n == 3 && onlyKanto,
       "with only the Kanto pack, only Vaporeon/Jolteon/Flareon are offered");
  }

  // and evolving really lands on one of them, not on a Kalos mon
  {
    gRegionArt = 1u << 0;
    bool everWrong = false;
    for (int trial=0; trial<40; trial++) {
      Pet q; q.begin(); q.dbgHatchAs(DEX_EEVEE, false);
      q.ageMinutes = 60UL * 40;              // past the level-30 gate
      q.fullness=q.joy=q.energy=q.hygiene=100;
      if (!q.canEvolveNow()) { everWrong = true; break; }
      q.evolve();
      if (q.speciesId < 134 || q.speciesId > 136) { everWrong = true;
        printf("      evolved into dex %d with only Kanto installed\n", q.speciesId); break; }
    }
    ck(!everWrong, "40 evolutions with only Kanto installed all stay in Kanto");
  }

  // negative check: that guard must be able to FAIL, so prove the same code
  // reaches a later region once its pack is there
  {
    gRegionArt = 0xFFFF;
    bool sawLater = false;
    for (int trial=0; trial<60 && !sawLater; trial++) {
      Pet q; q.begin(); q.dbgHatchAs(DEX_EEVEE, false);
      q.ageMinutes = 60UL * 40;
      q.fullness=q.joy=q.energy=q.hygiene=100;
      q.evolve();
      if (q.speciesId > 151) sawLater = true;
    }
    ck(sawLater, "and with every pack it does reach past Kanto (so the check above bites)");
  }

  // no branch may be a species with no art, whatever the packs say
  {
    gRegionArt = 0xFFFF;
    int16_t opts[EEVEE_EVO_COUNT];
    uint8_t n = p.eeveeOptions(opts);
    int artless=0;
    for (uint8_t i=0;i<n;i++) if (!speciesHasArt(opts[i])) artless++;
    ck(artless==0, "no offered branch is a species with no sprite anywhere");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
