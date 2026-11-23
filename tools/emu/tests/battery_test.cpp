// Battery curve + shutdown monitor (batcurve.h) and the power policy (power.h).
// Both are pure logic; ported from ViezeVingertjes' socquique/TamaPoke PRs #26/#27.
#include "batcurve.h"
#include "power.h"
#include <cstdio>

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

static bool lowFor(BatMonitor &m, int mv, uint32_t t0, uint32_t ms) {
  bool r = false;
  for (uint32_t t = t0; t <= t0 + ms; t += 2000) { m.sample(mv, t); r = m.shutdownNeeded(t); }
  return r;
}
static PowerCtx ctx(uint32_t idle) { PowerCtx c = {}; c.idleMs = idle; c.batPct = 80; return c; }

int main() {
  ck(batPercentFromMv(4300) == 100 && batPercentFromMv(BAT_CUTOFF_MV) == 0 && batPercentFromMv(3840) == 50,
     "curve endpoints");
  int prev = 100; bool mono = true;
  for (int mv = 4200; mv >= BAT_CUTOFF_MV; mv--) { int p = batPercentFromMv(mv); if (p > prev) mono = false; prev = p; }
  ck(mono, "curve is monotonic");
  { BatMonitor m; ck(!lowFor(m, 3250, 100000, 20000), "20 s low does not shut down");
    ck(lowFor(m, 3250, 120000, 12000), "30+ s low shuts down"); }
  { BatMonitor m; for (uint32_t t = 100000; t < 130000; t += 2000) m.sample(3700, t);
    ck(!lowFor(m, 3200, 130000, 20000), "a load sag does not shut down");
    for (uint32_t t = 152000; t <= 158000; t += 2000) m.sample(3700, t);
    ck(!m.shutdownNeeded(160000), "and recovery clears the count"); }
  { BatMonitor m; for (uint32_t t = 0; t <= 20000; t += 2000) m.sample(3200, t);
    ck(!m.shutdownNeeded(BAT_BOOT_GRACE_MS - 1), "no shutdown inside the boot grace");
    for (uint32_t t = 22000; t <= 60000; t += 2000) m.sample(3200, t);
    ck(m.shutdownNeeded(60000), "shutdown after grace"); }
  { BatMonitor m; m.seed(60); int a = m.sample(4100, 1000);
    ck(a == 60, "inflated voltage cannot raise the seeded percent");
    for (uint32_t t = 3000; t < 23000; t += 2000) a = m.sample(3700, t);
    ck(a < 60 && m.sample(4000, 30000) == a, "percent only falls while discharging"); }

  ck(dimStageFor(90000) == DIM_AWAKE && dimStageFor(90001) == DIM_LOW && dimStageFor(300001) == DIM_OFF,
     "dim ladder unchanged");
  ck(idleSince(1000, 1002) == 0 && idleSince(5, 0xFFFFFFF0u) == 21, "idleSince tolerates a touch stamped after the clock read");
  { PowerCtx c = ctx(0); c.batPct = 15; ck(brightnessFor(c) == 100, "low battery caps brightness");
    c.batPct = 5; ck(brightnessFor(c) == 60, "critical battery caps harder");
    c.charging = true; ck(brightnessFor(c) == 145, "charging lifts the cap"); }
  ck(frameIntervalMs(ctx(0)) == 100 && frameIntervalMs(ctx(70000)) == 150 && frameIntervalMs(ctx(400000)) == 1000,
     "frame interval grows with idleness");
  { PowerCtx g = ctx(200000); g.gameOpen = true; ck(frameIntervalMs(g) == 85, "games keep 85 ms"); }
  { PowerCtx c = ctx(1000); ck(amplifierWanted(c), "amp wanted when active");
    c.sleeping = true; ck(!amplifierWanted(c), "amp off while asleep"); }
  { PowerCtx c = ctx(0); ck(!lightSleepAllowed(c), "no light sleep in use");
    c.idleMs = 400000; ck(lightSleepAllowed(c), "light sleep when nearly off");
    c.usb = true; ck(!lightSleepAllowed(c), "never on USB"); }
  printf("%s\n", bad ? "FAILURES" : "battery and power policy hold");
  return bad ? 1 : 0;
}
