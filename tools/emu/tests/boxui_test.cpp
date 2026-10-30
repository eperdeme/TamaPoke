// The box SCREEN across every box: the real sketch, real taps.
//
// With the file store mounted the box is ten boxes of 36. The title's arrows
// change box; the rim and the pages stay inside one box (six pages); every sort
// orders ONE box and is still a bijection over all of them (two cells resolving
// to one slot is how a creature gets overwritten); and a tap in box 2 opens the
// creature drawn there and can move it.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "boxstore.h"
#include <cstdio>
#include <cstring>
#include <set>
uint32_t g_seed = 21; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false;
int FakeSerial::available() { return 0; } String FakeSerial::readStringUntil(char) { return String(""); }
void FakeESP::restart() { exit(0); }

void setup(); void render(); void onTap(int16_t x, int16_t y);
uint8_t uiCurrentScreen(); uint8_t uiRimPages();
uint16_t boxSlotFor(uint16_t visual);
void partySlotPos(int i, int *cx, int *cy);
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern Party party;
extern bool partyOpen, boxOpen;
extern uint8_t boxPage, boxNo, boxSort, boxSwapFrom;
extern uint16_t boxDetail, boxSel;

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static bool on(const char *n) { return !strcmp(SCREEN_NAME[uiCurrentScreen()], n); }
static PartyMon mk(int dex, int lvl) { PartyMon m; m.dex = dex; m.level = lvl; return m; }

int main() {
  gRegionArt = 0xFFFF;
  setup();
  if (pet.awaitingStarter()) pet.dbgHatchAs(4, false);
  ck(boxStoreReady() && party.boxCap() == BOX_TOTAL, "the sketch mounts the extra boxes at boot");

  // fill box 1 and part of boxes 2..4, with species in DESCENDING order so a
  // sort by number has real work to do across the seam
  for (int i = 0; i < 100; i++) party.boxAdd(mk(900 - i * 7, 10 + i % 50));

  partyOpen = true; boxOpen = true; boxPage = 0; boxNo = 0;
  render();
  ck(on("box"), "the box screen is up");
  ck(uiRimPages() == 6, "the rim pages within one box: six pages");

  // every sort is a bijection over all 360 slots
  for (uint8_t sort = 0; sort < 3; sort++) {
    boxSort = sort;
    std::set<uint16_t> seen;
    for (uint16_t v = 0; v < BOX_TOTAL; v++) seen.insert(boxSlotFor(v));
    char w[80];
    snprintf(w, sizeof(w), "sort %u maps the 360 cells onto 360 distinct slots", sort);
    ck(seen.size() == BOX_TOTAL && *seen.rbegin() == BOX_TOTAL - 1, w);
  }
  boxSort = 1;   // by number
  {
    // box 1 holds the first 36 added (dex 900 down to 655), box 2 the next 36
    bool inBox1 = true, sorted1 = true, inBox2 = true;
    for (uint16_t v = 0; v < BOX_SLOTS; v++) inBox1 &= boxSlotFor(v) < BOX_SLOTS;
    for (uint16_t v = 1; v < BOX_SLOTS; v++)
      sorted1 &= party.boxAt(boxSlotFor(v - 1)).dex <= party.boxAt(boxSlotFor(v)).dex;
    for (uint16_t v = BOX_SLOTS; v < 2 * BOX_SLOTS; v++)
      inBox2 &= boxSlotFor(v) >= BOX_SLOTS && boxSlotFor(v) < 2 * BOX_SLOTS;
    ck(inBox1 && inBox2, "sorting never moves a creature into another box's view");
    ck(sorted1 && party.boxAt(boxSlotFor(0)).dex == 900 - 35 * 7,
       "and orders each box by number on its own");
    // box 3 is part full: its empties go last, within box 3
    ck(!party.boxAt(boxSlotFor(2 * BOX_SLOTS)).empty() &&
       party.boxAt(boxSlotFor(3 * BOX_SLOTS - 1)).empty(), "empties last, per box");
  }
  boxSort = 0;

  // every page of every box draws
  bool drew = true;
  for (uint8_t b = 0; b < BOX_TOTAL / BOX_SLOTS; b++)
    for (uint8_t pg = 0; pg < 6; pg++) { boxNo = b; boxPage = pg; render(); drew &= on("box"); }
  ck(drew, "every page of every box renders");
  boxNo = 0;

  // the title changes box, and wraps
  boxPage = 0; boxNo = 0; boxDetail = 0; render();
  onTap(233 + 75, 45);
  ck(boxNo == 1 && boxPage == 0, "the title's right arrow goes to box 2");
  onTap(233 - 75, 45);
  ck(boxNo == 0, "the left arrow back to box 1");
  onTap(233 - 75, 45);
  ck(boxNo == 9, "and from box 1 it wraps to box 10: the last box is one tap away");
  boxNo = 0; boxPage = 3; render();
  onTap(233 + 75, 45);
  ck(boxNo == 1 && boxPage == 0, "from mid-box it lands on the next box's first page");
  onTap(233, 45);
  ck(boxNo == 1 && boxOpen, "the label between the arrows is not a control");

  // box 2, first page: tap its first cell
  boxNo = 1; boxPage = 0;
  render();
  int cx, cy;
  partySlotPos(0, &cx, &cy);
  onTap(cx, cy);
  ck(boxDetail == BOX_SLOTS + 1, "a tap in box 2 opens its first creature");
  render();
  ck(party.boxAt(boxDetail - 1).dex == 900 - 36 * 7, "the sheet shows the creature drawn there");

  // arm a swap from the party side and complete it on that page
  boxDetail = 0;
  render();
  party.slots[3] = mk(25, 5);
  party.savePair();
  boxSwapFrom = 4;               // party slot 3, + 1
  onTap(cx, cy);
  ck(party.slots[3].dex == 900 - 36 * 7 && party.boxAt(BOX_SLOTS).dex == 25,
     "a party creature trades places with one in box 2 from the box screen");
  ck(boxSwapFrom == 0, "and the pending swap is cleared");

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
