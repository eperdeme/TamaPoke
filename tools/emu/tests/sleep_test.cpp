// Putting the device down must not cost you the creature.
//
// A player left the board running overnight with the pet awake and came back to
// a Dratini that had run away. That was reachable because the LIVE tick is the
// only drain path with no floor: offline floors at 15, asleep floors at 30/35/45
// and skips the neglect check entirely, but a board left RUNNING took an awake
// creature to zero on all four stats in 100 minutes and to the point of running
// away in 160.
//
// Auto-sleep needs BOTH the screen off and the night window (00:00-06:00).
// Screen-off alone paused the game whenever the device was put down, and the
// creature is meant to get hungry during the day. The hour alone sent it to bed
// while somebody was still playing with it.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#ifndef TAMA_NATIVE_TEST
uint32_t g_seed=23; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
#endif
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// an epoch whose hour is exactly h
static uint32_t atHour(int h){ return (uint32_t)h * 3600 + 90; }

// Every case starts awake and unowned. The suite shares one NVS store, so a Pet
// built after another one slept would LOAD that sleep -- which is correct
// behaviour and a wrong starting point for the next test.
static void fresh(Pet &p, int hour){
  p.begin();
  p.dbgHatchAs(147,false);
  p.sleeping = false;
  p.sleepAuto = SLEEP_NONE;
  p.fullness = p.joy = p.energy = p.hygiene = 100;
  p.lastSeenEpoch = atHour(hour);
}

// run n minutes of live ticks, moving the clock with them
static void liveMinutes(Pet &p, int startHour, int n){
  for (int i = 0; i < n; i++) {
    p.lastSeenEpoch = atHour(startHour) + (uint32_t)i * 60;
    p.dbgTick();
  }
}

