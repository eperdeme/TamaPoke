// Jukebox feature tests: menu entry, UI paging & track selection, auto-stop on
// screen-off/sleep, listening joy reward + daily cap + mute exclusion, unlock
// rules, and plyA/plyB persistence across reboots and new eggs.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "audio.h"
#include "music.h"
#include <cstdio>
#include <cstring>

uint32_t g_seed = 7;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;

void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void loop();
void render();
void onTap(int16_t x, int16_t y);
void onSwipe(int dir);
void onSwipeV(int dir);
int uiMenuRowCenterY(int i);
void jukeRowRect(int i, int *x, int *y, int *w, int *h);
void jukeNavRect(int which, int *x, int *y, int *w, int *h);
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern bool menuOpen, jukeOpen, screenOff;
extern uint8_t jukePage, dimStage;

static int fails = 0;
static void check(bool ok, const char *msg) {
  if (!ok) {
    printf("FAIL: %s\n", msg);
    fails++;
  }
}

static void tapCenter(int x, int y, int w, int h) {
  onTap((int16_t)(x + w / 2), (int16_t)(y + h / 2));
}

int main() {
  setup();
  pet.dbgSetSeen(1700000000UL);
  pet.dbgHatchAs(25, false);

  // --- Initial unlocks & silent boot check ---
  check(pet.tuneUnlocked(TUNE_TITLE), "Title starts unlocked");
  check(!pet.tuneUnlocked(TUNE_PALLET), "Pallet Town stays locked until checkTunes runs");
  pet.checkTunes(false);
  check(pet.tuneUnlocked(TUNE_PALLET), "Pallet Town unlocks after hatch + checkTunes");
  check(!pet.tuneUnlocked(TUNE_CENTER), "Pokemon Center starts locked until 3-day streak");
  check(!pet.tuneUnlocked(TUNE_ROUTE1), "Route 1 starts locked");
  check(!pet.tuneUnlocked(TUNE_WILD), "Wild Battle starts locked");
  check(!pet.tuneUnlocked(TUNE_TRN), "Trainer Battle starts locked");
  check(!pet.tuneUnlocked(TUNE_GYM), "Gym Battle starts locked");
  check(!pet.tuneUnlocked(TUNE_WIN), "Victory starts locked");
  check(!pet.tuneUnlocked(TUNE_LAVENDER), "Lavender Town starts locked");
  check(!pet.tuneToast(), "no tune unlock toast on silent checkTunes");

  // --- Menu -> Jukebox ---
  menuOpen = true;
  onTap(233, (int16_t)uiMenuRowCenterY(3));
  check(jukeOpen && !menuOpen, "menu row 3 opens jukebox");
  check(jukePage == 0, "jukebox opens on page 0");
  check(!strcmp(SCREEN_NAME[uiCurrentScreen()], "jukebox"), "uiCurrentScreen is jukebox");

  // --- Row taps: Title (row 0), Pallet (row 1), stop, locked Route 1 (row 2) ---
  int rx, ry, rw, rh;
  jukeRowRect(0, &rx, &ry, &rw, &rh);
  tapCenter(rx, ry, rw, rh);
  check(audioCurrentTrack() == TUNE_TITLE, "tapping row 0 plays TUNE_TITLE");

  jukeRowRect(1, &rx, &ry, &rw, &rh);
  tapCenter(rx, ry, rw, rh);
  check(audioCurrentTrack() == TUNE_PALLET, "tapping row 1 switches to TUNE_PALLET");

  tapCenter(rx, ry, rw, rh);
  check(audioCurrentTrack() == 0xFF, "tapping active track stops playback");

  jukeRowRect(2, &rx, &ry, &rw, &rh);
  tapCenter(rx, ry, rw, rh);
  check(audioCurrentTrack() == 0xFF, "tapping locked Route 1 does not play");

  jukeRowRect(0, &rx, &ry, &rw, &rh);
  tapCenter(rx, ry, rw, rh);
  check(audioCurrentTrack() == TUNE_TITLE, "restarting TUNE_TITLE succeeds");

  // --- Nav footer & swipes ---
  int nx, ny, nw, nh;
  jukeNavRect(1, &nx, &ny, &nw, &nh);
  tapCenter(nx, ny, nw, nh);
  check(jukePage == 1 && jukeOpen, "next button moves to page 1");

  jukeNavRect(-1, &nx, &ny, &nw, &nh);
  tapCenter(nx, ny, nw, nh);
  check(jukePage == 0 && jukeOpen, "prev button moves back to page 0");

  onSwipe(-1);
  check(jukePage == 1 && jukeOpen, "horizontal swipe -1 advances jukebox page");
  onSwipe(1);
  check(jukePage == 0 && jukeOpen, "horizontal swipe +1 returns to page 0");

  // Swipe down closes modal while music stays active for pet dancing
  onSwipeV(1);
  check(!jukeOpen, "swipe down closes jukebox");
  check(audioCurrentTrack() == TUNE_TITLE, "song keeps playing after closing jukebox");

  // Reopen and close via CLOSE button
  menuOpen = true;
  onTap(233, (int16_t)uiMenuRowCenterY(3));
  jukeNavRect(0, &nx, &ny, &nw, &nh);
  tapCenter(nx, ny, nw, nh);
  check(!jukeOpen && audioCurrentTrack() == TUNE_TITLE, "CLOSE button exits without stopping music");

  // --- Auto-stop on screen-off & sleep in loop() ---
  pet.musicOn = true;
  loop();
  check(pet.musicOn && audioCurrentTrack() == TUNE_TITLE, "music stays on when screen active & awake");

  screenOff = true;
  loop();
  check(audioCurrentTrack() == 0xFF && !pet.musicOn, "screenOff stops jukebox music");
  screenOff = false;

  audioPlayTrack(TUNE_PALLET);
  pet.sleeping = true;
  loop();
  check(audioCurrentTrack() == 0xFF && !pet.musicOn, "sleeping stops jukebox music");
  pet.sleeping = false;

  // --- Listening joy reward, daily cap, muted exclusion, next-day reset ---
  audioSetEnabled(true);
  audioSetVolume(5);
  audioPlayTrack(TUNE_PALLET);
  pet.musicOn = true;
  pet.joy = 40;
  for (int m = 0; m < LISTEN_EVERY; m++) {
    pet.fullness = 100;
    pet.hygiene = 100;
    pet.poops = 0;
    pet.dbgTick();
  }
  check(pet.listenMin == LISTEN_EVERY, "listenMin advances by 1 per awake active minute");
  check(pet.joy == 40 - LISTEN_EVERY + LISTEN_JOY,
        "LISTEN_EVERY minutes grants LISTEN_JOY");

  // Drive past LISTEN_DAILY_MIN to hit the daily cap
  for (int m = LISTEN_EVERY; m < LISTEN_DAILY_MIN + 15; m++) {
    pet.fullness = 100;
    pet.hygiene = 100;
    pet.poops = 0;
    pet.joy = 30;
    pet.dbgTick();
  }
  pet.joy = 30;
  for (int m = 0; m < LISTEN_EVERY; m++) {
    pet.fullness = 100;
    pet.hygiene = 100;
    pet.poops = 0;
    pet.dbgTick();
  }
  check(pet.listenMin == LISTEN_DAILY_MIN, "listenMin clamps at LISTEN_DAILY_MIN");
  check(pet.joy == 30 - LISTEN_EVERY, "no listening joy past daily cap");

  // Next day resets daily counter, but muted / disabled stops listenTick from crediting
  pet.dbgSetSeen(1700000000UL + 86400UL);
  audioSetEnabled(false);
  loop();
  check(!pet.musicOn, "loop clears pet.musicOn when audio disabled");
  pet.fullness = 100;
  pet.hygiene = 100;
  pet.poops = 0;
  pet.dbgTick();
  check(pet.listenToday() == 0, "disabled audio does not accrue listenMin");

  audioSetEnabled(true);
  audioSetVolume(0);
  audioPlayTrack(TUNE_PALLET);
  loop();
  check(!pet.musicOn, "loop clears pet.musicOn at volume 0");
  pet.fullness = 100;
  pet.hygiene = 100;
  pet.poops = 0;
  pet.dbgTick();
  check(pet.listenToday() == 0, "volume 0 does not accrue listenMin");

  audioSetVolume(5);
  audioPlayTrack(TUNE_PALLET);
  loop();
  check(pet.musicOn, "loop marks pet.musicOn when audible");
  pet.joy = 20;
  for (int m = 0; m < LISTEN_EVERY; m++) {
    pet.fullness = 100;
    pet.hygiene = 100;
    pet.poops = 0;
    pet.dbgTick();
  }
  check(pet.listenMin == LISTEN_EVERY && pet.joy == 20 - LISTEN_EVERY + LISTEN_JOY,
        "new day restores daily listening joy allowance");
  audioMusic(MUS_NONE);

  // --- Unlock rules ---
  pet.noteCaught(19, false);
  check((pet.checkTunes(true) & (1u << TUNE_ROUTE1)) != 0, "catching wild Pokemon unlocks Route 1");
  check(pet.tuneToast() && pet.tuneToastId == TUNE_ROUTE1, "live unlock raises NEW SONG toast");

  pet.winBadge(0, 0, false);
  uint16_t b1 = pet.checkTunes(true);
  check((b1 & (1u << TUNE_GYM)) && (b1 & (1u << TUNE_WIN)),
        "1st badge unlocks Gym Battle and Victory");

  pet.winBadge(0, 1, false);
  pet.winBadge(0, 2, false);
  pet.winBadge(0, 3, false);
  check((pet.checkTunes(true) & (1u << TUNE_TRN)) != 0, "4th badge unlocks Trainer Battle");

  for (uint8_t b = 4; b < 8; b++) pet.winBadge(0, b, false);
  check((pet.checkTunes(true) & (1u << TUNE_WILD)) != 0, "8th badge unlocks Wild Battle");

  pet.bestStreak = 3;
  check((pet.checkTunes(true) & (1u << TUNE_CENTER)) != 0, "3-day streak unlocks Pokemon Center");

  pet.lastEnd = CER_FAREWELL;
  check((pet.checkTunes(true) & (1u << TUNE_LAVENDER)) != 0, "farewell unlocks Lavender Town");

  // --- Persistence across newEgg() and fresh Pet boot ---
  uint16_t unlockedMask = pet.tunes;
  uint8_t savedListenMin = pet.listenMin;
  uint32_t savedListenDay = pet.listenDay;
  pet.saveNow();

  pet.newEgg();
  check(pet.tunes == unlockedMask, "newEgg preserves player tunes bitmask");
  check(pet.listenMin == savedListenMin && pet.listenDay == savedListenDay,
        "newEgg preserves daily listening count");

  Pet reloaded;
  reloaded.begin();
  check(reloaded.tunes == unlockedMask, "plyA/plyB checkpoint restores tunes bitmask");
  check(reloaded.listenMin == savedListenMin && reloaded.listenDay == savedListenDay,
        "plyA/plyB checkpoint restores listenMin/listenDay");

  if (fails) {
    printf("jukebox_test: %d failures\n", fails);
    return 1;
  }
  printf("jukebox_test PASS\n");
  return 0;
}
