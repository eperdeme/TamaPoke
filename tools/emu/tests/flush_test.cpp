// Every screen must flush, or the panel freezes on the previous frame.
// Headless screenshots CANNOT catch this: shotMode reads gfx->buffer()
// directly and never looks at frameReady, which is exactly how the training
// submenu shipped frozen.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=7; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
// wasPressed lives in the sketch and millis() in clock.cpp; both are linked in
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern Arduino_Canvas *gfx;
extern Pet pet;
extern bool cardOpen, galleryOpen, clockOpen, kbOpen, menuOpen, partyOpen, partyPick;
extern bool trainOpen, movePickOpen, battleOpen, gymOpen, playerOpen;
extern bool exploreOpen;
extern uint8_t cardPage;
extern uint8_t partyDetail, boxDetail;
extern bool boxOpen, releaseConfirm;
extern bool bagOpen;
// The four minigames, the paged settings screen and the player's pages. The
// minigames were NEVER covered here, which is a gap in the one test that exists
// because a screen shipped frozen -- and every one of them advances real state
// from its render path, so a missing flush there is worse than a still picture.
extern bool gameOpen, sackOpen, spdOpen, memoOpen;
extern uint8_t clockPage, playerPage;
void startGame(); void startSack(); void startSpeedGame(); void startMemoGame();
extern Party party;
void startBattle(int16_t dex, uint8_t lvl);

static int bad = 0;
static void clearAll(){
  cardOpen=galleryOpen=clockOpen=kbOpen=menuOpen=partyOpen=partyPick=false;
  trainOpen=movePickOpen=battleOpen=gymOpen=playerOpen=false;
  exploreOpen=false;
  boxOpen=releaseConfirm=false;
  bagOpen=false;
  gameOpen=sackOpen=spdOpen=memoOpen=false;
  clockPage=0; playerPage=0;
  partyDetail=boxDetail=0;
}
static void check(const char *name){
  gfx->frameReady = false;
  render();
  if (!gfx->frameReady) { printf("FAIL  %-12s never flushed -- the panel would freeze\n", name); bad++; }
  else printf("PASS  %-12s flushes\n", name);
}

// The crash breadcrumb has to name the screen that is ACTUALLY drawn, or a
// crash report points at the wrong one -- which is worse than no report, since
// it sends the next person hunting in the wrong file. Checked here because
// this test already knows how to put each screen up.
static void crumbIs(const char *want){
  render();
  const char *got = SCREEN_NAME[uiCurrentScreen()];
  if (strcmp(got, want)) {
    printf("FAIL  crumb says '%s' while '%s' is on the panel\n", got, want); bad++;
  } else printf("PASS  crumb   %-12s named correctly\n", want);
}
int main(){
  setup();
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.ageMinutes = 60UL*60;
  while (pet.hasLearnOffer()) pet.declineLearn();

  clearAll(); check("main");
  clearAll(); trainOpen=true;    check("train");
  clearAll(); movePickOpen=true; check("movepick");
  clearAll(); exploreOpen=true;  check("explore");
  clearAll(); gymOpen=true;      check("gyms");
  clearAll(); playerOpen=true;   check("player");
  clearAll(); menuOpen=true;     check("menu");
  clearAll(); partyOpen=true;    check("party");
  clearAll(); bagOpen=true;      check("bag");
  clearAll(); clockOpen=true;    check("clock");
  // Settings is two pages now, and the second one is where brightness and the
  // reset live -- a page that never flushed would look like a frozen device
  // exactly where somebody is trying to change a setting.
  clearAll(); clockOpen=true; clockPage=1; check("settings");
  // Every player page, including the medals and TODAY's checklist. Derived from
  // PLAYER_PAGES rather than a literal, so a ladder added later brings its page.
  for (uint8_t p = 0; p < GYM_REGIONS + 2; p++) {
    clearAll(); playerOpen=true; playerPage=p;
    char n[16]; snprintf(n,sizeof(n),"player%u",p); check(n);
  }
  // THE FOUR MINIGAMES, started properly rather than by setting a flag: each has
  // a live branch and a results branch, and only a real start puts it in the live
  // one. All four APPLY their reward from inside render(), so this is also the
  // test that would catch one of them being skipped.
  clearAll(); startGame();      check("ball");
  clearAll(); startSack();      check("sack");
  clearAll(); startSpeedGame(); check("speed");
  clearAll(); startMemoGame();  check("memo");
  for (uint8_t p=0;p<4;p++){ clearAll(); cardOpen=true; cardPage=p;
    char n[16]; snprintf(n,sizeof(n),"card%u",p); check(n); }
  clearAll(); startBattle(9,50); check("battle");
  // The detail sheets and the release confirm drawn over them. A creature has to
  // be IN the slot: an empty one falls through to the grid, which flushes for a
  // different reason and would pass without ever drawing a sheet.
  {
    PartyMon m; m.dex=25; m.level=30; m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20;
    snprintf(m.nick,sizeof(m.nick),"PIKA");
    party.slots[0]=m; party.box[0]=m; party.save(); party.boxSave();
  }
  clearAll(); partyOpen=true; partyDetail=1;                      check("partysheet");
  clearAll(); partyOpen=true; partyDetail=1; releaseConfirm=true;  check("partyconfirm");
  clearAll(); partyOpen=true; boxOpen=true; boxDetail=1;           check("boxsheet");
  clearAll(); partyOpen=true; boxOpen=true; boxDetail=1; releaseConfirm=true; check("boxconfirm");

  clearAll();                       crumbIs("main");
  clearAll(); trainOpen=true;       crumbIs("train");
  clearAll(); menuOpen=true;        crumbIs("menu");
  clearAll(); partyOpen=true;       crumbIs("party");
  clearAll(); exploreOpen=true;     crumbIs("explore");
  clearAll(); gymOpen=true;         crumbIs("gym");
  clearAll(); playerOpen=true;      crumbIs("player");
  clearAll(); movePickOpen=true;    crumbIs("movepick");
  clearAll(); clockOpen=true;       crumbIs("clock");
  clearAll(); cardOpen=true;        crumbIs("card");
  clearAll(); bagOpen=true;         crumbIs("bag");
  clearAll(); startBattle(9,50);    crumbIs("battle");
  clearAll(); startMemoGame();      crumbIs("minigame");
  clearAll();

  printf("%s\n", bad ? "FAILURES" : "every screen flushes");
  return bad?1:0;
}
