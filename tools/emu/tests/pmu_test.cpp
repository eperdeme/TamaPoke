// A failed I2C read of the PMU's IRQ status must not latch any interrupt.
//
// XPowersLib's getIrqStatus() stores readRegister()'s int straight into a
// uint8_t array, and readRegister() returns -1 on I2C failure -- landing as
// 0xFF (every IRQ bit set, including PWR short press, long press, and
// low-battery warnings). One glitch on the bus shared with the touch controller
// toggled screenOff and triggered flushBeforePowerLoss().
//
// This compiles the REAL rtcbat.cpp against stand-ins in tools/emu/pmu/ that
// reproduce that XPowersLib behaviour.
#include "Arduino.h"
#include "XPowersLib.h"
#include "rtcbat.h"
#include <cstdio>

uint32_t g_seed = 5;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
uint32_t millis() { static uint32_t t = 0; return t += 3000; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

static int bad = 0;
static void ck(bool ok, const char *w) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", w);
  if (!ok) bad++;
}

int main() {
  ck(batBegin(), "the PMU comes up");
  pwrSetup();

  // Positive controls: real interrupts are seen once and acknowledged.
  g_pmuStatus[1] = 0x08;  // PKEY short press
  pwrPoll();
  ck(pwrShortPressed() && !pwrLongPressed() && !batLowWarning(),
     "a real short press is reported");
  ck(g_pmuStatus[1] == 0, "and acknowledged (status cleared)");
  ck(!pwrShortPressed(), "consumed once only");

  g_pmuStatus[1] = 0x04;  // PKEY long press
  pwrPoll();
  ck(!pwrShortPressed() && pwrLongPressed() && !batLowWarning(),
     "a real long press is reported");

  g_pmuStatus[0] = 0x02;  // battery low warning level 1
  pwrPoll();
  ck(!pwrShortPressed() && !pwrLongPressed() && batLowWarning(),
     "a real low-battery warning is reported");

  // Full I2C failure across all three status registers -> 0xFFFFFF.
  uint32_t errs0 = pmuReadErrors();
  int clears0 = g_pmuClears;
  g_pmuFailNext = 3;
  pwrPoll();
  ck(!pwrShortPressed() && !pwrLongPressed() && !batLowWarning(),
     "a failed I2C read latches no IRQ flags");
  ck(pmuReadErrors() == errs0 + 1, "and increments pmuReadErrors()");
  ck(g_pmuClears == clears0, "and does not clear the chip's IRQ registers");

  // Partial failure mid-burst (registers 0 and 1 fail, register 2 reads).
  g_pmuFailNext = 2;
  pwrPoll();
  ck(!pwrShortPressed() && !pwrLongPressed() && !batLowWarning(),
     "a partly failed read latches no IRQ flags either");
  ck(pmuReadErrors() == errs0 + 2, "and increments pmuReadErrors() again");
  ck(g_pmuClears == clears0, "still without clearing registers");

  // Failure on only the third byte (register 2 = 0xFF).
  g_pmuStatus[2] = 0xFF;
  pwrPoll();
  ck(!pwrShortPressed() && !pwrLongPressed() && !batLowWarning(),
     "0xFF in the third status byte is also rejected");
  ck(pmuReadErrors() == errs0 + 3, "and counted");
  ck(g_pmuClears == clears0, "without clearing registers");

  // Recovery: a real press after failed reads still gets through.
  g_pmuStatus[2] = 0;
  g_pmuStatus[1] = 0x08;
  pwrPoll();
  ck(pwrShortPressed(), "a real press after failed reads still gets through");

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
