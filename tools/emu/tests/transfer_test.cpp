// A sprite pack streaming in over USB puts the board in TRANSFER MODE: the game
// stops drawing and taking taps so the board spends its time on the card. What
// is pinned here is what makes that safe to have:
//
//   * a tap that opens the menu does nothing while files arrive -- and the same
//     tap works once they stop, so the first check cannot pass on a dead tap
//   * the per-file sprite reload and region rescan wait for the stream to end,
//     and then happen by themselves
//   * a fight is never taken over, because its render path pumps the link
//
// There is no PUT path in the emulator, so sdTransferAt is set by hand: it is
// exactly what the end of a PUT leaves behind on the board.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "sdmon.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed=77; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop(); void render();
uint8_t uiCurrentScreen();
bool transferMode();
void startBattle(int16_t dex, uint8_t lvl);
void emuAdvanceMs(uint32_t ms);
extern const char *const SCREEN_NAME[];
extern Pet pet;
extern bool menuOpen, battleOpen;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static bool on(const char *name){ return !strcmp(SCREEN_NAME[uiCurrentScreen()], name); }

// handleTouch() self-gates to 50 Hz off millis(), so real time has to pass
// between polls; a tight spin is swallowed by that gate.
static void pump(int n){ for(int i=0;i<n;i++){ std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); } }
static void tap(int x,int y){
  g_touchX=x; g_touchY=y; g_touchDown=true; emuFireInterrupt();
  pump(5);
  g_touchDown=false; emuFireInterrupt(); pump(3);
}
static void fileArrived(){ sdTransferAt = millis(); }

int main(){
  setup();
  for(int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.ageMinutes = 60UL*40;
  while (pet.hasLearnOffer()) pet.declineLearn();
  pump(2);
  ck(!transferMode() && on("main"), "no transfer, no transfer screen");

  // ---- taps
  fileArrived();
  ck(transferMode() && on("transfer"), "a file arriving puts the transfer screen up");
  tap(233, 60);                                   // the name band opens the menu
  ck(!menuOpen, "a tap mid-transfer does nothing");
  sdTransferAt = 0;
  tap(233, 60);
  ck(menuOpen, "the same tap opens the menu once the transfer is over");
  menuOpen = false;
  pump(2);

  // ---- the work it defers
  fileArrived();
  sdDirty = sdArtDirty = true;
  pump(3);
  ck(sdDirty && sdArtDirty, "the sprite reload and region rescan wait while files arrive");
  emuAdvanceMs(SD_TRANSFER_IDLE_MS);
  pump(2);
  ck(!transferMode() && on("main"), "the mode ends by itself once the stream goes quiet");
  ck(!sdDirty && !sdArtDirty, "and the reload and rescan it deferred run then");

  // ---- never over a fight
  startBattle(9, 50);
  fileArrived();
  ck(!transferMode() && on("battle"), "a fight is never taken over by a transfer");
  battleOpen = false;
  sdTransferAt = 0;

  printf("%s\n", bad ? "FAILURES" : "transfer mode stands aside, and comes back");
  return bad ? 1 : 0;
}
