// First boot: pick a region, then pick a starter from it.
//
// The screen is two steps now, and the thing worth pinning is that it reads its
// species from dex.h's per-region starter arrays rather than a copy. Those same
// arrays are the pool a region's FIRST EGG is drawn from (pet.cpp rollInRegion),
// where Kanto's five deliberately include Pikachu and Eevee -- so the array must
// not be trimmed to match the screen, and the screen must not drift if somebody
// reorders the array. This test is the thing standing between those two.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <set>
#include <cstring>
uint32_t g_seed=29; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void FakeESP::restart(){exit(0);}

void setup(); void render(); void onTap(int16_t x, int16_t y);
int16_t starterOf(uint8_t region, uint8_t i);
uint8_t starterCountShown(uint8_t region);
extern Pet pet;
extern bool starterRegionDone;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// where renderRegionPick draws row i, and where renderStarterSelect draws row i
static int regionRowY(int i){ return 108 + i*72 + 30; }
static int starterRowY(int i){ return 110 + i*(70+8) + 35; }

int main(){
  setup();
  render();
  ck(pet.awaitingStarter(), "a brand new save asks for a starter");

  // --- the canonical trio, pinned. Reordering REGIONS[].starters would change
  // the first screen anyone sees, and nothing else would notice.
  const int16_t want[3][3] = { {1,4,7}, {152,155,158}, {252,255,258} };
  for (uint8_t r = 0; r < 3; r++) {
    bool ok = starterCountShown(r) == 3;
    for (uint8_t i = 0; i < 3; i++) if (starterOf(r,i) != want[r][i]) ok = false;
    char m[64]; snprintf(m,sizeof(m),"%s offers its canonical three", REGIONS[r].name);
    ck(ok, m);
  }

  // --- and every one of them really belongs to that region's dex range
  {
    bool ok = true;
    for (uint8_t r = 0; r < 3; r++)
      for (uint8_t i = 0; i < starterCountShown(r); i++) {
        int16_t d = starterOf(r,i);
        if (d < REGIONS[r].lo || d > REGIONS[r].hi) ok = false;
      }
    ck(ok, "and each sits inside its own region's range");
  }

  // --- Kanto's egg pool is NOT trimmed to what the screen shows. This is the
  // one that fails if somebody "tidies" dex.h to match the choice screen.
  ck(REGIONS[0].starterCount > starterCountShown(0),
     "Kanto's egg pool still holds more than the three on screen");
  {
    bool pika=false, eevee=false;
    for (uint8_t i=0;i<REGIONS[0].starterCount;i++){
      if (REGIONS[0].starters[i]==25) pika=true;
      if (REGIONS[0].starters[i]==133) eevee=true;
    }
    ck(pika && eevee, "Pikachu and Eevee are still reachable as a first egg");
  }

  // --- drive the real two-step flow through onTap: Johto, then its middle one
  onTap(233, regionRowY(1));                  // JOHTO
  ck(pet.region == 1, "tapping a region on the first screen sets the egg region");
  ck(pet.awaitingStarter(), "and does not choose a creature by itself");

  render();                                   // now the starter list
  onTap(233, starterRowY(1));                 // CYNDAQUIL
  ck(!pet.awaitingStarter(), "tapping a starter finishes the first boot");
  ck(pet.eggPeek() == 155, "and it is the one that was tapped, from that region");

  // the egg really is Johto's, which is the whole point of choosing first
  ck(pet.eggPeek() >= REGIONS[1].lo && pet.eggPeek() <= REGIONS[1].hi,
     "the waiting egg belongs to the region that was picked");

  // --- and it survives a reload: region is persisted, the flow is not repeated
  {
    Pet again; again.begin();
    ck(again.region == 1, "the region choice is saved");
    ck(!again.awaitingStarter(), "and the first boot does not run a second time");
  }

  // --- the starter screen's random button: one of THAT region's three, and the
  // region chosen for later eggs is left alone
  {
    std::set<int16_t> seen;
    bool inRegion = true, regionKept = true;
    for (int k = 0; k < 100; k++) {
      nvs().clear();
      gRegionArt = 0xFFFF;                      // every pack on the card
      pet.begin();                              // a fresh save: asks for a starter
      starterRegionDone = true;
      pet.setRegion(2);                         // HOENN
      render();
      onTap(233, 350 + 24);
      if (pet.awaitingStarter()) { inRegion = false; break; }
      const int16_t d = pet.eggPeek();
      inRegion &= d == starterOf(2, 0) || d == starterOf(2, 1) || d == starterOf(2, 2);
      regionKept &= pet.region == 2;
      seen.insert(d);
    }
    ck(inRegion && seen.size() == 3, "the starter screen's random button picks one of that region's three");
    ck(regionKept, "and leaves the region chosen for later eggs alone");
  }

  // --- the region screen's random button: one of all the regions' starters,
  // and with no region chosen, later eggs come from all of them
  {
    std::set<int16_t> canon, seen;
    for (uint8_t r = 0; r < REGION_COUNT; r++)
      if (r != REGION_ALL) for (uint8_t i = 0; i < 3; i++) canon.insert(starterOf(r, i));
    bool allCanon = true, regionAll = true, skipped = true;
    for (int k = 0; k < 300; k++) {
      nvs().clear();
      gRegionArt = 0xFFFF;
      pet.begin();
      starterRegionDone = false;                // back on the region screen
      render();
      onTap(233, 336 + 22);
      skipped &= starterRegionDone;
      if (pet.awaitingStarter()) { allCanon = false; break; }
      allCanon &= canon.count(pet.eggPeek()) == 1;
      regionAll &= pet.region == REGION_ALL;
      seen.insert(pet.eggPeek());
    }
    ck(allCanon && skipped, "the region screen's random button picks a starter and skips the starter screen");
    ck(seen.size() == canon.size(), "and every one of the 27 starters can come out");
    ck(regionAll, "and the eggs after it come from all regions");
  }

  // --- a tap that misses every row must do nothing rather than pick row 0.
  // Starting over: an empty Pokedex is what makes it a first boot.
  {
    pet.factoryReset();
    Pet fresh; fresh.begin();
    ck(fresh.awaitingStarter(), "a wiped save asks again");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
