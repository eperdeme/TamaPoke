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
  // --- the setting itself ---------------------------------------------------
  levelSpeedLoad();
  ck(gLevelMins == 60 && levelSpeedStep() == 1, "a save with no setting plays at the original 60 min per level");
  levelSpeedSet(60);
  const uint8_t order[] = { 30, 15, 5, 1, 60 };
  bool inOrder = true;
  for (uint8_t want : order) { levelSpeedCycle(); if (gLevelMins != want) inOrder = false; }
  ck(inOrder, "tapping cycles 60 -> 30 -> 15 -> 5 -> 1 -> 60");
  levelSpeedSet(5);
  levelSpeedSet(7);
  ck(gLevelMins == 5, "a value not in the list is ignored");
  gLevelMins = 60;
  levelSpeedLoad();
  ck(gLevelMins == 5, "the choice survives a reboot");
  {
    Preferences pr; pr.begin("tamapoke", false); pr.putUChar("lvspd", 42); pr.end();
    levelSpeedLoad();
    ck(gLevelMins == 60, "a junk stored value reads as 60");
  }
  const uint8_t steps[] = { 1, 2, 4, 12, 60 };
  const uint8_t mins[]  = { 60, 30, 15, 5, 1 };
  bool stepsOk = true;
  for (int i = 0; i < 5; i++) { levelSpeedSet(mins[i]); if (levelSpeedStep() != steps[i]) stepsOk = false; }
  ck(stepsOk, "each speed adds 60/N game minutes per real minute");

  // --- each speed levels exactly as advertised ------------------------------
  for (int i = 0; i < 5; i++) {
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
  levelSpeedSet(60);

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
