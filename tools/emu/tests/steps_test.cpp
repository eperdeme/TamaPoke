// Step counting: the detector against synthetic gait, and the Pet's day rule.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "steps.h"
#include "power.h"
#include <cstdio>
#include <cmath>

uint32_t g_seed = 9;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
static uint32_t g_ms = 0;
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

// a walk: gravity on `axis` plus a sine at `hz`, sampled every `dtMs` (with jitter)
static int walk(StepDetector &d, float hz, float amp, int dtMs, int seconds, int axis,
                uint32_t t0 = 1000, float noise = 0.015f, int jitterMs = 0) {
  int steps = 0;
  uint32_t t = t0;
  uint32_t end = t0 + seconds * 1000;
  while (t < end) {
    float s = amp * sinf(2.0f * 3.14159265f * hz * (t - t0) / 1000.0f);
    float n = ((rand() % 2001) - 1000) / 1000.0f * noise;
    float a[3] = { 0, 0, 0 };
    a[axis] = 1.0f + s + n;
    a[(axis + 1) % 3] = n * 0.5f;
    steps += d.feed(a[0], a[1], a[2], t);
    t += dtMs + (jitterMs ? (rand() % (2 * jitterMs + 1)) - jitterMs : 0);
  }
  return steps;
}

int main() {
  srand(7);

  // 1. a real walk is counted across cadences, sample rates, and orientations
  struct { float hz; int dt; int axis; } cases[] = {
    {1.6f, 16, 2}, {2.0f, 16, 2}, {2.5f, 16, 2},
    {2.5f, POWER_STEP_SLEEP_MS, 2},
    {2.0f, 50, 2}, {2.0f, 100, 2},
    {2.0f, 16, 0}, {2.0f, 16, 1},
  };
  for (auto &c : cases) {
    StepDetector d;
    int got = walk(d, c.hz, 0.25f, c.dt, 30, c.axis, 1000, 0.015f, c.dt > 40 ? 15 : 0);
    int want = (int)(30 * c.hz);
    char w[96];
    snprintf(w, sizeof(w), "walk %.1f Hz, %d ms samples, axis %d: %d steps (want ~%d)", c.hz, c.dt, c.axis, got, want);
    ck(abs(got - want) <= 4, w);
  }

  // 2. a gentle walk still counts
  {
    StepDetector d;
    int got = walk(d, 1.8f, 0.16f, 20, 30, 2);
    ck(got >= 45, "a gentle walk (0.16 g) is still counted");
  }

  // 3. NOT counting: noise, slow rock, short 2-peak shake, rapid rattle
  { StepDetector d; int got = walk(d, 0.0f, 0.0f, 16, 60, 2, 1000, 0.03f);
    ck(got == 0, "sitting still, with sensor noise: 0 steps"); }
  { StepDetector d; int got = walk(d, 0.3f, 0.4f, 16, 60, 2);
    ck(got == 0, "slow rocking at 0.3 Hz: 0 steps"); }
  { StepDetector d; int got = walk(d, 2.0f, 0.4f, 16, 1, 2);
    ck(got == 0, "a shake of two peaks: 0 steps"); }
  { StepDetector d; int got = walk(d, 8.0f, 0.5f, 16, 20, 2);
    ck(got == 0, "rattling at 8 Hz: 0 steps"); }
  { StepDetector d; int got = walk(d, 2.0f, 0.4f, 16, 3, 2);
    ck(got >= 4, "...and the same shake held for 3 s IS a walk (the run guard decides)"); }

  // 4. a hole in the data restarts the filters instead of inventing a step
  {
    StepDetector d;
    walk(d, 2.0f, 0.25f, 16, 10, 2, 1000);
    int after = d.feed(0.0f, 0.0f, 3.0f, 60000);
    ck(after == 0, "a sample after a long gap counts nothing");
  }

  // 5. Pet: the total belongs to a day and persists cleanly
  const uint32_t DAY = 86400, T0 = 1767225600UL;
  {
    Pet p; p.begin();
    if (p.awaitingStarter()) p.chooseStarter(4);
    if (p.isEgg()) p.dbgHatchAs(4, false);
    p.setClock(T0 + 10 * 3600);
    p.addSteps(120);
    ck(p.stepsToday() == 120, "steps add up within a day");
    p.setClock(T0 + 23 * 3600 + 3599);
    p.addSteps(5);
    ck(p.stepsToday() == 125, "...up to the last second before midnight");
    p.setClock(T0 + DAY);
    ck(p.stepsToday() == 0, "midnight: the displayed total is 0");
    p.addSteps(7);
    ck(p.stepsToday() == 7, "the first steps after midnight start a new total");
    p.setClock(T0 + 3 * DAY + 100);
    ck(p.stepsToday() == 0, "the board off for days: still 0, not yesterday's");
    p.addSteps(3);
    ck(p.stepsToday() == 3, "...and counting again from 0");
  }
  {
    Pet p; p.begin();
    p.setClock(T0 + 5 * DAY + 3600);
    p.setStepsToday(4321);
    p.saveNow();
    Pet q; q.begin();
    q.setClock(T0 + 5 * DAY + 7200);
    ck(q.stepsToday() == 4321, "steps survive a save/load round trip");
    q.newEgg();
    ck(q.stepsToday() == 4321, "newEgg() leaves the player's steps alone");
    Pet r; r.begin();
    r.setClock(T0 + 6 * DAY + 60);
    ck(r.stepsToday() == 0, "a reload on the next day reads 0");
  }
  {
    Pet p; p.begin();
    p.setClock(T0 + 3600);
    p.setStepsToday(STEP_MAX - 2);
    p.addSteps(500);
    ck(p.stepsToday() == STEP_MAX, "the counter stops at five digits");
  }

  printf("%s\n", bad ? "FAILED" : "all ok");
  return bad ? 1 : 0;
}
