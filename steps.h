#pragma once
#include <stdint.h>

// Step counting from a 3-axis accelerometer.
//
// No Arduino dependency on purpose, like gbsynth: it takes samples and says how
// many steps they held, so steps_test can drive it with synthetic walking and
// the board feeds it from the QMI8658. The motion maths is the part worth
// testing; the I2C read is not.
//
// The signal is the magnitude of the acceleration, so it does not matter how the
// board is held, pocketed or strapped. Gravity is removed with a slow average,
// the rest is smoothed, and a step is a rise through a threshold after the
// signal has first dropped back. Four steps in a row at a walking cadence must
// be seen before any of them count: a shake, a bump or a fidget makes one or two
// peaks, never a run of four.

#define STEP_GOAL 10000        // daily target for step progress bar and top reward
#define STEP_MAX  99999        // counter ceiling; five digits fit the step pill
// The day's milestones: each pays the pet once a day, the last is the goal.
#define STEP_MS_COUNT 3
static const uint16_t STEP_MS[STEP_MS_COUNT] = { 2500, 5000, STEP_GOAL };

class StepDetector {
 public:
  // One sample: acceleration in g on each axis and the time it was taken, in ms.
  // Returns how many steps to add (usually 0, 1 once walking, 4 when a run is
  // first confirmed). Irregular timing is fine; a gap over half a second
  // restarts the filters.
  uint8_t feed(float ax, float ay, float az, uint32_t ms);
  void reset();

 private:
  bool primed = false;
  uint32_t lastMs = 0;
  float grav = 1.0f;           // slow average of the magnitude: what gravity looks like now
  float sig = 0.0f;            // smoothed motion with gravity taken off
  bool armed = false;          // the signal has dropped low enough for a new step to count
  uint32_t lastPeakMs = 0;     // the last peak that was accepted as a footfall
  uint32_t lastAnyMs = 0;      // the last peak of any kind, accepted or not
  uint8_t run = 0;             // consecutive well-spaced peaks
};
