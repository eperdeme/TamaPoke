// Level speed, farewell level, real age and checkpoint upgrade compatibility.
//
// Age is always stored at 60 game minutes per level; the setting only decides how
// many game minutes one real minute adds. What that must guarantee:
// 1. Each speed 1..60 levels up exactly as fast as it says, live and offline,
//    with no drift.
// 2. Switching speed never moves the current level.
// 3. Only growth speeds up: needs drain, egg hatch, passive bond and farewell
//    rules behave as in the original game, in real minutes / in levels.
// 4. Real age tracks real minutes alive across ticks, offline catch-up, saves,
//    party/box trips, older v1 records, captures (0) and frozen companions.
// 5. Both v4.3.1 checkpoints ("BDAY" only) and pre-merge PR #7 checkpoints
//    (untagged 5-byte realAgeMin+growFrac tail) load cleanly without colliding.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "ckpt.h"
#include <cstdio>
#include <cstring>
#include <vector>

#ifndef TAMA_NATIVE_TEST
uint32_t g_seed=11; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
#endif
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

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
  c.p.frozen = false;
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
  eggPreferNewLoad();
  ck(gLevelMins == 60 && gFarewellLevel == 73 && gEggPreferNew,
     "a save with no settings plays the original game: 60 min/lv, farewell at 73, prefer new");
  levelSpeedSet(0);   ck(gLevelMins == 1, "the speed stops at 1, it does not wrap");
  levelSpeedSet(75);  ck(gLevelMins == 60, "and at 60");
  farewellLevelSet(10);  ck(gFarewellLevel == 50, "the farewell level stops at 50");
  farewellLevelSet(140); ck(gFarewellLevel == 100, "and at 100");
  levelSpeedSet(7); farewellLevelSet(61); eggPreferNewSet(false);
  gLevelMins = 60; gFarewellLevel = 73; gEggPreferNew = true;
  levelSpeedLoad(); farewellLevelLoad(); eggPreferNewLoad();
  ck(gLevelMins == 7 && gFarewellLevel == 61 && !gEggPreferNew, "all three survive a reboot");
  eggPreferNewSet(true);
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
  const uint8_t mins[] = { 60, 30, 20, 15, 5, 1 };
  for (int i = 0; i < 6; i++) {
    levelSpeedSet(mins[i]);
    Clocked c; hatched(c, 6);
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
    // Passive bond stays on real minutes (1 point per 60 real minutes of good care)
    levelSpeedSet(1);
    Clocked c; hatched(c, 6);
    c.p.bond = 0;
    c.p.setClock(CLOCK_EPOCH_FLOOR + 500000UL);
    for (int i = 0; i < 60; i++) {
      c.p.fullness = c.p.joy = c.p.energy = c.p.hygiene = 100;
      run(c, 1);
    }
    ck(c.p.bond == 1, "passive bond still earns 1 point per 60 real minutes even at 1 min/level");
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
    // the farewell level setting + farewellDone respected
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
    c.p.farewellDone = true;
    ck(!c.p.canFarewellNow(), "a spent farewell stays spent even when lowering the farewell level");
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

  // --- the REAL age: what the card shows, at any speed ----------------------
  {
    levelSpeedSet(1);
    Clocked c; hatched(c, 6);
    c.p.realAgeMin = 0;
    run(c, 90);
    ck(c.p.realAgeMin == 90 && c.p.level() >= 90,
       "at 1 min per level the real age counts real minutes while the level races");
    const uint32_t t0 = CLOCK_EPOCH_FLOOR + 2000000UL;
    c.p.setClock(t0);
    c.p.syncClock(t0 + 30UL * 60UL);
    ck(c.p.realAgeMin == 120, "time switched off adds real minutes, not game ones");
    c.p.saveNow();
    Pet r; r.begin();
    ck(r.realAgeMin == 120, "the real age survives a save and reload");
    PartyMon m = c.p.toPartyMon();
    ck(m.hasRealAge() && m.realAgeMin == 120, "a banked record keeps the real age");
    Pet q; q.begin(); q.dbgHatchAs(1, false);
    q.switchTo(m);
    ck(q.realAgeMin == 120, "and making it active again restores it");
    PartyMon old = m; old.stateVersion = 1; old.realAgeMin = 0;
    old.ageMinutes = 2UL * 1440;
    q.switchTo(old);
    ck(q.realAgeMin == 2UL * 1440, "an older record's real age is its game age, as it was before");
    PartyMon wild; wild.dex = 25; wild.level = 30; wild.stateVersion = PARTYMON_STATE_VERSION;
    wild.ageMinutes = 29UL * MINUTES_PER_LEVEL; wild.realAgeMin = 0;
    q.switchTo(wild);
    ck(q.realAgeMin == 0 && q.level() == 30, "a fresh capture is level 30 but zero real minutes old");
    q.frozen = true;
    uint32_t now = millis();
    for (int k = 0; k < 10; k++) { now += PET_TICK_MS; q.update(now); }
    ck(q.realAgeMin == 0, "a frozen companion's real age stands still live");
    q.setClock(t0);
    q.syncClock(t0 + 30UL * 60UL);
    ck(q.realAgeMin == 0 && q.level() == 30, "and stands still offline too");
  }

  // --- checkpoint upgrade compatibility: v4.3.1 ("BDAY") and PR #7 (5-byte tail)
  {
    levelSpeedSet(7);
    Clocked c; hatched(c, 6);
    c.p.setClock(CLOCK_EPOCH_FLOOR + 3000000UL);
    c.p.caress();
    run(c, 5);
    c.p.saveNow();
    // 1. Both BDAY and RAGE survive a normal round-trip
    {
      Pet r; r.begin();
      ck(r.realAgeMin == 5 && r.growFrac == c.p.growFrac,
         "both realAgeMin and growFrac survive a tagged v4 checkpoint reload");
    }
    // 2. Simulate a v4.3.1 checkpoint: 48-byte PartyMon + 8-byte "BDAY" tail, NO "RAGE"
    {
      Preferences pr; pr.begin("tamapoke", false);
      size_t n = pr.getBytesLength("petA");
      const char *key = "petA";
      size_t nB = pr.getBytesLength("petB");
      if (nB > 0) {
        std::vector<uint8_t> a(n), b(nB);
        if (n) pr.getBytes("petA", a.data(), n);
        pr.getBytes("petB", b.data(), nB);
        if (!n || ckptRd32(b.data() + 8) > ckptRd32(a.data() + 8)) { key = "petB"; n = nB; }
      }
      std::vector<uint8_t> orig(n);
      pr.getBytes(key, orig.data(), n);
      // Rebuild with monLen = 48 (v4.3.1 stride) + 8-byte BDAY block only
      const size_t PET_FIXED = 70, PET_TAIL_MONLEN = 72, PET_TAIL_MON = 74;
      const size_t oldMonLen = 48;
      const size_t bodyLen = PET_TAIL_MON + oldMonLen + 8;
      std::vector<uint8_t> v431(bodyLen + CKPT_CRC, 0);
      memcpy(v431.data(), orig.data(), PET_TAIL_MON);
      ckptWr16(v431.data() + PET_TAIL_MONLEN, oldMonLen);
      memcpy(v431.data() + PET_TAIL_MON, orig.data() + PET_TAIL_MON, oldMonLen);
      ckptWr32(v431.data() + PET_TAIL_MON + oldMonLen, 0x59414442UL); // "BDAY"
      ckptWr32(v431.data() + PET_TAIL_MON + oldMonLen + 4, 20400UL);
      uint32_t nextGen = ckptRd32(orig.data() + 8) + 10;
      ckptSeal(v431.data(), bodyLen, 0x31504B54UL, 3, nextGen);
      pr.remove("petA"); pr.remove("petB");
      pr.putBytes("petA", v431.data(), v431.size());
      pr.end();

      Pet r; r.begin();
      ck(r.realAgeMin == r.ageMinutes && r.growFrac == 0,
         "a v4.3.1 BDAY checkpoint is never misread as realAgeMin");
      // 3. Simulate a pre-merge PR #7 checkpoint: 52-byte PartyMon + untagged 5-byte tail
      const size_t pr7MonLen = 52;
      const size_t pr7Body = PET_TAIL_MON + pr7MonLen + 5;
      std::vector<uint8_t> pr7(pr7Body + CKPT_CRC, 0);
      memcpy(pr7.data(), orig.data(), PET_TAIL_MON);
      pr7[71] = 0; // PET_TAIL_RSVD was 0 in PR #7
      ckptWr16(pr7.data() + PET_TAIL_MONLEN, pr7MonLen);
      memcpy(pr7.data() + PET_TAIL_MON, orig.data() + PET_TAIL_MON, pr7MonLen);
      ckptWr32(pr7.data() + PET_TAIL_MON + pr7MonLen, 777UL);
      pr7[PET_TAIL_MON + pr7MonLen + 4] = 4;
      ckptSeal(pr7.data(), pr7Body, 0x31504B54UL, 2, nextGen + 1);
      pr.begin("tamapoke", false);
      pr.remove("petA"); pr.remove("petB");
      pr.putBytes("petA", pr7.data(), pr7.size());
      pr.end();

      Pet rPr7; rPr7.begin();
      ck(rPr7.realAgeMin == 777 && rPr7.growFrac == 4,
         "a pre-merge PR #7 untagged 5-byte realAgeMin+growFrac tail migrates cleanly");
      (void)PET_FIXED;
    }
  }
  levelSpeedSet(60);

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
