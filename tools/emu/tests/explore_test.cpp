// Explore chooses its own installed region and routes that choice into the
// real wild encounter builder without changing the egg region.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include <cstdio>

uint32_t g_seed = 0xE7A0;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void onTap(int16_t x, int16_t y);
bool startWildBattle(uint8_t region, bool hard);
int8_t exploreRegionHit(int16_t x, int16_t y);
int uiTapFinger();
// The region chooser's tap resolver and its geometry. The sprite-pack gate it
// enforces now covers the GYM ladders as well, which is a behaviour change worth
// pinning: it used to exempt them, so a ladder was enterable whether its pack was
// on the card or not -- and a gym leader with no sprite opens the fight with a bare
// dex number where a creature should be.
int regionPickTap(int16_t x, int16_t y, uint8_t mode);
int uiRegionRowCenterY(int row);
int uiRegionRowX();
int uiRegionRowsPerPage();
extern uint8_t rpickPage;
#define RP_FOR_GYMS 0
#define RP_FOR_DEX  1
extern Pet pet;
extern bool exploreOpen;
extern uint8_t exploreRegion;
extern int16_t wildDex;

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static bool measureRegionControl(int8_t direction, int16_t &left, int16_t &top,
                                 int16_t &right, int16_t &bottom) {
  left = top = 466;
  right = bottom = -1;
  for (int16_t py = 0; py < 466; py++)
    for (int16_t px = 0; px < 466; px++)
      if (exploreRegionHit(px, py) == direction) {
        if (px < left) left = px;
        if (px > right) right = px;
        if (py < top) top = py;
        if (py > bottom) bottom = py;
      }
  return right >= left && bottom >= top;
}

int main() {
  {
    Preferences owner;
    owner.begin("handle-probe", false);
    {
      Preferences alias = owner;
      ck(alias.putUChar("probe", 1) == 1, "a copied Preferences initially shares the open handle");
    }
    ck(owner.putUChar("probe", 2) == 0,
       "destroying a copied Preferences closes the original handle, like real NVS");
  }
  setup();
  pet.setRegion(0);
  pet.dbgHatchAs(6, false);
  gRegionArt = 0xFFFF;
  exploreOpen = true;
  exploreRegion = 0;

    int16_t rightL, rightT, rightR, rightB, leftL, leftT, leftR, leftB;
    ck(measureRegionControl(1, rightL, rightT, rightR, rightB),
      "the next-region control has a hit area");
    ck(measureRegionControl(-1, leftL, leftT, leftR, leftB),
      "the previous-region control has a hit area");
    ck(rightR - rightL + 1 >= uiTapFinger() && rightB - rightT + 1 >= uiTapFinger(),
      "the next-region control is finger-sized in both dimensions");
    ck(leftR - leftL + 1 >= uiTapFinger() && leftB - leftT + 1 >= uiTapFinger(),
      "the previous-region control is finger-sized in both dimensions");
    int16_t rightX = (rightL + rightR) / 2, rightY = (rightT + rightB) / 2;
    int16_t leftX = (leftL + leftR) / 2, leftY = (leftT + leftB) / 2;
  onTap(rightX, rightY);
  ck(exploreRegion == 1, "the Explore selector advances to Johto");
  ck(pet.region == 0, "changing Explore does not change the egg region");
  onTap(leftX, leftY);
  ck(exploreRegion == 0, "the Explore selector can move back to Kanto");

  gRegionArt = (uint16_t)((1u << 0) | (1u << 2));
  exploreRegion = 0;
  onTap(rightX, rightY);
  ck(exploreRegion == 2, "the selector skips a region whose pack is missing");

  gRegionArt = 0xFFFF;
  ck(startWildBattle(1, false), "an installed selected region starts an encounter");
  ck(wildDex >= REGIONS[1].lo && wildDex <= REGIONS[1].hi,
     "the encounter comes from the selected region, not the egg region");
    pet.rename("AFTERWILD");
    ck(pet.saveHealthy() && !pet.savePending(), "starting a wild battle does not invalidate saving");
    Pet afterEncounter;
    afterEncounter.begin();
    ck(afterEncounter.speciesId == pet.speciesId && !strcmp(afterEncounter.nick, "AFTERWILD"),
      "a save made after starting the encounter survives reload");

  // ---- THE SPRITE-PACK GATE NOW COVERS THE GYM LADDERS TOO
  //
  // It read `forGyms || regionAvailable(i)` in the draw path and `mode !=
  // RPICK_FOR_GYMS && !regionAvailable(i)` in the tap path, so a ladder was always
  // open regardless of whether its creatures could be drawn. Alola could already
  // hit that; Galar and Paldea -- which have twenty-nine art-less species between
  // them -- would have made it ordinary.
  //
  // Both modes are checked, because the whole fault was one mode being exempt.
  {
    rpickPage = 0;
    const int rx = uiRegionRowX();
    gRegionArt = 0xFFFF;                       // every pack present
    int allOpen = 0;
    for (int row = 0; row < uiRegionRowsPerPage(); row++)
      if (regionPickTap((int16_t)rx, (int16_t)uiRegionRowCenterY(row), RP_FOR_GYMS) == row)
        allOpen++;
    ck(allOpen == uiRegionRowsPerPage(),
       "with every pack installed, every gym ladder on the page is selectable");

    gRegionArt = 1u << 0;                      // KANTO only
    ck(regionPickTap((int16_t)rx, (int16_t)uiRegionRowCenterY(0), RP_FOR_GYMS) == 0,
       "with only Kanto installed, Kanto's ladder is still selectable");
    ck(regionPickTap((int16_t)rx, (int16_t)uiRegionRowCenterY(1), RP_FOR_GYMS) < 0,
       "but a ladder whose sprite pack is missing is refused, not entered");
    ck(regionPickTap((int16_t)rx, (int16_t)uiRegionRowCenterY(1), RP_FOR_DEX) < 0,
       "exactly as the Pokedex already refused it");

    // AND IT FAILS SAFE. gRegionArt defaults to all-set, so a board with no card
    // at all behaves as it always did rather than losing every gym.
    gRegionArt = 0xFFFF;
    ck(regionPickTap((int16_t)rx, (int16_t)uiRegionRowCenterY(1), RP_FOR_GYMS) == 1,
       "and putting the packs back makes them selectable again");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}