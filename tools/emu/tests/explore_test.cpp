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

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}