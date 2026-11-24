// What walking pays, plus the in-game step counter toggle ("stepen").
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "steps.h"
#include <cstdio>

uint32_t g_seed = 19;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
static uint32_t g_ms = 1000;
uint32_t millis() { return g_ms; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *w) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", w);
  if (!ok) bad++;
}

static const uint32_t DAY = 86400, T0 = 1900000000UL - (1900000000UL % 86400);

static void hatched(Pet &p, uint32_t day) {
  stepCounterSet(true);
  p.begin();
  p.dbgHatchAs(25, false);
  p.ivAtk = p.ivDef = p.ivSpe = p.ivHp = 31;
  p.trAtk = p.trDef = p.trSpe = 0;
  p.stepClaim = 0;
  p.stepCount = 0;
  p.stepDay = 0;
  p.setClock(T0 + day * DAY + 3600);
}

int main() {
  // --- Default enabled state ---
  nvs().clear();
  stepCounterLoad();
  ck(gStepCounterEnabled, "step counter defaults to enabled on a fresh device");

  // --- below the first milestone nothing pays ---
  {
    Pet p; hatched(p, 1);
    p.fullness = p.joy = 40;
    p.setStepsToday(STEP_MS[0] - 1);
    ck(p.claimSteps() == 0 && p.fullness == 40 && p.joy == 40,
       "one step short of the first milestone pays nothing");
  }

  // --- the first one: a snack, once ---
  {
    Pet p; hatched(p, 1);
    p.fullness = p.joy = 40;
    p.setStepsToday(STEP_MS[0]);
    ck(p.claimSteps() == 1, "reaching the first milestone pays it");
    ck(p.fullness == 55 && p.joy == 50, "a snack: +15 food, +10 joy");
    ck(p.stepToast() && p.stepToastSteps == STEP_MS[0], "and the toast says which milestone");
    p.fullness = p.joy = 40;
    ck(p.claimSteps() == 0 && p.fullness == 40, "asked again it pays nothing: once a day");
  }

  // --- jumping past several pays all of them, and training lands at the goal ---
  {
    Pet p; hatched(p, 1);
    p.fullness = p.joy = p.energy = 20;
    p.setStepsToday(STEP_GOAL);
    uint8_t top = p.claimSteps();
    ck(top == STEP_MS_COUNT, "reaching the goal in one go pays every milestone it passed");
    ck(p.trAtk + p.trDef + p.trSpe == 3, "the goal trains 3 points into one of the three stats");
    ck(p.energy == 30, "the second milestone's energy was paid too");
  }

  // --- the IV ceiling is never crossed ---
  {
    Pet p; hatched(p, 1);
    p.ivAtk = p.ivDef = p.ivSpe = 0;
    p.trAtk = p.trMaxAtk(); p.trDef = p.trMaxDef(); p.trSpe = p.trMaxSpe();
    uint16_t before = p.trAtk + p.trDef + p.trSpe;
    p.setStepsToday(STEP_GOAL);
    p.claimSteps();
    ck(p.trAtk + p.trDef + p.trSpe == before, "a fully trained creature gains nothing from the goal");
    ck(p.joy >= 30 || p.joy == 100, "but still gets the joy");
  }
  {
    Pet p; hatched(p, 1);
    p.ivAtk = p.ivDef = p.ivSpe = 0;
    p.trAtk = p.trMaxAtk(); p.trDef = p.trMaxDef(); p.trSpe = p.trMaxSpe() - 1;
    p.setStepsToday(STEP_GOAL);
    p.claimSteps();
    ck(p.trSpe == p.trMaxSpe() && p.trAtk == p.trMaxAtk() && p.trDef == p.trMaxDef(),
       "with one point of headroom it fills exactly that, and no stat passes its ceiling");
  }

  // --- a new day pays again ---
  {
    Pet p; hatched(p, 1);
    p.setStepsToday(STEP_MS[0]);
    p.claimSteps();
    p.setClock(T0 + 2 * DAY + 3600);
    p.addSteps(STEP_MS[0]);
    ck(p.stepsToday() == STEP_MS[0] && p.claimSteps() == 1,
       "the next day the same milestone pays again");
  }

  // --- the claim survives a reboot: a restart must not pay a second time ---
  {
    Pet p; hatched(p, 3);
    p.fullness = p.joy = 40;
    p.setStepsToday(STEP_MS[1]);
    p.claimSteps();
    p.saveNow();
    Pet q; q.begin();
    q.setClock(T0 + 3 * DAY + 7200);
    q.fullness = q.joy = 40;
    ck(q.stepsToday() == STEP_MS[1], "the steps came back");
    ck(q.claimSteps() == 0 && q.joy == 40, "and so did the claim: a reboot pays nothing twice");
  }

  // --- nothing pays while an egg waits, and it pays once the pet hatches ---
  {
    Pet p; hatched(p, 4);
    p.newEgg();
    p.setClock(T0 + 4 * DAY + 3600);
    p.setStepsToday(STEP_MS[0]);
    ck(p.isEgg() && p.claimSteps() == 0, "an egg earns nothing from walking");
    p.dbgHatchAs(25, false);
    p.setClock(T0 + 4 * DAY + 3700);
    ck(p.claimSteps() == 1, "the same steps pay once something has hatched");
  }

  // --- in-game toggle: disabling pauses rewards while retaining counts across reboots ---
  {
    Pet p; hatched(p, 5);
    p.fullness = p.joy = 40;
    p.setStepsToday(STEP_MS[0]);
    stepCounterSet(false);
    gStepCounterEnabled = true;
    stepCounterLoad();
    ck(!gStepCounterEnabled, "stepen=0 survives reload into stepCounterLoad()");
    ck(p.claimSteps() == 0 && p.fullness == 40, "disabled step counter suppresses milestone rewards");
    ck(p.stepsToday() == STEP_MS[0], "existing step total stays intact when toggled off");

    stepCounterSet(true);
    stepCounterLoad();
    ck(gStepCounterEnabled, "re-enabling persists stepen=1");
    ck(p.claimSteps() == 1 && p.fullness == 55, "re-enabling lets unclaimed milestones pay");
  }

  printf(bad ? "FAILED %d\n" : "all good\n", bad);
  return bad ? 1 : 0;
}
