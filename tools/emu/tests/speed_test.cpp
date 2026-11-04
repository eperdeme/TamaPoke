// The level-speed setting (SETTINGS page 2: real minutes per level).
//
// Age is always stored at 60 game minutes per level; the setting only decides how
// many game minutes one real minute adds. What that must guarantee:
// 1. Each speed levels up exactly as fast as it says, live and offline.
// 2. Switching speed never moves the current level.
// 3. Only growth speeds up: the needs drain, the egg and the farewell rule
//    behave as in the original game, in real minutes / in levels.
// 4. The choice persists, junk reads as the original 60, and it cycles in order.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>

#ifndef TAMA_NATIVE_TEST
uint32_t g_seed=11; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
#endif
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// Each Pet keeps its own tick reference, taken from millis() in begin() -- which
// is a real, running clock on the native board -- so each gets its own clock,
// started just after begin(): every +60 s then lands exactly one tick.
struct Clocked { Pet p; uint32_t now = 0; };
static void run(Clocked &c, int realMinutes) {
  for (int i = 0; i < realMinutes; i++) {
    c.now += PET_TICK_MS;
    c.p.update(c.now);
  }
}
static void hatched(Clocked &c, int16_t dex) {
  c.p.begin();
  if (c.p.awaitingStarter()) c.p.chooseStarter(4);
  c.p.dbgHatchAs(dex, false);
  while (c.p.hasLearnOffer()) c.p.declineLearn();
  c.p.ageMinutes = 0;
  c.p.sleeping = false;
  c.now = millis();
}

