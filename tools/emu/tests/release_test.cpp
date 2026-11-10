// Moving a party creature into the box, and letting a boxed creature go for good.
//
// This is an IRREVERSIBLE action reached by a tap on a round panel, which is the
// exact shape CLAUDE.md section 4 warns about, so what is pinned here is not
// "does release work" but the things that make it safe:
//
//   * RELEASE never acts on the first tap -- it arms a confirm
//   * NO really cancels, and the creature is still there afterwards
//   * the confirm is MODAL: a tap that would otherwise hit a move row underneath
//     it does nothing, rather than falling through to the picker
//   * TO BOX never arms the destructive confirmation
//   * a released creature is gone from BOTH the party and the box
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "inventory.h"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <thread>
uint32_t g_seed=1234; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void loop(); void render();
void partyTap(int16_t,int16_t); void boxTap(int16_t,int16_t);
void onTap(int16_t,int16_t);
void drawSaveWarning();
bool startWildBattle(uint8_t, bool);
void battleTap(int16_t,int16_t);
int btlCellIndexAt(int16_t,int16_t);
extern bool battleOpen, btlOver, wildCaught;
extern uint8_t btlMenu, btlBagPage, btlMsgCount;
uint8_t learnableList(uint8_t *, uint8_t);
extern Arduino_Canvas *gfx;
extern bool gNvsLow;
extern uint8_t movePickParty, movePickSlot, movePickPage;
uint8_t uiCurrentScreen();
extern const char *const SCREEN_NAME[];
extern uint32_t confirmUntil;
extern uint8_t dimStage;
extern bool holdFired;
extern Pet pet;
extern bool partyOpen, boxOpen, movePickOpen, releaseConfirm;
extern uint8_t partyDetail, boxSwapFrom;
extern uint16_t boxDetail, boxSel;
extern uint8_t boxPage, boxSort;
extern bool partyPick, handoverStored, boxJumpOpen;

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// Geometry taken from the sketch's own constants rather than copied as numbers:
// a test that restates the layout drifts from it and then proves nothing. This
// file used to do exactly that -- PCELL_X/Y were the 2x3 grid's literals, so
// every party tap missed once the slots became a ring.
void partySlotPos(int i, int *cx, int *cy);
static int SLOT_X(int i){ int x, y; partySlotPos(i, &x, &y); return x; }
static int SLOT_Y(int i){ int x, y; partySlotPos(i, &x, &y); return y; }
bool monSheetBtn(int16_t,int16_t,bool);
#define CONF_YES_Y (206 + 52/2)
#define CONF_NO_Y  (268 + 52/2)
#define CONF_X 233

static void sheetButtonCenter(bool primary, int &cx, int &cy) {
  int left=466, right=-1, top=466, bottom=-1;
  for (int y=0;y<466;y++) for (int x=0;x<466;x++)
    if (monSheetBtn(x,y,primary)) {
      if (x<left) left=x; if (x>right) right=x;
      if (y<top) top=y; if (y>bottom) bottom=y;
    }
  cx=(left+right)/2; cy=(top+bottom)/2;
}

// handleTouch() self-gates to 50 Hz off millis(), so real time has to pass
// between polls; a tight spin is swallowed by that gate.
static void pump(int n){ for(int i=0;i<n;i++){ std::this_thread::sleep_for(std::chrono::milliseconds(30)); loop(); } }
static void tap(int x,int y){
  g_touchX=x; g_touchY=y; g_touchDown=true; emuFireInterrupt();
  pump(5);
  g_touchDown=false; emuFireInterrupt(); pump(3);
}
static void hold(int x,int y,int ms){
  g_touchX=x; g_touchY=y; g_touchDown=true; emuFireInterrupt();
  pump(ms/30);
  g_touchDown=false; emuFireInterrupt(); pump(3);
}

static PartyMon mon(int16_t dex, uint16_t lvl, const char *nick){
  PartyMon m; m.dex=dex; m.level=lvl; m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20;
  snprintf(m.nick,sizeof(m.nick),"%s",nick);
  return m;
}
static void clearAll(){
  for(int i=0;i<PARTY_SLOTS;i++) party.releaseAt(i);
  for(auto &m : party.box) m = PartyMon();
  party.boxSave();
  partyOpen=boxOpen=movePickOpen=releaseConfirm=false;
  partyPick=handoverStored=boxJumpOpen=false;
  boxPage=boxSort=0;
  partyDetail=boxDetail=boxSwapFrom=boxSel=0;
}

