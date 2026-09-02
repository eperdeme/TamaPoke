// Building trainer or wild opponents must not overwrite the player's creature.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include <cstdio>

uint32_t g_seed = 77;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static void seedPlayer() {
  Pet player;
  player.begin();
  player.dbgHatchAs(6, false);              // CHARIZARD
  player.ageMinutes = 60UL * 72;            // level 73
  player.ivAtk = 30;
  player.ivDef = 29;
  player.ivSpe = 28;
  player.ivHp = 27;
  player.rename("REAL");
  player.saveNow();
}

int main() {
  seedPlayer();

  {
    Pet trainerFoe;
    trainerFoe.dbgHatchAs(95, false);         // ONIX
    trainerFoe.ivAtk = trainerFoe.ivDef = trainerFoe.ivSpe = trainerFoe.ivHp = 16;
    trainerFoe.ageMinutes = 60UL * 13;
    trainerFoe.relearnFromLevel();
  }
  {
    Pet player;
    player.begin();
    ck(player.speciesId == 6, "a trainer opponent does not replace the stored player");
    ck(player.level() == 73, "the player keeps its level after a trainer is built");
    ck(player.ivAtk == 30 && player.ivDef == 29 && player.ivSpe == 28 && player.ivHp == 27,
       "the player keeps its IVs after a trainer is built");
    ck(!strcmp(player.nick, "REAL"), "the player keeps its name after a trainer is built");
    ck(!player.isRegistered(95), "a trainer opponent is not registered as raised");
    ck(player.isRegistered(6), "the raised creature stays registered after a trainer is built");
  }

  seedPlayer();
  {
    Pet wildFoe;
    wildFoe.dbgHatchAs(25, true);             // shiny PIKACHU
    wildFoe.ivAtk = 20;
    wildFoe.ivDef = 21;
    wildFoe.ivSpe = 22;
    wildFoe.ivHp = 23;
    wildFoe.ageMinutes = 60UL * 24;
    wildFoe.relearnFromLevel();
  }

  {
    Pet player;
    player.begin();
    ck(player.speciesId == 6, "a wild opponent does not replace the stored player");
    ck(player.level() == 73, "the player keeps its level after a wild foe is built");
    ck(player.ivAtk == 30 && player.ivDef == 29 && player.ivSpe == 28 && player.ivHp == 27,
       "the player keeps its IVs after a wild foe is built");
    ck(!strcmp(player.nick, "REAL"), "the player keeps its name after a wild foe is built");
    ck(!player.isRegistered(25), "a wild opponent is not registered before capture");
    ck(player.isRegistered(6), "the raised creature stays registered after a wild foe is built");
  }

  {
    Pet player;
    player.begin();
    player.ageMinutes = 60UL * 80;
    player.saveNow();
    Pet reloaded;
    reloaded.begin();
    ck(reloaded.level() == 81, "an opened Pet still saves normally");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
