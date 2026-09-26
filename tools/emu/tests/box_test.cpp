// The box is a separate NVS key on purpose. This checks the swap is a real
// exchange in both directions, that it persists, and -- most importantly --
// that a save written before the box existed still loads its party intact.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "dex.h"
#include <cstdio>
#include <cstring>
uint32_t g_seed=3; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}
static PartyMon mk(int dex,int lvl){ PartyMon m; m.dex=dex; m.level=lvl;
  m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20; return m; }

int main(){
  // a save from BEFORE the box existed: party key only, no box key
  { Preferences seed; seed.begin("tamapoke", false);
    PartyMon old[PARTY_SLOTS];
    for (int i=0;i<PARTY_SLOTS;i++) old[i]=mk(1+i*20, 30+i);
    seed.putBytes("party", old, sizeof(old));
    seed.end(); }
  Party p; p.begin();
  bool kept = true;
  for (int i=0;i<PARTY_SLOTS;i++) if (p.slots[i].dex != 1+i*20) kept=false;
  ck(kept, "a pre-box save keeps its whole party");
  ck(p.boxCount()==0, "and comes up with an empty box, not garbage");

  // deposit: party slot 0 <-> empty box slot 0
  int16_t was = p.slots[0].dex;
  p.swapPartyBox(0, 0);
  ck(p.box[0].dex==was && p.slots[0].empty(), "swapping into an empty box slot deposits");
  // withdraw: the same call the other way round
  p.swapPartyBox(0, 0);
  ck(p.slots[0].dex==was && p.box[0].empty(), "and swapping back withdraws");

  // a real exchange, both occupied
  p.box[3] = mk(150, 70);
  int16_t a = p.slots[2].dex, b = p.box[3].dex;
  p.swapPartyBox(2, 3);
  ck(p.slots[2].dex==b && p.box[3].dex==a, "two occupied slots exchange");

  // and it survives a reload
  Party q; q.begin();
  ck(q.slots[2].dex==b && q.box[3].dex==a, "the swap persists across a reload");

  ck(p.boxFirstFree()==0, "boxFirstFree finds the hole");
  for (int i=0;i<BOX_SLOTS;i++) p.box[i]=mk(19,5);
  ck(p.boxFirstFree()==-1 && !p.boxAdd(mk(1,1)), "a full box refuses more");

  // a farewell with a full party must reach the box rather than being stuck
  { Party r; r.begin();
    for (int i=0;i<PARTY_SLOTS;i++) r.slots[i]=mk(1+i,40);
    for (int i=0;i<BOX_SLOTS;i++) r.box[i]=PartyMon();
    r.save(); r.boxSave();
    PartyMon newcomer = mk(150, 73);
    bool toParty = r.add(newcomer);
    bool toBox = toParty ? false : r.boxAdd(newcomer);
    ck(!toParty && toBox, "a full party sends the newcomer to the box");
    ck(r.box[0].dex==150, "and it is really there");
    // a full party AND a full box is the only case that should refuse
    for (int i=0;i<BOX_SLOTS;i++) r.box[i]=mk(19,5);
    ck(!r.add(newcomer) && !r.boxAdd(newcomer),
       "only a full party AND a full box refuses, which is when the player picks");
  }

  // withdrawing into a party that has room must not need a party slot picked
  { Party r; r.begin();
    for (int i=0;i<PARTY_SLOTS;i++) r.slots[i]=PartyMon();
    for (int i=0;i<BOX_SLOTS;i++) r.box[i]=PartyMon();
    r.slots[0]=mk(6,50); r.box[0]=mk(9,40);
    r.save(); r.boxSave();
    int free = r.firstFree();
    ck(free==1, "the first free party slot is found");
    r.swapPartyBox((uint8_t)free, 0);
    ck(r.slots[1].dex==9 && r.box[0].empty(),
       "withdrawing into a free slot moves it without displacing anyone");
  }

  // A SWAP IS ONE MOVE AND MUST COMMIT AS ONE.
  //
  // swapPartyBox() changed the party and the box, which used to be two separate
  // NVS keys written one after the other. A cut between them left the creature
  // that moved OUT of the box sitting in both places while the one that moved in
  // was lost -- a duplicate and a loss from a single interrupted swap, which is
  // the same fault as issue #3 and worse, because it invents a creature.
  //
  // The assertion is the INVARIANT rather than either outcome: whatever happens,
  // each creature must appear exactly once across the pair. Both legal results
  // satisfy it -- the swap happened, or it did not -- and only a mixture fails.
  // A test pinned to one of the two outcomes would have to guess how far the
  // write got.
  {
    nvs().clear();
    Party r;
    r.begin();
    for (int i = 0; i < PARTY_SLOTS; i++) r.slots[i] = PartyMon();
    for (int i = 0; i < BOX_SLOTS; i++) r.box[i] = PartyMon();
    r.slots[0] = mk(6, 50);        // CHARIZARD in the party
    r.box[0] = mk(25, 40);         // PIKACHU in the box
    r.save();
    r.boxSave();

    // One successful write, then the power goes. Under the old two-write swap
    // that is precisely the torn state; under a single-write pair it is either
    // wholly done or wholly not.
    nvsFailWritesAfter(1);
    r.swapPartyBox(0, 0);
    nvsResumeWrites();

    Party loaded;
    loaded.begin();
    auto countOf = [&](int16_t dex) {
      int n = 0;
      for (int i = 0; i < PARTY_SLOTS; i++) if (loaded.slots[i].dex == dex) n++;
      for (int i = 0; i < BOX_SLOTS; i++) if (loaded.box[i].dex == dex) n++;
      return n;
    };
    printf("      after a cut mid-swap: %d x CHARIZARD, %d x PIKACHU\n",
           countOf(6), countOf(25));
    ck(countOf(6) == 1, "an interrupted swap does not lose or duplicate the party creature");
    ck(countOf(25) == 1, "nor the box creature");
    // And the pair has to be one of the two legal arrangements, not a blend.
    bool before = loaded.slots[0].dex == 6 && loaded.box[0].dex == 25;
    bool after = loaded.slots[0].dex == 25 && loaded.box[0].dex == 6;
    ck(before || after, "the pair reads as either the state before the swap or the state after it");
  }

  // A save whose BOX was written before PartyMon grew.
  //
  // Last, and self-contained: it seeds NVS itself and builds its own Party, so
  // it does not care what the suite above left behind -- and nothing below it
  // inherits the odd-sized blob it writes.
  //
  // This is the dangerous direction and it had NO test at all. getBytes()
  // refuses an oversized blob and leaves the destination alone, so a box stored
  // at a shorter stride read back EMPTY and the next boxSave() wrote that
  // emptiness over the real one. The party has always migrated by length; the
  // box did not, and nothing noticed because the record had never grown.
  {
    // Genuinely from scratch, which this block always CLAIMED to be and was not.
    // It seeded only the "box" key, so once the pair gained a checkpoint the
    // records left behind by the blocks above were newer than the legacy blob
    // being seeded here -- and the checkpoint rightly won, which read as the
    // migration having failed. CLAUDE.md § "a state a test leaves behind is the
    // next test's input", caught by this migration breaking rather than by the
    // block that caused it.
    nvs().clear();
    const size_t oldStride = sizeof(PartyMon) - 12;   // any earlier, shorter layout
    uint8_t old[oldStride * BOX_SLOTS];
    memset(old, 0, sizeof(old));
    for (int i=0;i<BOX_SLOTS;i++) {
      PartyMon m = mk(30+i, 25+i);
      memcpy(old + i*oldStride, &m, oldStride);   // the leading fields only
    }
    Preferences seed; seed.begin("tamapoke", false);
    seed.putBytes("box", old, sizeof(old));
    seed.end();
    Party g; g.begin();
    bool all = true;
    for (int i=0;i<BOX_SLOTS;i++) if (g.box[i].dex != 30+i) all=false;
    ck(g.boxCount()==BOX_SLOTS, "a box written at an older, shorter stride is not lost");
    ck(all, "and every record lands at the right offset");
    ck(!g.box[0].hasCareState(),
       "a migrated record admits it predates care state rather than faking it");
    // Migration must REWRITE at the current layout, or it runs on every boot
    // and no appended field can ever be trusted.
    Preferences chk; chk.begin("tamapoke", false);
    ck(chk.getBytesLength("box") == sizeof(g.box),
       "and it is rewritten in the current layout, once");
    chk.end();
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