int main(){
  // --- THE DAY: putting it down does NOT pause the game
  {
    Pet p; fresh(p, 13);
    p.setScreenOff(true);
    ck(!p.sleeping, "screen off in the afternoon does not put it to sleep");
    liveMinutes(p, 13, 120);
    printf("      two hours face-down at 13:00: food=%u joy=%u hyg=%u\n",
           p.fullness, p.joy, p.hygiene);
    ck(p.fullness == 0, "it still gets hungry with the screen off during the day");
  }

  // --- THE NIGHT: screen off after midnight and it sleeps
  {
    Pet p; fresh(p, 1);
    p.setScreenOff(true);
    ck(p.sleeping, "screen off at 01:00 puts it to sleep");
  }

  // --- put down BEFORE midnight: it nods off when the hour comes
  {
    Pet p; fresh(p, 23);
    p.setScreenOff(true);
    ck(!p.sleeping, "put down at 23:00, still awake -- the evening is yours");
    liveMinutes(p, 23, 70);              // through midnight
    ck(p.sleeping, "and it goes to bed by itself once midnight arrives");
  }

  // --- the whole evening still drains, which is the point of a narrow window
  {
    Pet p; fresh(p, 20);
    p.setScreenOff(true);
    liveMinutes(p, 20, 120);             // 20:00 -> 22:00, screen off
    ck(!p.sleeping, "screen off at 20:00 does not sleep it");
    ck(p.fullness == 0, "and the evening drains like any other hour");
  }

  // --- THE ONE THAT MATTERS: the whole night costs nothing
  {
    Pet p; fresh(p, 0);
    p.setScreenOff(true);
    liveMinutes(p, 0, 6 * 60 - 5);       // 00:00 -> 05:55
    printf("      at 05:55: food=%u joy=%u ene=%u hyg=%u ready=%d\n",
           p.fullness, p.joy, p.energy, p.hygiene, (int)p.canRunawayNow());
    ck(p.fullness >= 30 && p.joy >= 35 && p.hygiene >= 45,
       "a night asleep never drops below the floors");
    ck(p.energy > 50, "and it wakes rested");
    ck(!p.canRunawayNow(), "NOWHERE NEAR running away");
    ck(p.speciesId == 147, "the creature is still there");

    // AND IT DOES NOT GET UP BY ITSELF. Waking at 06:00 would reopen the hole:
    // from the floors, food empties by 06:15 and everything by 07:40, so anyone
    // who sleeps past eight would find it ready to run away all over again.
    liveMinutes(p, 6, 3 * 60);           // through to 09:00, screen still off
    ck(p.sleeping, "still asleep at 09:00 -- you might be too");
    ck(!p.canRunawayNow(), "and STILL nowhere near running away");
    printf("      at 09:00 unattended: food=%u joy=%u ene=%u hyg=%u\n",
           p.fullness, p.joy, p.energy, p.hygiene);

    // it wakes when YOU do
    p.setScreenOff(false);
    ck(!p.sleeping, "turning the screen back on is what wakes it");
  }

  // --- the light button still beats both
  {
    Pet p; fresh(p, 13);
    p.toggleLight();
    ck(p.sleeping, "the light puts it to bed by hand, in daylight");
    p.setScreenOff(false);
    ck(p.sleeping, "and neither the screen...");
    liveMinutes(p, 13, 30);
    ck(p.sleeping, "...nor the clock overrules that");
    p.toggleLight();
    ck(!p.sleeping, "only the light does");
  }

  // --- waking it at night keeps it awake rather than fighting the rule
  {
    Pet p; fresh(p, 1);
    p.setScreenOff(true);
    ck(p.sleeping, "asleep at 01:00");
    p.toggleLight();                     // the player wants to play
    ck(!p.sleeping, "the light wakes it");
    liveMinutes(p, 1, 30);
    ck(!p.sleeping, "and it stays awake rather than nodding straight off again");
  }

  // --- total neglect while AWAKE still ends badly: the teeth are still there
  {
    Pet p; fresh(p, 9);
    liveMinutes(p, 9, 170);
    ck(!p.sleeping, "ignored all day, still awake");
    ck(p.canRunawayNow(), "and genuine neglect still makes it ready to leave");
  }

  // --- a board whose clock was never set never auto-sleeps, and says so by
  //     still draining rather than by freezing
  {
    Pet p; fresh(p, 12);
    p.lastSeenEpoch = 0;
    p.setScreenOff(true);
    ck(!p.sleeping, "no clock, no bedtime");
  }

  // --- THE MORNING AFTER. A creature that went to bed already at zero keeps a
  //     neglectTicks of 60 all night, because the sleeping branch of tick()
  //     returns before the neglect block and so neither counts nor clears it.
  //     canRunawayNow() used to read that counter alone, so waking the screen
  //     made a creature with 100 energy instantly ready to leave -- and the
  //     runaway button is drawn over the creature, where the tap that says good
  //     morning lands. It cost a real player a DRAGONAIR.
  {
    Pet p; fresh(p, 22);
    p.fullness = p.joy = p.energy = p.hygiene = 0;   // a neglected evening
    liveMinutes(p, 22, 60);
    ck(p.canRunawayNow(), "armed by an hour of total neglect before bed");

    p.setScreenOff(true);                            // 23:00, not the window yet
    ck(!p.sleeping, "screen off before midnight does not sleep it yet");

    for (int i = 0; i < 540; i++) {                  // through to 08:00
      p.lastSeenEpoch = atHour(23) + (uint32_t)i * 60;
      p.dbgTick();
    }
    ck(p.sleeping, "it slept from midnight through the night");
    ck(p.energy == 100, "and energy recovered while it slept");
    ck(!p.canRunawayNow(), "asleep it cannot leave -- the night itself was safe");

    p.setScreenOff(false);                           // you pick the device up
    ck(!p.canRunawayNow(),
       "and waking it does NOT hand you a runaway: 100 energy is not neglect");
  }

  // --- but the counter is not the only thing that matters: a creature actually
  //     at zero on waking is still ready, or the ending would have no teeth
  {
    Pet p; fresh(p, 22);
    p.fullness = p.joy = p.energy = p.hygiene = 0;
    liveMinutes(p, 22, 60);
    p.dbgSetSeen(atHour(9));                         // morning, awake, still empty
    p.fullness = p.joy = p.energy = p.hygiene = 0;
    ck(p.canRunawayNow(), "genuinely empty on waking is still ready to leave");
  }

  // --- an egg is not something you put to sleep
  {
    Pet p; p.begin(); p.newEgg();
    p.setScreenOff(true);
    ck(!p.sleeping, "an egg does not sleep");
  }

  // --- passive bond: +1 per hour with every need at or above BOND_CALM_MIN,
  // awake or asleep, at most BOND_CALM_DAILY a day for the whole game
  {
    const uint32_t D = 20000UL * 86400UL;      // a real calendar day
    auto run = [](Pet &p, uint32_t from, int minutes, bool topUp) {
      for (int i = 0; i < minutes; i++) {
        p.lastSeenEpoch = from + (uint32_t)i * 60;
        if (topUp) { p.fullness = p.joy = p.energy = p.hygiene = 100; p.poops = 0; }
        p.dbgTick();
      }
    };
    Pet p; fresh(p, 13);
    // put down for a nap after care: needs drain 4x slower, so it earns
    p.bond = 0; p.sleeping = true; p.sleepAuto = SLEEP_PLAYER;
    run(p, D + 13 * 3600, 90, false);
    printf("      a 90-minute nap after care: bond +%u\n", p.bond);
    ck(p.bond == 1, "a creature cared for and put down for a nap earns a point");

    // left awake and ignored: needs fall under the threshold within half an hour
    p.sleeping = false; p.sleepAuto = SLEEP_NONE;
    p.fullness = p.joy = p.energy = p.hygiene = 100; p.poops = 0;
    uint8_t b = p.bond;
    run(p, D + 15 * 3600, 120, false);
    ck(p.bond == b, "one left awake and ignored earns nothing");

    // kept at 100: one point per counted hour, until the day's allowance
    p.bond = 0;
    run(p, D + 17 * 3600, 6 * 60, true);
    printf("      six hours at full care: bond +%u\n", p.bond);
    ck(p.bond == 6, "it pays one point per counted hour");
    run(p, D + 23 * 3600, 59, true);           // the rest of the day, well cared for
    printf("      ...and to midnight: bond +%u (the nap's point came first)\n", p.bond);
    ck(p.bond == 7, "minutes carry over between counted stretches");
    // past midnight is the NEXT calendar day: no care needed for the refill
    run(p, D + 24 * 3600, 3 * 60, true);
    ck(p.bond == 10, "a new calendar day starts its own allowance, even without care");

    // the cap for a day, a swap and a reboot
    Pet q; fresh(q, 13);
    const uint32_t D2 = 20002UL * 86400UL;
    q.bond = 0;
    run(q, D2 + 8 * 3600, 14 * 60, true);
    ck(q.bond == BOND_CALM_DAILY, "fourteen hours at full care still stop at the daily allowance");
    PartyMon other; other.dex = 1; other.level = 20; other.stateVersion = 1;
    other.ageMinutes = 19UL * MINUTES_PER_LEVEL;
    q.switchTo(other); q.sleeping = false;
    uint8_t ob = q.bond;
    run(q, D2 + 22 * 3600, 90, true);
    ck(q.bond == ob, "the allowance is shared: another creature gets none that day");
    q.saveNow();
    {
      Pet r; r.begin(); r.sleeping = false;
      uint8_t rb = r.bond;
      run(r, D2 + 23 * 3600 + 30 * 60, 25, true);
      ck(r.bond == rb, "and a reboot does not refill it");
    }

    // Reaching bond 100 must not spend the remaining shared allowance. A
    // different active creature can still earn the tenth point that day.
    const uint32_t D3 = D2 + 86400UL;
    Pet maxed; fresh(maxed, 13);
    maxed.bond = 91;
    run(maxed, D3 + 8 * 3600, 9 * 60, true);
    ck(maxed.bond == 100, "passive bond can bring a creature to 100");
    maxed.switchTo(other); maxed.sleeping = false;
    run(maxed, D3 + 17 * 3600, 60, true);
    ck(maxed.bond == 1,
       "a creature already at 100 does not consume another member's daily point");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