int main(){
  // --- the settings themselves ---------------------------------------------
  levelSpeedLoad();
  farewellLevelLoad();
  ck(gLevelMins == 60 && gFarewellLevel == 73,
     "a save with no settings plays the original game: 60 min per level, farewell at 73");
  levelSpeedSet(0);   ck(gLevelMins == 1, "the speed stops at 1, it does not wrap");
  levelSpeedSet(75);  ck(gLevelMins == 60, "and at 60");
  farewellLevelSet(10);  ck(gFarewellLevel == 50, "the farewell level stops at 50");
  farewellLevelSet(140); ck(gFarewellLevel == 100, "and at 100");
  levelSpeedSet(7); farewellLevelSet(61);
  gLevelMins = 60; gFarewellLevel = 73;
  levelSpeedLoad(); farewellLevelLoad();
  ck(gLevelMins == 7 && gFarewellLevel == 61, "both survive a reboot");
  {
    Preferences pr; pr.begin("tamapoke", false);
    pr.putUChar("lvspd", 0); pr.putUChar("fwlv", 200); pr.end();
    levelSpeedLoad(); farewellLevelLoad();
    ck(gLevelMins == 60 && gFarewellLevel == 73, "junk stored values read as the defaults");
  }

  // --- EVERY speed from 1 to 60 levels exactly, with no drift ----------------
  {
    bool exact = true;
    for (int mpl = 1; mpl <= 60 && exact; mpl++) {
      levelSpeedSet(mpl);
      Clocked c; hatched(c, 6);
      run(c, mpl * 5 - 1);
      const bool before = c.p.level() == 5;
      run(c, 1);
      if (!before || c.p.level() != 6) {
        printf("      %d min per level: level %u after %d min\n", mpl, c.p.level(), mpl * 5);
        exact = false;
      }
    }
    ck(exact, "at every speed from 1 to 60, five levels take exactly five times that many minutes");
  }
  {
    levelSpeedSet(7);
    Clocked c; hatched(c, 6);
    run(c, 3);
    ck(c.p.realMinutesToNextLevel() == 4, "the minutes to the next level are exact at a speed that does not divide 60");
    run(c, 7 * 90);
    ck(c.p.level() == 91 && c.p.realMinutesToNextLevel() == 4, "and still exact ninety levels later");
  }
  const uint8_t mins[]  = { 60, 30, 20, 15, 5, 1 };

  // --- each speed levels exactly as advertised ------------------------------
  for (int i = 0; i < 6; i++) {
    levelSpeedSet(mins[i]);
    Clocked c; hatched(c, 6);                     // CHARIZARD: nothing to evolve into
    run(c, mins[i] - 1);
    const bool notYet = c.p.level() == 1;
    run(c, 1);
    char w[80];
    snprintf(w, sizeof(w), "at %u min per level, level 2 arrives after exactly %u real minutes",
             (unsigned)mins[i], (unsigned)mins[i]);
    ck(notYet && c.p.level() == 2, w);
  }

  // --- switching mid-life keeps the level -----------------------------------
  {
    levelSpeedSet(1);
    Clocked c; hatched(c, 6);
    run(c, 10);
    const uint8_t l = c.p.level();
    levelSpeedSet(60);
    ck(c.p.level() == l, "slowing down does not move the level");
    run(c, 30);
    ck(c.p.level() == l, "and half an hour at 60 min/level adds nothing yet");
    levelSpeedSet(5);
    ck(c.p.level() == l, "speeding up again does not move it either");
    run(c, 5);
    ck(c.p.level() == l + 1, "the half-hour already earned counts toward the next level");
  }

  // --- only growth speeds up ------------------------------------------------
  {
    levelSpeedSet(60);
    Clocked a; hatched(a, 6);
    a.p.fullness = a.p.joy = a.p.energy = 100;
    levelSpeedSet(1);
    Clocked b; hatched(b, 6);
    b.p.fullness = b.p.joy = b.p.energy = 100;
    levelSpeedSet(60); run(a, 10);
    levelSpeedSet(1);  run(b, 10);
    ck(a.p.fullness == b.p.fullness && a.p.energy == b.p.energy,
       "hunger and energy drain at the same real-time rate at any speed");
    ck(b.p.level() == 11 && a.p.level() == 1, "while the level moves 60x faster");
  }
  {
    levelSpeedSet(1);
    Clocked c; c.p.begin();
    if (c.p.awaitingStarter()) c.p.chooseStarter(4);
    c.p.newEgg();
    c.now = millis();
    ck(c.p.isEgg(), "a fresh egg");
    run(c, 2);
    ck(c.p.isEgg(), "at the fastest speed the egg still waits its real 3 minutes");
    run(c, 1);
    ck(!c.p.isEgg(), "and hatches on the third");
    ck(c.p.level() == 1, "hatching at full speed starts at level 1, not 3");
  }
  {
    levelSpeedSet(1);
    Clocked c; hatched(c, 6);
    c.p.fullness = c.p.joy = c.p.energy = c.p.hygiene = 100;
    run(c, 71);
    c.p.sleeping = false;
    ck(!c.p.canFarewellNow(), "at 1 min per level the farewell is not offered at 71 minutes");
    run(c, 1);
    c.p.sleeping = false;
    ck(c.p.canFarewellNow(), "it is at 72: the same level-73 rule, in minutes");
  }

  {
    // 20 min per level is one life a day: the farewell exactly 24 h after hatching
    levelSpeedSet(20);
    Clocked c; hatched(c, 6);
    c.p.fullness = c.p.joy = c.p.energy = c.p.hygiene = 100;
    run(c, 24 * 60 - 1);
    c.p.sleeping = false;
    const bool notYet = !c.p.canFarewellNow();
    run(c, 1);
    c.p.sleeping = false;
    ck(notYet && c.p.canFarewellNow(), "at 20 min per level the farewell comes exactly 24 h after hatching");
  }

  {
    // the farewell level setting: 60 at 1 min per level -> offered after 59 minutes
    levelSpeedSet(1); farewellLevelSet(60);
    Clocked c; hatched(c, 6);
    c.p.fullness = c.p.joy = c.p.energy = c.p.hygiene = 100;
    run(c, 58); c.p.sleeping = false;
    const bool notYet = !c.p.canFarewellNow();
    run(c, 1); c.p.sleeping = false;
    ck(notYet && c.p.canFarewellNow(), "with the farewell at 60 it is offered at level 60, not 73");
    farewellLevelSet(80); c.p.sleeping = false;
    ck(!c.p.canFarewellNow(), "raising the setting takes the offer back until the new level");
    farewellLevelSet(50); c.p.sleeping = false;
    ck(c.p.canFarewellNow(), "lowering it below the current level offers it at once");
    farewellLevelSet(FAREWELL_LVL_DEFAULT);
  }

  // --- offline catch-up uses the speed too ----------------------------------
  {
    levelSpeedSet(1);
    Clocked c; hatched(c, 6);
    const uint32_t t0 = CLOCK_EPOCH_FLOOR + 1000000UL;
    c.p.setClock(t0);
    c.p.syncClock(t0 + 30UL * 60UL);
    ck(c.p.level() == 31, "30 minutes switched off at 1 min per level is 30 levels");
    levelSpeedSet(60);
    Clocked d; hatched(d, 6);
    d.p.setClock(t0);
    d.p.syncClock(t0 + 30UL * 60UL);
    ck(d.p.level() == 1, "and at 60 min per level it is half of one");
  }
  // --- the REAL age: what the card shows, at any speed --------------------
  {
    levelSpeedSet(1);
    Clocked c; hatched(c, 6);
    c.p.realAgeMin = 0;
    run(c, 90);
    ck(c.p.realAgeMin == 90 && c.p.level() >= 91 - 1,
       "at 1 min per level the real age counts real minutes while the level races");
    // offline too
    const uint32_t t0 = CLOCK_EPOCH_FLOOR + 2000000UL;
    c.p.setClock(t0);
    c.p.syncClock(t0 + 30UL * 60UL);
    ck(c.p.realAgeMin == 120, "time switched off adds real minutes, not game ones");
    // it survives a save and reload
    c.p.saveNow();
    Pet r; r.begin();
    ck(r.realAgeMin == 120, "the real age survives a save and reload");
    // it travels through the party and back
    PartyMon m = c.p.toPartyMon();
    ck(m.hasRealAge() && m.realAgeMin == 120, "a banked record keeps the real age");
    Pet q; q.begin(); q.dbgHatchAs(1, false);
    q.switchTo(m);
    ck(q.realAgeMin == 120, "and making it active again restores it");
    // a record from before the field existed is estimated from its game age
    PartyMon old = m; old.stateVersion = 1; old.realAgeMin = 0;
    old.ageMinutes = 2UL * 1440;
    q.switchTo(old);
    ck(q.realAgeMin == 2UL * 1440, "an older record's real age is its game age, as it was before");
    // a capture starts at zero
    PartyMon wild; wild.dex = 25; wild.level = 30; wild.stateVersion = PARTYMON_STATE_VERSION;
    wild.ageMinutes = 29UL * MINUTES_PER_LEVEL; wild.realAgeMin = 0;
    q.switchTo(wild);
    ck(q.realAgeMin == 0 && q.level() == 30, "a fresh capture is level 30 but zero real minutes old");
    // a frozen companion does not age, in real time either
    q.frozen = true;
    uint32_t now = millis();
    for (int k = 0; k < 10; k++) { now += PET_TICK_MS; q.update(now); }
    ck(q.realAgeMin == 0, "a frozen companion's real age stands still");
  }
  levelSpeedSet(60);

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
