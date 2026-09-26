// WHAT A CLOCK READING IS WORTH.
//
// The RTC is a physical part with its own battery, and offline progression
// multiplies whatever it says by a whole life's worth of decay -- so a bad
// reading is not a cosmetic problem, it ages a creature days it never lived and
// can carry it past the farewell threshold in one boot.
//
// Three faults were live before clockVerdict() existed, and each has a case
// here. The FOURTH case matters just as much and is the reason this test cannot
// simply assert "refuse odd-looking readings": a genuine multi-day absence must
// still be applied in full, or the gate has quietly disabled the feature it was
// meant to protect.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>

uint32_t g_seed=5; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// A hatched creature with a clock reference already set, which is the state every
// case below starts from.
static void freshPet(Pet &p, uint32_t seen) {
  p.begin();
  if (p.awaitingStarter()) p.chooseStarter(4);
  p.dbgHatchAs(6, false);
  while (p.hasLearnOffer()) p.declineLearn();
  p.ageMinutes = 20UL * MINUTES_PER_LEVEL;
  p.setClock(seen);
}

int main(){
  const uint32_t base = CLOCK_EPOCH_FLOOR + 900UL * 86400;   // a plausible "now"

  // ---- the verdict itself, in isolation
  {
    uint32_t mins = 999;
    ck(clockVerdict(base, 0, &mins) == CLK_REJECT && mins == 0,
       "a zero reading is refused outright -- the RTC could not be read");
    ck(clockVerdict(base, CLOCK_EPOCH_FLOOR - 1, &mins) == CLK_REJECT,
       "and so is anything older than the firmware that could have written it");
    ck(clockVerdict(base, base - 40UL * 86400, &mins) == CLK_REBASE && mins == 0,
       "a clock that went BACKWARDS is rebased, never measured");
    ck(clockVerdict(0, base, &mins) == CLK_REBASE && mins == 0,
       "the first ever reading is adopted and ages nothing");
    ck(clockVerdict(base, base + 30, &mins) == CLK_REBASE,
       "half a minute is not an absence");
    ck(clockVerdict(base, base + 60UL * 86400, &mins) == CLK_REBASE && mins == 0,
       "a jump of sixty days is a fault, and is NOT clamped-then-applied");
    ck(clockVerdict(base, base + 3UL * 86400, &mins) == CLK_APPLY &&
       mins == 3UL * 24 * 60,
       "a real three-day absence applies in full");
    ck(clockVerdict(base, base + CLOCK_MAX_OFFLINE_MINS * 60, &mins) == CLK_APPLY &&
       mins == CLOCK_MAX_OFFLINE_MINS,
       "and the longest plausible absence is still applied, right at the edge");
    ck(clockVerdict(base, base + CLOCK_MAX_OFFLINE_MINS * 60 + 60, &mins) == CLK_REBASE,
       "one minute past it is not");
  }

  // ---- and through syncClock(), which is what the boot actually calls
  //
  // The isolated verdict proves the rule; these prove the CALLER obeys it.
  // CLAUDE.md is explicit that a rule proven only in isolation says nothing
  // about its caller.
  {
    Pet p; freshPet(p, base);
    const uint32_t ageWas = p.ageMinutes;
    p.syncClock(0);
    ck(p.lastSeenEpoch == base,
       "an unreadable RTC does not destroy the reference it could not replace");
    ck(p.ageMinutes == ageWas, "and ages nothing");
    // The old code assigned the zero first, so the NEXT good reading measured
    // from 1970 and came out as a jump of half a century.
    p.syncClock(base + 2UL * 86400);
    ck(p.ageMinutes == ageWas + 2UL * 24 * 60,
       "so the next good reading measures a real two days, not fifty years");
  }
  {
    Pet p; freshPet(p, base);
    const uint32_t ageWas = p.ageMinutes;
    p.syncClock(base + 400UL * 86400);      // the RTC lost its mind
    ck(p.ageMinutes == ageWas,
       "an implausible jump ages the creature by nothing at all");
    ck(p.lastSeenEpoch == base + 400UL * 86400,
       "but the reading is adopted, so the absence is not re-measured forever");
  }
  {
    Pet p; freshPet(p, base);
    const uint32_t ageWas = p.ageMinutes;
    p.syncClock(base - 10UL * 86400);       // backwards
    ck(p.ageMinutes == ageWas, "a backward jump ages nothing");
  }
  {
    // THE ONE THAT PROVES THE GATE DID NOT JUST TURN OFF OFFLINE PROGRESSION.
    Pet p; freshPet(p, base);
    const uint32_t ageWas = p.ageMinutes;
    p.fullness = 90; p.joy = 90;
    p.syncClock(base + 6UL * 3600);         // six hours out
    ck(p.ageMinutes == ageWas + 6UL * 60, "six hours away still ages six hours");
    ck(p.fullness < 90 && p.joy < 90, "and the bars still fell while it waited");
  }
  {
    // setClock() is the explicit path -- the player typing a time, or a host
    // sending TIME. It does not judge, but it must not adopt a non-time either,
    // because that is pure loss: the old reference goes and nothing replaces it.
    Pet p; freshPet(p, base);
    p.setClock(0);
    ck(p.lastSeenEpoch == base, "setClock(0) keeps the reference it had");
    p.setClock(base + 99);
    ck(p.lastSeenEpoch == base + 99, "and an explicit time is taken as given");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
