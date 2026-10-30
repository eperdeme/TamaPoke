// The evolution choice, driven through the real screens.
//
// EVOLVE on a branching creature must not pick for the player: it opens a list
// of the forms, the tapped one is what it becomes, and backing out changes
// nothing (the EVOLVE button is still there). A linear creature keeps the old
// two-button dialog. Also pins the HATCHED Pokedex filter against the real
// galleryPass() the grid draws with.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=77; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void FakeESP::restart(){exit(0);}

void emuAdvanceMs(uint32_t ms);
void setup(); void render(); void onTap(int16_t x, int16_t y); void onSwipeV(int dir);
uint8_t uiCurrentScreen();
bool galleryPass(int16_t dex); bool galleryMember(int16_t dex);
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern Party party;
extern uint8_t choiceKind;
extern bool evoPickOpen;
extern uint8_t galleryFilter;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static bool on(const char *n){ return !strcmp(SCREEN_NAME[uiCurrentScreen()], n); }

// geometry, from TamaPoke.ino
static const int EVO_BTN_CY = 172 + 32;      // EVO_BTN_Y + EVO_BTN_H/2
static const int CONFIRM_B1_CY = 206 + 26;   // CONFIRM_B1_Y + CONFIRM_BTN_H/2
static int pickRowY(int i){ return 100 + i * 64 + 27; }

static void kirliaReady(){
  pet.dbgHatchAs(280, false);                 // RALTS, from an egg
  pet.speciesId = 281;                        // KIRLIA
  pet.ageMinutes = (uint32_t)40 * MINUTES_PER_LEVEL;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 100;
}

int main(){
  gRegionArt = 0xFFFF;
  setup();
  render();
  if (pet.awaitingStarter()) { pet.dbgHatchAs(4, false); }

  // --- a branch opens the choice list --------------------------------------
  kirliaReady();
  render();
  ck(pet.wantEvolveButton(), "a ready KIRLIA shows the EVOLVE button");
  onTap(233, EVO_BTN_CY);
  ck(choiceKind == 1, "tapping it opens the evolve / keep dialog");
  onTap(233, CONFIRM_B1_CY);
  ck(evoPickOpen && on("evopick"), "EVOLVE on a branch opens the choice list instead of picking");
  ck(pet.speciesId == 281, "and has not evolved yet");
  render();                                    // must draw and flush

  // backing out changes nothing
  onSwipeV(+1);                                // swipe down = back
  ck(!evoPickOpen, "swiping down closes the list");
  ck(pet.speciesId == 281 && pet.wantEvolveButton(), "still a KIRLIA, and EVOLVE is still offered");

  // the tapped form is what it becomes
  onTap(233, EVO_BTN_CY);
  onTap(233, CONFIRM_B1_CY);
  ck(evoPickOpen, "the list opens again");
  onTap(233, pickRowY(1));                     // second row = GALLADE
  ck(!evoPickOpen, "picking a form closes the list");
  ck(pet.speciesId == 475, "and the creature is the GALLADE that was tapped");
  ck(pet.isHatchedRegistered(475), "a hatched KIRLIA's evolution lands in HATCHED");

  // --- a linear evolution keeps the old dialog -------------------------------
  pet.dbgHatchAs(4, false);                    // CHARMANDER
  pet.ageMinutes = (uint32_t)20 * MINUTES_PER_LEVEL;
  pet.fullness = pet.joy = pet.energy = pet.hygiene = 100;
  emuAdvanceMs(EVOLVE_ANIM_MS + 100);         // let GALLADE's evolution animation finish
  render();
  // The first tap after a hatch can be taken by its banner; tap until the
  // dialog is up (the same EVOLVE path as before this change).
  for (int i = 0; i < 3 && choiceKind != 1; i++) { onTap(233, EVO_BTN_CY); emuAdvanceMs(300); render(); }
  ck(choiceKind == 1, "a one-way evolution still shows the evolve / keep dialog");
  onTap(233, CONFIRM_B1_CY);
  ck(!evoPickOpen, "a one-way evolution does not open the list");
  ck(pet.speciesId == 5, "it simply evolves into CHARMELEON");

  // --- the classic Pokedex: every cell drawn, membership decides colour -------
  {
    PartyMon wild; wild.dex = 361; wild.level = 30; wild.stateVersion = 1;
    wild.ageMinutes = 29UL * MINUTES_PER_LEVEL; wild.setOrigin(ORIGIN_WILD);
    pet.switchTo(wild);                          // a capture, now raised
    ck(galleryPass(1) && galleryPass(151), "every species has a cell, whatever the view");
    galleryFilter = 0;                           // COMPLETE
    ck(galleryMember(361) && galleryMember(475), "COMPLETE holds both the capture and the egg's line");
    galleryFilter = 2;                           // HATCHED
    ck(!galleryMember(361), "HATCHED does not hold the capture");
    ck(galleryMember(475) && galleryMember(280), "but holds the egg's RALTS and its GALLADE");
    galleryFilter = 4;                           // CAUGHT
    ck(galleryMember(361) && !galleryMember(280), "CAUGHT holds the capture and not the egg's line");
    galleryFilter = 1;                           // COMPLETE SHINY
    ck(!galleryMember(361), "no shinies, so COMPLETE SHINY is empty of it");
    galleryFilter = 0;
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
