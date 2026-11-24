#include "steps.h"
#include <math.h>

// Tuned against synthetic gait in steps_test, NOT against a person: a real
// walk wants a pass on the board. The numbers that matter are the first two.
static const float RISE_G = 0.10f;        // peak height that counts, in g above gravity
static const float FALL_G = 0.04f;        // how far it must drop back before the next one can
static const uint32_t MIN_GAP_MS = 250;   // faster than 4 steps/s is not a walk
static const uint32_t RATTLE_MS = 150;    // peaks this close are a vibration, not feet
static const uint32_t MAX_GAP_MS = 1200;  // slower than this and the run is broken
static const uint8_t RUN_TO_COUNT = 4;    // peaks in a row before any are believed
static const float GRAV_TAU_MS = 1500.0f;
static const float SMOOTH_TAU_MS = 40.0f;

void StepDetector::reset() {
  primed = false;
  run = 0;
  armed = false;
  lastPeakMs = 0;
  lastAnyMs = 0;
}

uint8_t StepDetector::feed(float ax, float ay, float az, uint32_t ms) {
  float m = sqrtf(ax * ax + ay * ay + az * az);
  if (!primed || ms - lastMs > 500) {      // first sample, or a hole in the data
    primed = true;
    lastMs = ms;
    grav = m;
    sig = 0.0f;
    armed = false;
    run = 0;
    return 0;
  }
  float dt = (float)(ms - lastMs);
  if (dt <= 0.0f) return 0;
  lastMs = ms;

  grav += (m - grav) * (dt / (GRAV_TAU_MS + dt));
  float d = m - grav;
  sig += (d - sig) * (dt / (SMOOTH_TAU_MS + dt));

  if (sig < FALL_G) armed = true;
  if (!(armed && sig > RISE_G)) return 0;

  armed = false;
  // A peak right on the heels of the last one, counted or not, is a rattle.
  // Dropping it silently was not enough: every second peak of an 8 Hz buzz
  // landed 250 ms after the one before and passed as a 4 Hz walk.
  uint32_t sinceAny = ms - lastAnyMs;
  bool rattle = lastAnyMs && sinceAny < RATTLE_MS;
  lastAnyMs = ms;
  if (rattle) {
    run = 0;
    return 0;
  }
  uint32_t gap = ms - lastPeakMs;
  if (lastPeakMs && gap < MIN_GAP_MS) return 0;   // a ripple on the same footfall, not a new one
  lastPeakMs = ms;
  if (gap > MAX_GAP_MS || run == 0) {
    run = 1;
    return 0;
  }
  if (run < RUN_TO_COUNT) {
    run++;
    return run == RUN_TO_COUNT ? RUN_TO_COUNT : 0;   // confirmed: hand back the ones held back
  }
  return 1;
}
