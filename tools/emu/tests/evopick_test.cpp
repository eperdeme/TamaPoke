// Branching evolutions must let the player choose an offered form.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 77;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void FakeESP::restart() { exit(0); }

void emuAdvanceMs(uint32_t ms);
void setup();
void render();
void onTap(int16_t x, int16_t y);
void onSwipeV(int dir);
void onSwipe(int dir);
void onRim(int dir);
uint8_t uiCurrentScreen();
uint8_t uiRimPages();
int uiTapFinger();
bool evoPickRowRect(uint8_t row, int *x, int *y, int *w, int *h);
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern uint8_t choiceKind;
extern bool evoPickOpen;
extern uint8_t evoPickPage;

static int failures = 0;
static void check(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) failures++;
}
static bool on(const char *name) {
  return !strcmp(SCREEN_NAME[uiCurrentScreen()], name);
}
static void prepareKirlia() {
  pet.dbgHatchAs(280, false);
  pet.speciesId = 281;
  pet.ageMinutes = (uint32_t)40 * MINUTES_PER_LEVEL;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 100;
  while (pet.hasLearnOffer()) pet.declineLearn();
}
static void openEvolutionPicker() {
  onTap(233, 204);
  onTap(233, 232);
}

int main() {
  gRegionArt = 0xFFFF;
  setup();
  render();
  if (pet.awaitingStarter()) pet.dbgHatchAs(4, false);

  for (uint8_t row = 0; row < 3; row++) {
    int x, y, w, h;
    check(evoPickRowRect(row, &x, &y, &w, &h) && h >= uiTapFinger(),
          "every evolution choice has a finger-sized hit target");
  }

  pet.dbgHatchAs(133, false);
  pet.ageMinutes = (uint32_t)40 * MINUTES_PER_LEVEL;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 100;
  render();
  openEvolutionPicker();
  check(evoPickOpen && on("evopick") && uiRimPages() == 3,
        "EEVEE opens a three-page picker for its eight offered forms");
  onSwipe(-1);
  check(evoPickOpen, "the horizontal tile gesture does not dismiss the modal");
  onRim(1);
  check(evoPickOpen && evoPickPage == 1, "the rim pages the evolution choices");
  onRim(1);
  check(evoPickOpen && evoPickPage == 2, "paging reaches the final evolution choices");
  int x, y, w, h;
  evoPickRowRect(1, &x, &y, &w, &h);
  onTap(x + w / 2, y + h / 2);
  check(!evoPickOpen && pet.speciesId == 700,
        "the last page can select SYLVEON without changing its target");
  emuAdvanceMs(EVOLVE_ANIM_MS + 100);

  prepareKirlia();
  render();
  check(pet.wantEvolveButton(), "a ready KIRLIA offers evolution");
  onTap(233, 204);
  check(choiceKind == 1, "EVOLVE opens the existing confirmation dialog");
  onTap(233, 232);
  check(evoPickOpen && on("evopick") && pet.speciesId == 281,
        "confirming a branch opens the picker without evolving yet");
  render();

  onSwipeV(1);
  check(!evoPickOpen && pet.speciesId == 281 && pet.wantEvolveButton(),
        "swiping down cancels and leaves evolution available");

  openEvolutionPicker();
  check(evoPickOpen, "the picker can be reopened");
  evoPickRowRect(1, &x, &y, &w, &h);
  onTap(x + w / 2, y + h / 2);
  check(!evoPickOpen && pet.speciesId == 475,
        "tapping GALLADE evolves to exactly the selected form");

  pet.dbgHatchAs(4, false);
  pet.ageMinutes = (uint32_t)20 * MINUTES_PER_LEVEL;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 100;
  emuAdvanceMs(EVOLVE_ANIM_MS + 100);
  while (pet.hasLearnOffer()) pet.declineLearn();
  render();
  for (int i = 0; i < 3 && choiceKind != 1; i++) {
    onTap(233, 204);
    emuAdvanceMs(300);
    render();
  }
  check(choiceKind == 1, "a linear evolution still uses the confirmation dialog");
  onTap(233, 232);
  check(!evoPickOpen && pet.speciesId == 5,
        "a linear evolution continues directly to its only target");

  printf("%s\n", failures ? "FAILURES" : "all good");
  return failures ? 1 : 0;
}