int main(){
  setup();
  for(int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  pet.ageMinutes = 60UL*40;
  while (pet.hasLearnOffer()) pet.declineLearn();
  int primaryX, primaryY, secondaryX, secondaryY;
  sheetButtonCenter(true, primaryX, primaryY);
  sheetButtonCenter(false, secondaryX, secondaryY);

  // ---- PARTY -> BOX is an explicit, non-destructive action on the sheet
  {
    clearAll();
    party.slots[0]=mon(25,30,"PIKA");
    party.save();
    partyOpen=true;
    partyTap(SLOT_X(0), SLOT_Y(0));      // open its sheet
    ck(partyDetail==1, "tapping a party slot opens its sheet");
    ck(!releaseConfirm, "with no confirm up yet");
    partyTap(secondaryX, secondaryY);             // TO BOX
    ck(!releaseConfirm, "TO BOX does not arm a destructive confirmation");
    ck(boxOpen && !partyDetail, "TO BOX opens storage directly");
    ck(boxSwapFrom==1, "and keeps that party member selected for deposit");
    boxTap(SLOT_X(0), SLOT_Y(0));                // first empty box slot
    ck(party.count()==0 && party.boxCount()==1,
       "tapping a box slot moves the selected creature into storage");
    ck(party.box[0].dex==25, "without releasing it");
  }

  // ---- the box sheet
  {
    clearAll();
    party.box[0]=mon(133,25,"EEVEE");
    party.boxSave();
    partyOpen=true; boxOpen=true;
    boxTap(SLOT_X(0), SLOT_Y(0));
    ck(boxDetail==1, "tapping a box slot opens its sheet");
    ck(party.count()==0, "rather than moving the creature on one tap");

    boxTap(secondaryX, secondaryY);               // RELEASE
    ck(releaseConfirm, "RELEASE arms a confirm on the box sheet");
    movePickOpen=false;
    boxTap(233, 170);                             // over a move row beneath it
    ck(!movePickOpen, "the confirm is modal: the sheet underneath is inert");
    ck(party.boxCount()==1, "and the creature is still there before confirmation");
    boxTap(CONF_X, CONF_NO_Y);
    ck(party.boxCount()==1, "NO keeps it");

    boxTap(secondaryX, secondaryY);
    boxTap(CONF_X, CONF_YES_Y);
    ck(party.boxCount()==0, "the confirmed RELEASE lets a boxed creature go");
    ck(party.count()==0, "and it is not pushed into the party instead");
    Party q; q.begin();
    ck(q.boxCount()==0, "the release is persisted, not only in RAM");
  }

  // ---- TO PARTY still works, since the sheet replaced a direct tap
  {
    clearAll();
    party.box[0]=mon(143,40,"SNORLAX");
    party.boxSave();
    partyOpen=true; boxOpen=true;
    boxTap(SLOT_X(0), SLOT_Y(0));
    boxTap(primaryX, primaryY);                   // TO PARTY
    ck(party.count()==1 && party.slots[0].dex==143, "TO PARTY withdraws it");
    ck(party.boxCount()==0, "and the box slot is freed");
  }

  // ---- a full party still opens the box sheet before asking who steps out
  {
    clearAll();
    for (int i=0;i<PARTY_SLOTS;i++) party.slots[i]=mon(10+i,30+i,"PARTY");
    party.box[0]=mon(143,40,"SNORLAX");
    party.save();
    party.boxSave();
    partyOpen=true; boxOpen=true;
    boxTap(SLOT_X(0), SLOT_Y(0));
    ck(boxDetail==1 && boxOpen, "a full party still opens the boxed creature's sheet");
    ck(boxSel==0, "and does not ask for a replacement before TO PARTY is pressed");
    boxTap(primaryX, primaryY);                   // TO PARTY
    ck(!boxOpen && boxDetail==0 && boxSel==1,
       "TO PARTY then opens the party replacement picker");
    int16_t oldPartyDex=party.slots[2].dex;
    partyTap(SLOT_X(2), SLOT_Y(2));
    ck(party.slots[2].dex==143 && party.box[0].dex==oldPartyDex,
       "choosing a party slot exchanges the two creatures");
    ck(boxSel==0, "and clears the pending replacement");
  }

  // ---- MAKE ACTIVE, tapped where a thumb actually lands
  {
    clearAll();
    const uint16_t last = BOX_SLOTS - 1;
    party.box[last] = mon(143,40,"LAST");
    party.boxSave();
    partyOpen=boxOpen=true;
    boxPage=BOX_SLOTS/6-1;
    boxTap(SLOT_X(5), SLOT_Y(5));
    ck(boxDetail == BOX_SLOTS, "the last page opens the last real box slot");
    nvsFailWritesAfter(0);
    boxTap(primaryX, primaryY);
    ck(boxDetail == BOX_SLOTS && party.count() == 0 && party.box[last].dex == 143,
       "failed withdrawal retains its sheet and does not announce a move");
    nvsResumeWrites();
    boxTap(secondaryX, secondaryY);
    nvsFailWritesAfter(0);
    boxTap(CONF_X, CONF_YES_Y);
    ck(releaseConfirm && boxDetail == BOX_SLOTS && party.box[last].dex == 143,
       "failed release keeps the creature and its confirmation");
    nvsResumeWrites();
    boxTap(CONF_X, CONF_NO_Y);
    boxTap(primaryX, primaryY);
    ck(!boxDetail && party.slots[0].dex == 143 && party.box[last].empty(),
       "retry withdraws the same high-index creature");

    clearAll();
    for (int i=0;i<PARTY_SLOTS;i++) party.slots[i]=mon(10+i,30,"PARTY");
    party.box[last]=mon(143,40,"LAST");
    party.save();
    partyOpen=boxOpen=true; boxPage=BOX_SLOTS/6-1;
    boxTap(SLOT_X(5), SLOT_Y(5));
    boxTap(primaryX, primaryY);
    ck(boxSel == BOX_SLOTS, "the replacement picker retains a high box index");
    nvsFailWritesAfter(0);
    partyTap(SLOT_X(2), SLOT_Y(2));
    ck(boxSel == BOX_SLOTS && party.slots[2].dex == 12 && party.box[last].dex == 143,
       "failed replacement leaves both creatures and the pending selection");
    nvsResumeWrites();
    partyTap(SLOT_X(2), SLOT_Y(2));
    ck(!boxSel && party.slots[2].dex == 143 && party.box[last].dex == 12,
       "retry exchanges the selected high-index creature");
  }

  // ---- a failed handover is neither a full collection nor permission to drop it
  {
    clearAll();
    pet.setEnded(mon(25,30,"WAIT"), CER_RELEASE);
    nvsFailWritesAfter(0);
    loop();
    ck(pet.endedKind != CER_NONE && !partyPick && party.count() == 0,
       "failed automatic add preserves the handover without an overwrite prompt");
    nvsResumeWrites();
    loop();
    ck(pet.endedKind == CER_NONE && party.count() == 1 && party.slots[0].dex == 25,
       "retry persists the handover before consuming it");
    clearAll();
    pet.setEnded(mon(133,30,"ACK"), CER_RELEASE);
    nvsFailWritesAfter(1);
    loop();
    ck(handoverStored && pet.endedKind != CER_NONE && party.count() == 1,
       "a failed pet acknowledgement retains the pending handover");
    nvsResumeWrites();
    loop();
    ck(!handoverStored && pet.endedKind == CER_NONE && party.count() == 1,
       "acknowledgement retry does not bank the same creature twice");
  }

  {
    clearAll();
    party.slots[0]=mon(6,100,"MOVES");
    party.save();
    partyOpen=true; movePickOpen=true;
    movePickParty=1; movePickSlot=0; movePickPage=0;
    uint8_t choices[64];
    ck(learnableList(choices,sizeof(choices)) > 1, "the party move picker has real choices");
    party.slots[0].moves[0]=choices[1];
    party.save();
    uint8_t before[MOVE_SLOTS];
    memcpy(before,party.slots[0].moves,sizeof(before));
    nvsFailWritesAfter(0);
    onTap(233,90);
    ck(movePickOpen && !memcmp(before,party.slots[0].moves,sizeof(before)),
       "failed party move edit restores the old moves and leaves the picker open");
    gfx->fillScreen(RGB565_BLACK);
    gNvsLow=false;
    drawSaveWarning();
    bool warning=false;
    for (auto pixel : gfx->fb) if (pixel != RGB565_BLACK) warning=true;
    ck(pet.saveHealthy() && !party.saveHealthy() && warning,
       "a collection failure alone draws the real save warning");
    nvsResumeWrites();
    onTap(233,90);
    ck(!movePickOpen && party.slots[0].moves[0] == choices[0],
       "retry commits the party move edit before closing the picker");
    movePickParty=0;
  }

  {
    clearAll();
    partyOpen=false;
    bag.clear();
    bag.add(IT_MASTERBALL,2);
    gRegionArt=0xFFFF;
    if (pet.isEgg()) pet.dbgHatchAs(6,false);
    ck(startWildBattle(0,false), "a real wild encounter is available for the storage-failure check");
    btlMsgCount=0; btlMenu=3; btlBagPage=0;
    int left=466,right=0,top=466,bottom=0;
    for (int y=0;y<466;y++) for (int x=0;x<466;x++)
      if (btlCellIndexAt(x,y)==0) {
        if(x<left)left=x; if(x>right)right=x;
        if(y<top)top=y; if(y>bottom)bottom=y;
      }
    // The ball decrement commits first; collection persistence then fails.
    nvsFailWritesAfter(1);
    battleTap((left+right)/2,(top+bottom)/2);
    ck(battleOpen && !btlOver && !wildCaught && party.count()==0 &&
       pet.endedKind==CER_NONE,
       "failed capture persistence retains the encounter without claiming a catch or replacement prompt");
    nvsResumeWrites();
    btlMsgCount=0; btlMenu=3;
    battleTap((left+right)/2,(top+bottom)/2);
    ck(btlOver && wildCaught && party.count()==1,
       "retry catches and banks the same encounter after storage recovers");
    battleOpen=false;
  }

  // ---- MAKE ACTIVE, tapped where a thumb actually lands
  //
  // With an egg waiting it must work, AND it must work from the panel's centre
  // line, which is where the old action was drawn full-width and where a thumb
  // goes by default.
  {
    clearAll();
    party.slots[0] = mon(3, 100, "");
    party.save();
    if (!pet.isEgg()) { pet.newEgg(); }
    if (pet.awaitingStarter()) pet.chooseStarter(4);
    partyOpen = true;
    partyTap(SLOT_X(0), SLOT_Y(0));
    ck(partyDetail == 1, "the sheet opens with an egg waiting");
    partyTap(primaryX, primaryY);                // centre primary action
    ck(pet.speciesId == 3, "MAKE ACTIVE works when tapped at the panel centre");
    ck(party.count() == 0, "and the creature leaves the party to become the pet");
  }

  // ---- the 3 s hold, and WHERE it is allowed to fire
  //
  // The hold opens "release the live pet?" -- unconfirmed, irreversible, and
  // drawn over the creature. It used to be gated by a hand-kept list of screens
  // to EXCLUDE (gallery, card, keyboard, clock), which left it live on the party
  // screen, whose grid overlaps inPetZone. Worse, that dialog's YES box lands on
  // top of party slot 4: hold a slot, tap where you think a creature is, and
  // lose the one you are actually raising.
  //
  // BOTH halves are asserted. "It never fires on the party screen" is vacuously
  // true if the hold has stopped working everywhere, which would delete a real
  // feature and still pass -- and an earlier version of this check did exactly
  // that, silently, until the old gate was put back and it stayed green.
  {
    clearAll();
    partyOpen=boxOpen=true;
    tap(440,440);  // wake without hitting a control
    boxOpen=true;
    uint8_t oldSort=boxSort;
    tap(233,224);
    ck(boxSort != oldSort && !boxJumpOpen, "a hub tap still changes the box order");
    hold(233,224,1200);
    ck(boxJumpOpen && boxSort != oldSort, "a one-second hub hold opens grouped pages without sorting again");
    boxJumpOpen=false;

    clearAll();
    party.slots[0]=mon(25,30,"PIKA");
    party.save();
    if (pet.isEgg()) pet.dbgHatchAs(6,false);
    while (pet.hasLearnOffer()) pet.declineLearn();
    if (pet.sleeping) pet.toggleLight();

    // Wake first: swallowGesture is (dimStage > 0 || screenOff), sampled when the
    // finger goes down, so a dimmed panel eats the hold before the gate is even
    // consulted. (440,440) is off every control on both screens.
    partyOpen=true;
    tap(440,440);
    ck(dimStage==0, "the panel is awake, so the gate is what is being tested");
    ck(!strcmp(SCREEN_NAME[uiCurrentScreen()],"party"), "and the party screen is up");
    confirmUntil=0; holdFired=false;
    hold(SLOT_X(0), SLOT_Y(0), 3400);       // dead centre of party slot 0
    ck(confirmUntil==0, "holding a party slot does NOT open the release dialog");
    ck(partyDetail==0 || !holdFired, "and it was a hold, not a tap that opened a sheet");

    clearAll();
    tap(440,440);
    ck(dimStage==0, "still awake for the companion check");
    ck(!strcmp(SCREEN_NAME[uiCurrentScreen()],"main"),
       "and back on the main screen, or the check below proves nothing");
    confirmUntil=0; holdFired=false;
    hold(233,200,3400);                     // the creature itself
    ck(confirmUntil!=0, "the hold STILL works on the main screen, where it belongs");
    confirmUntil=0;
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
