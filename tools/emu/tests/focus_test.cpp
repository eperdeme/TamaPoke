// Choosing which creature you raise.
//
// Driven through focusSwap() -- the thing the button actually calls -- not just
// Pet::switchTo() underneath it. CLAUDE.md is explicit about this: reverting the
// move picker to its own gate failed NOTHING until swipe_test was taught to
// drive learnableFor() itself, so a rule proven in isolation says nothing about
// the caller.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=11; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
extern Pet pet;
extern Party party;
void focusSwap(uint8_t slot);

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  setup();
  for (int i=0;i<4;i++) render();
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  while (pet.hasLearnOffer()) pet.declineLearn();

  // A creature with a life behind it: the point of the swap is that none of
  // this is lost.
  pet.ageMinutes = 40UL*60;
  pet.fullness=41; pet.joy=37; pet.energy=52; pet.hygiene=63;
  pet.weight=18; pet.bond=44; pet.careMistakes=3; pet.poops=2;
  pet.berryKnown=true;
  pet.trAtk=30; pet.trDef=20; pet.trSpe=10;
  pet.ivAtk=29; pet.ivDef=11; pet.ivSpe=25; pet.ivHp=7;
  strcpy(pet.nick,"ZARD");
  int16_t liveDex = pet.speciesId;
  uint8_t liveLvl = pet.level();

  PartyMon m; m.dex=9; m.level=52; m.ivAtk=20; m.ivDef=20; m.ivSpe=20; m.ivHp=20;
  m.stateVersion=1;
  m.ageMinutes=(uint32_t)51*MINUTES_PER_LEVEL;
  m.fullness=12; m.joy=90; m.energy=5; m.hygiene=71;
  m.bond=66; m.careMistakes=1; m.weight=40; m.poops=1; m.berryKnown=1;
  strcpy(m.nick,"SHELL");
  party.replaceAt(2, m);

  focusSwap(2);
  ck(pet.speciesId==9, "the stored creature is now the one on screen");
  ck(!strcmp(pet.nick,"SHELL"), "with its nickname");
  ck(pet.level()==52, "and its level");
  ck(pet.fullness==12 && pet.joy==90 && pet.energy==5 && pet.hygiene==71,
     "and its care stats, not fresh ones");
  ck(pet.bond==66 && pet.careMistakes==1 && pet.weight==40 && pet.poops==1,
     "and its bond, mistakes, weight and poops");
  ck(pet.berryKnown, "and whether it had found its berry");
  // Frozen is the OLD revive. A swapped-in creature must age again, or the
  // feature is the thing it replaced wearing a different label.
  ck(!pet.frozen, "it is NOT frozen: it levels again");

  // The exchange half: the creature that was live took the slot.
  ck(party.slots[2].dex==liveDex, "the creature that was on screen took its slot");
  ck(party.slots[2].level==liveLvl, "at the level it had reached");
  ck(!strcmp(party.slots[2].nick,"ZARD"), "with its nickname");
  ck(party.slots[2].hasCareState(), "and a real care record, not a blank one");
  ck(party.slots[2].fullness==41 && party.slots[2].joy==37 &&
     party.slots[2].energy==52 && party.slots[2].hygiene==63,
     "carrying every care stat it had");
  ck(party.slots[2].trAtk==30 && party.slots[2].ivAtk==29,
     "and its training and IVs");
  ck(party.count()==1, "no slot was consumed or freed: it is an exchange");

  // Round trip. This is the whole promise: swapping away and back must return
  // the creature, not a reset copy of it.
  focusSwap(2);
  ck(pet.speciesId==liveDex && pet.level()==liveLvl,
     "swapping back returns the original creature");
  ck(pet.fullness==41 && pet.joy==37 && pet.energy==52 && pet.hygiene==63,
     "with the care state it left with");
  ck(pet.bond==44 && pet.careMistakes==3 && pet.weight==18 && pet.poops==2,
     "and its bond, mistakes, weight and poops");
  ck(pet.ivAtk==29 && pet.ivDef==11 && pet.ivSpe==25 && pet.ivHp==7,
     "and untouched IVs");
  ck(party.slots[2].dex==9 && party.slots[2].joy==90,
     "and the other one is back in the slot, still itself");

  // Negative check: with the care block ignored, the round trip would hand back
  // the default 80/80/80/100. Prove the assertion above can actually fail by
  // feeding it a record that genuinely has no care state.
  PartyMon old; old.dex=25; old.level=30;
  old.ivAtk=old.ivDef=old.ivSpe=old.ivHp=15;
  strcpy(old.nick,"PIKA");
  ck(!old.hasCareState(), "a default-constructed record reports NO care state");
  party.replaceAt(4, old);
  focusSwap(4);
  ck(pet.speciesId==25 && pet.level()==30,
     "a pre-care record still comes back at its banked level");
  ck(pet.fullness==80 && pet.joy==80 && pet.energy==80 && pet.hygiene==100,
     "and starts fresh rather than inventing a care history");
  ck(pet.bond==0 && pet.careMistakes==0,
     "with no bond or mistakes it never actually had");

  // The save has to agree with RAM, or the swap survives only until a reboot.
  pet.saveNow();
  party.save();
  Pet reloaded; reloaded.begin();
  ck(reloaded.speciesId==25 && reloaded.level()==30,
     "the swap survives a reload");
  Party reparty; reparty.begin();
  ck(reparty.slots[4].dex==liveDex,
     "and so does the creature it displaced");
  ck(reparty.slots[4].hasCareState() && reparty.slots[4].joy==37,
     "with its care state intact through NVS");

  // An empty slot must do nothing at all -- not blank the live creature.
  int16_t was = pet.speciesId;
  focusSwap(0);
  ck(pet.speciesId==was, "swapping with an empty slot changes nothing");
  focusSwap(PARTY_SLOTS + 3);
  ck(pet.speciesId==was, "and neither does an out-of-range slot");

  printf(bad?"FAILED %d\n":"OK\n", bad);
  return bad?1:0;
}
