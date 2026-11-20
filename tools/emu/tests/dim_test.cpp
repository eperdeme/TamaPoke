// Boot darkness and touch-dimming regression test (issue #21).
//
// 1. setup() must keep the panel dark with a cleared black framebuffer until
//    loop() renders and flushes the first real frame, avoiding boot-time PSRAM
//    garbage.
// 2. Touching the screen must never dim it. loop() captures `now = millis()`
//    before handleTouch() stamps `lastInteract = millis()` after a ~1-2 ms I2C
//    touch read. Unsigned `now - lastInteract` wrapped negative deltas to
//    ~4.29e9, dropping dimStage to 2 (brightness 8) on every press and release.
//    g_touchReadUs models that I2C latency so the regression is reproducible.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <chrono>
#include <thread>

uint32_t g_seed = 31337;
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
void updateBrightness(uint32_t now);
extern Pet pet;
extern Arduino_CO5300 *panel;
extern Arduino_Canvas *gfx;
extern uint8_t dimStage;
extern uint32_t lastInteract;

static int bad = 0;
static void ck(bool ok, const char *w) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", w);
  if (!ok) bad++;
}

static uint8_t lowest = 255;
static void pump(int n) {
  for (int i = 0; i < n; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    loop();
    if (panel->brightness < lowest) lowest = panel->brightness;
  }
}

int main() {
  setup();
  ck(panel->brightness == 0, "after setup() the panel is still dark");
  bool black = true;
  for (uint16_t px : gfx->fb) if (px) { black = false; break; }
  ck(black, "and the framebuffer holds a black frame, not PSRAM garbage");

  gfx->frameReady = false;
  loop();
  ck(gfx->frameReady, "the first loop() flushes a frame immediately");
  bool drawn = false;
  for (uint16_t px : gfx->fb) if (px) { drawn = true; break; }
  ck(drawn, "with real rendered content");
  ck(panel->brightness > 0 && dimStage == 0, "and lights the panel on that same pass");

  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6, false);
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.sleeping = false;
  g_touchReadUs = 2000;  // models the CST9217 I2C point read latency
  pump(5);
  uint8_t awake = panel->brightness;
  ck(awake >= 100 && dimStage == 0, "the panel stays awake and bright");

  // Press and release repeatedly while getPoint() advances millis() past loop()'s `now`.
  lowest = 255;
  for (int k = 0; k < 8; k++) {
    g_touchX = 60; g_touchY = 220; g_touchDown = true; emuFireInterrupt(); pump(3);
    g_touchDown = false; emuFireInterrupt(); pump(3);
  }
  printf("     brightness awake=%u, lowest while being touched=%u\n", awake, lowest);
  ck(lowest == awake, "tapping never dips the panel below its awake brightness");
  ck(dimStage == 0, "and leaves dimStage at 0");

  // Confirm the dimming ladder itself still works.
  lastInteract = millis() - 100000;
  pump(2);
  ck(dimStage == 1 && panel->brightness < awake, "100 s idle still enters dimStage 1");

  lastInteract = millis() - 310000;
  pump(2);
  ck(dimStage == 2 && panel->brightness == 8, "310 s idle still enters dimStage 2");

  // Direct clamp & 32-bit millis() rollover checks.
  lastInteract = 12;
  updateBrightness(10);
  ck(dimStage == 0 && panel->brightness == awake, "lastInteract slightly ahead of now clamps to 0");

  lastInteract = 0xFFFFFFF0u;
  updateBrightness(50);
  ck(dimStage == 0 && panel->brightness == awake, "millis() 32-bit rollover stays undimmed");

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
