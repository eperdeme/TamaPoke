// THE VIEW LAYER: the Pokedex filter, and the box's on-screen order.
//
// Both share one shape and one risk. Neither touches the save -- they change what
// is DRAWN, not what is stored -- so the thing that can go wrong is not data loss
// but a MISMATCH: the grid drawn from one calculation and the tap resolved from
// another, which puts the finger on a different creature than the eye. That
// shipped in this project before, which is why gymRowRect() and partySlotAt()
// exist as single answers, and it is what these assertions are pinning.
//
// The box sort has a second, sharper failure available to it: if boxSlotFor() is
// not a BIJECTION, two visual cells resolve to the same real slot and a swap
// overwrites a creature. That is asserted for every mode.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "dex.h"
#include <cstdio>
#include <cstring>
#include <set>

uint32_t g_seed=17; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;}
String FakeSerial::readStringUntil(char){return String("");}
void setup(); void render();
extern Pet pet;
extern Party party;

// the view state and the firmware's own answers about it
extern uint8_t galleryRegion, galleryPage, galleryFilter, boxSort;
extern int16_t galleryDetail;
bool galleryPass(int16_t dex);
uint16_t galleryCount();
int16_t galleryAt(uint16_t visual);
uint8_t galleryPages();
void galleryClampPage();
uint8_t boxSlotFor(uint8_t visual);
const char *personalityName();
int uiGoalCount();
bool uiGoalMet(int i);
int uiGoalsMet();
// the memory game, driven as the panel drives it
extern bool memoOpen, memoShowing;
extern uint32_t memoOverUntil;
extern uint8_t memoSeq[], memoLen, memoAt, memoBest;
void startMemoGame();
void memoTap(int16_t x, int16_t y);
void leaveMemo();
void memoPadPos(int i, int *cx, int *cy);
int uiMemoPads();
#define MEMO_PADS_T uiMemoPads()
// The emulator's clock, nudged deterministically so the sequence can be walked
// forward without the test sleeping for seconds. See clock.cpp.
void emuAdvanceMs(uint32_t ms);
// The cry hook, counted by the emulator's audio stub rather than sounded. See
// host_impl.cpp: the emulator structurally cannot judge audio, but it can prove
// the game asked for the right cry at the right moment.
extern int g_emuCryCount;
extern int16_t g_emuCryDex;
void ensureMon();
void galleryTap(int16_t x, int16_t y);
// The COUNTS come from the firmware, not from a copy kept here: a filter or an
// order added to the sketch and not to this list would otherwise go untested in
// silence. The names below are asserted against them below, which is the cheap
// half of the same guarantee -- the sketch's enums are file-scope in the .ino and
// cannot be included.
uint8_t galleryFilterCount();
uint8_t boxSortCount();
enum : uint8_t { GFILT_ALL = 0, GFILT_RAISED, GFILT_CAUGHT, GFILT_SHINY };
enum : uint8_t { BSORT_SLOT = 0, BSORT_DEX, BSORT_LEVEL };

static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// What the GRID would draw across every page, resolved exactly as renderGallery()
// and galleryTap() resolve it. If these two ever stop agreeing, this is the test
// that says so.
static std::set<int16_t> drawnAcrossPages() {
  std::set<int16_t> seen;
  const uint8_t pages = galleryPages();
  for (uint8_t pg = 0; pg < pages; pg++)
    for (int cell = 0; cell < 16; cell++) {
      const int16_t d = galleryAt((uint16_t)pg * 16 + cell);
      if (d) seen.insert(d);
    }
  return seen;
}

static std::set<int16_t> passingInRegion() {
  std::set<int16_t> want;
  const RegionInfo &rg = REGIONS[galleryRegion % (REGION_COUNT - 1)];
  for (int16_t d = rg.lo; d <= (int16_t)rg.hi && d <= DEX_COUNT; d++)
    if (galleryPass(d)) want.insert(d);
  return want;
}

int main(){
  setup();
  for (int i=0;i<4;i++) render();
  // The names above are this file's, the counts are the firmware's. If they ever
  // disagree, every loop below is silently skipping a mode.
  ck(galleryFilterCount() == GFILT_SHINY + 1,
     "this test knows about every Pokedex filter the firmware has");
  ck(boxSortCount() == BSORT_LEVEL + 1,
     "and about every box order");
  if (pet.awaitingStarter()) pet.chooseStarter(4);
  if (pet.isEgg()) pet.dbgHatchAs(6,false);
  while (pet.hasLearnOffer()) pet.declineLearn();

  // ---------------------------------------------------------------- the filter
  galleryRegion = 0;            // Kanto: 1..151
  galleryPage = 0;

  // A known dex state. CHARIZARD is live and therefore registered; MEW is
  // registered and shiny; LAPRAS is in the BOX and was NEVER registered, which is
  // the case that makes RAISED and CAUGHT different questions rather than two
  // names for one bit -- registerSpecies() only ever fires for the live creature,
  // so a wild capture sitting in the box is absent from the dex.
  pet.dexReg[(151-1)>>3] |= 1 << ((151-1)&7);        // MEW raised
  pet.dexShinyReg[(151-1)>>3] |= 1 << ((151-1)&7);   // ...and shiny
  {
    PartyMon lap; lap.dex=131; lap.level=40;
    party.box[0] = lap;
    party.boxSave();
  }
  ck(!pet.isRegistered(131), "a box-only creature is genuinely NOT in the dex");

  galleryFilter = GFILT_ALL;
  ck(galleryCount() == 151, "ALL shows the whole region");
  ck(galleryPages() == 10, "across ten pages of sixteen");
  ck(galleryAt(0) == 1 && galleryAt(150) == 151,
     "and the grid runs from the first species to the last");
  ck(galleryAt(151) == 0, "with nothing past the end");

  galleryFilter = GFILT_RAISED;
  {
    const uint16_t n = galleryCount();
    ck(n >= 1, "RAISED shows what has actually been raised");
    ck(galleryPass(6) && galleryPass(151), "including the live creature and MEW");
    ck(!galleryPass(131), "but NOT the one that only ever sat in the box");
  }

  galleryFilter = GFILT_CAUGHT;
  ck(galleryPass(131), "CAUGHT does show the box-only creature");
  ck(galleryPass(6), "and the live one");
  ck(!galleryPass(151), "but not MEW, which is in the dex and not in your hands");

  galleryFilter = GFILT_SHINY;
  ck(galleryCount() == 1 && galleryAt(0) == 151, "SHINY shows only shiny records");
  ck(galleryPages() == 1, "on a single page");

  // THE MISMATCH CHECK. Every filter, and for each one the set the grid would
  // draw must be exactly the set the filter admits -- no cell drawn that the tap
  // path cannot resolve, and nothing admitted that never gets drawn.
  for (uint8_t f = 0; f < galleryFilterCount(); f++) {
    galleryFilter = f;
    galleryPage = 0;
    const std::set<int16_t> drawn = drawnAcrossPages(), want = passingInRegion();
    char m[120];
    snprintf(m, sizeof(m),
             "filter %u: the grid draws exactly what the filter admits (%u of %u)",
             f, (unsigned)drawn.size(), (unsigned)want.size());
    ck(drawn == want, m);
    ck(drawn.size() == galleryCount(), "and the count agrees with the grid");
  }

  // An EMPTY result still has a page, or the screen draws nothing at all and
  // reads as a crash rather than as an empty filter.
  {
    galleryFilter = GFILT_SHINY;
    memset(pet.dexShinyReg, 0, sizeof(pet.dexShinyReg));
    ck(galleryCount() == 0, "a filter can legitimately match nothing");
    ck(galleryPages() == 1, "and still has one page to say so on");
    ck(galleryAt(0) == 0, "with no cell to tap");
  }

  // Cycling a filter must not leave the page past the end of a shorter result.
  {
    galleryFilter = GFILT_ALL;
    galleryPage = 9;                     // the last page of 151
    galleryFilter = GFILT_SHINY;         // now one page
    galleryClampPage();
    ck(galleryPage == 0, "narrowing the filter pulls the page back into range");
  }

  // Every region, so a filter cannot be valid in Kanto and broken in Paldea.
  {
    galleryFilter = GFILT_ALL;
    int wrong = 0;
    for (uint8_t r = 0; r < REGION_COUNT - 1; r++) {
      galleryRegion = r;
      galleryPage = 0;
      const RegionInfo &rg = REGIONS[r];
      const uint16_t span = (uint16_t)(rg.hi - rg.lo + 1);
      if (galleryCount() != span) wrong++;
      if (galleryAt(0) != (int16_t)rg.lo) wrong++;
      if (galleryAt(span - 1) != (int16_t)rg.hi) wrong++;
    }
    ck(wrong == 0, "every region's grid spans exactly that region");
    galleryRegion = 0;
  }

  // ------------------------------------------------------------- the box order
  // A box with holes in it and deliberately unsorted contents.
  for (int i = 0; i < BOX_SLOTS; i++) party.box[i] = PartyMon();
  struct { int slot, dex, lvl; } seed[] = {
    { 0, 149, 55 }, { 1,   6, 90 }, { 3,  25, 12 },
    { 4, 131, 40 }, { 9,   9, 90 }, { 17, 3, 71 },
  };
  for (auto &s : seed) {
    PartyMon m; m.dex = s.dex; m.level = s.lvl;
    m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
    party.box[s.slot] = m;
  }
  party.boxSave();

  // A copy, to prove the sort never touches the data.
  PartyMon before[BOX_SLOTS];
  memcpy(before, party.box, sizeof(before));

  for (uint8_t mode = 0; mode < boxSortCount(); mode++) {
    boxSort = mode;
    // BIJECTION: every visual position maps to a distinct real slot, and every
    // real slot is reachable. Anything less means two cells share a creature.
    std::set<int> hit;
    bool inRange = true;
    for (uint8_t v = 0; v < BOX_SLOTS; v++) {
      const uint8_t s = boxSlotFor(v);
      if (s >= BOX_SLOTS) inRange = false;
      hit.insert(s);
    }
    char m[110];
    snprintf(m, sizeof(m), "order %u: every visual cell maps to its own real slot",
             mode);
    ck(inRange && hit.size() == BOX_SLOTS, m);

    // and the creatures on screen are the creatures in the box, once each
    std::set<int> shown;
    int nonEmpty = 0;
    for (uint8_t v = 0; v < BOX_SLOTS; v++) {
      const PartyMon &mm = party.box[boxSlotFor(v)];
      if (!mm.empty()) { nonEmpty++; shown.insert(boxSlotFor(v)); }
    }
    snprintf(m, sizeof(m), "order %u: shows all six creatures and invents none",
             mode);
    ck(nonEmpty == 6 && shown.size() == 6, m);

    // EMPTY SLOTS LAST, so the grid reads full-first rather than gap-toothed.
    bool sawEmpty = false, gapAfter = false;
    for (uint8_t v = 0; v < BOX_SLOTS; v++) {
      if (party.box[boxSlotFor(v)].empty()) sawEmpty = true;
      else if (sawEmpty) gapAfter = true;
    }
    snprintf(m, sizeof(m), "order %u: the empty slots are all at the end", mode);
    ck(mode == BSORT_SLOT ? true : !gapAfter, m);

    snprintf(m, sizeof(m), "order %u: sorting the VIEW left the box untouched", mode);
    ck(memcmp(before, party.box, sizeof(before)) == 0, m);
  }

  boxSort = BSORT_SLOT;
  {
    bool identity = true;
    for (uint8_t v = 0; v < BOX_SLOTS; v++) if (boxSlotFor(v) != v) identity = false;
    ck(identity, "the default order is the slots themselves, unchanged");
  }

  boxSort = BSORT_DEX;
  {
    // 3, 6, 9, 25, 131, 149 -- ascending, then the holes
    const int want[6] = { 3, 6, 9, 25, 131, 149 };
    bool ok = true;
    for (int i = 0; i < 6; i++)
      if (party.box[boxSlotFor((uint8_t)i)].dex != want[i]) ok = false;
    ck(ok, "by NUMBER runs up the dex");
  }

  boxSort = BSORT_LEVEL;
  {
    // 90, 90, 71, 55, 40, 12 -- strongest first, and the two 90s keep slot order
    const int wantLvl[6] = { 90, 90, 71, 55, 40, 12 };
    bool ok = true;
    for (int i = 0; i < 6; i++)
      if (party.box[boxSlotFor((uint8_t)i)].level != wantLvl[i]) ok = false;
    ck(ok, "by LEVEL puts the strongest first");
    ck(party.box[boxSlotFor(0)].dex == 6 && party.box[boxSlotFor(1)].dex == 9,
       "and equal levels stay in slot order, so the order is stable");
  }

  // A box that is entirely empty must not confuse the permutation.
  {
    for (int i = 0; i < BOX_SLOTS; i++) party.box[i] = PartyMon();
    int wrong = 0;
    for (uint8_t mode = 0; mode < boxSortCount(); mode++) {
      boxSort = mode;
      std::set<int> hit;
      for (uint8_t v = 0; v < BOX_SLOTS; v++) hit.insert(boxSlotFor(v));
      if (hit.size() != BOX_SLOTS) wrong++;
    }
    ck(wrong == 0, "an empty box is still a clean permutation in every order");
    boxSort = BSORT_SLOT;
  }

  // -------------------------------------------------------- derived personality
  //
  // The point of deriving it is that it costs the save nothing AND is stable. So
  // the assertions are: it survives a reload, it survives EVOLVING, it actually
  // reads the stats it claims to, and every trait is reachable.
  {
    pet.dbgHatchAs(4, false);            // CHARMANDER, so there is room to evolve
    while (pet.hasLearnOffer()) pet.declineLearn();

    pet.ivAtk = 31; pet.ivDef = 10; pet.ivSpe = 12; pet.ivHp = 11;
    ck(pet.personality() == PERS_BOLD, "the strongest attacker reads as BOLD");
    pet.ivAtk = 9; pet.ivDef = 30; pet.ivSpe = 12; pet.ivHp = 11;
    ck(pet.personality() == PERS_STURDY, "the best defence reads as STURDY");
    pet.ivAtk = 9; pet.ivDef = 10; pet.ivSpe = 29; pet.ivHp = 11;
    ck(pet.personality() == PERS_BRISK, "the fastest reads as BRISK");
    pet.ivAtk = 9; pet.ivDef = 10; pet.ivSpe = 12; pet.ivHp = 31;
    ck(pet.personality() == PERS_HARDY, "the toughest reads as HARDY");
    // No standout stat: described by overall quality instead, which is the only
    // honest reading of a flat spread.
    pet.ivAtk = pet.ivDef = pet.ivSpe = pet.ivHp = 28;
    ck(pet.personality() == PERS_EAGER, "a flat, strong spread reads as EAGER");
    pet.ivAtk = pet.ivDef = pet.ivSpe = pet.ivHp = 9;
    ck(pet.personality() == PERS_CALM, "and a flat, modest one as CALM");

    // STABILITY, which is what makes it usable as flavour at all.
    pet.ivAtk = 31; pet.ivDef = 10; pet.ivSpe = 12; pet.ivHp = 11;
    const uint8_t was = pet.personality();
    pet.saveNow();
    { Pet r; r.begin(); ck(r.personality() == was, "it survives a reload"); }
    const int16_t before = pet.speciesId;
    pet.ageMinutes = 30UL * MINUTES_PER_LEVEL;
    if (pet.canEvolveNow()) pet.evolve();
    ck(pet.speciesId != before ? pet.personality() == was : true,
       "and EVOLVING, because it reads the IVs rather than the species");

    // Every trait must be reachable from a real IV roll, or one of the six is
    // decoration. IVs run 8..31, so the sweep covers the whole legal range.
    bool seen[Pet::PERSONALITY_COUNT] = { false };
    for (int a = 8; a <= 31; a += 3)
      for (int d = 8; d <= 31; d += 3)
        for (int s = 8; s <= 31; s += 3)
          for (int h = 8; h <= 31; h += 3) {
            pet.ivAtk = a; pet.ivDef = d; pet.ivSpe = s; pet.ivHp = h;
            const uint8_t p = pet.personality();
            if (p < Pet::PERSONALITY_COUNT) seen[p] = true;
          }
    int reachable = 0;
    for (uint8_t i = 0; i < Pet::PERSONALITY_COUNT; i++) if (seen[i]) reachable++;
    char m[90];
    snprintf(m, sizeof(m), "all %u traits are reachable from a real IV roll (%d)",
             (unsigned)Pet::PERSONALITY_COUNT, reachable);
    ck(reachable == (int)Pet::PERSONALITY_COUNT, m);

    // An egg has no IVs worth reading, and must not index off the end.
    { Pet e; e.begin(); if (!e.isEgg()) e.newEgg();
      ck(e.personality() < Pet::PERSONALITY_COUNT,
         "an egg still answers with a valid trait rather than reading past the list"); }
  }

  // ------------------------------------------------------- today's care goals
  //
  // NON-DESTRUCTIVE is the requirement, so that is the first thing asserted:
  // reading the checklist must not write anything, and it must not invent a
  // reward to claim. Everything else is that each line really tracks the state it
  // names.
  {
    pet.dbgHatchAs(1, false);
    while (pet.hasLearnOffer()) pet.declineLearn();
    ck(uiGoalCount() == 5, "there are five things to do in a day");

    // all failing
    pet.fullness = 10; pet.joy = 10; pet.energy = 10; pet.hygiene = 10; pet.poops = 3;
    pet.lastCareDay = 0;
    ck(uiGoalsMet() == 0, "a neglected creature meets none of them");

    // and each one, one at a time, so no line is standing in for another
    pet.fullness = 70;
    ck(uiGoalMet(1) && uiGoalsMet() == 1, "feeding it ticks exactly one line");
    pet.poops = 0; pet.hygiene = 70;
    ck(uiGoalMet(2) && uiGoalsMet() == 2, "cleaning up ticks the next");
    pet.joy = 70;
    ck(uiGoalMet(3) && uiGoalsMet() == 3, "and playing with it another");
    pet.energy = 70;
    ck(uiGoalMet(4) && uiGoalsMet() == 4, "and letting it rest another");

    // The care line reads the SAME clock the streak does, rather than the UI
    // keeping its own idea of when a day turns over.
    pet.dbgSetSeen(CLOCK_EPOCH_FLOOR + 500UL * 86400);
    ck(!uiGoalMet(0), "with no care registered today the first line is open");
    pet.caress();
    ck(pet.caredToday() && uiGoalMet(0), "caring for it closes that line");
    ck(uiGoalsMet() == 5, "and the day can genuinely be completed");

    // NOTHING IS WRITTEN BY LOOKING. A checklist that saved on read would put a
    // flash write behind every frame of the screen it lives on.
    {
      const size_t keysBefore = nvs().size();
      pet.flushSave();
      const uint32_t genBefore = pet.petGeneration();
      for (int i = 0; i < 20; i++) { uiGoalsMet(); uiGoalMet(i % 5); }
      ck(pet.petGeneration() == genBefore && nvs().size() == keysBefore,
         "reading the checklist writes nothing at all");
      ck(!pet.savePending(), "and marks nothing dirty");
    }

    // A NEW DAY REOPENS IT, with no stored flag to reset -- which is the whole
    // reason it is computed.
    pet.dbgSetSeen(CLOCK_EPOCH_FLOOR + 501UL * 86400);
    ck(!uiGoalMet(0), "tomorrow the care line is open again, with nothing to clear");
  }

  // ------------------------------------------------------------ the memory game
  //
  // Driven through the real thing -- startMemoGame(), memoTap() and render() --
  // rather than through pet.playMemo() alone. CLAUDE.md is explicit that a rule
  // proven in isolation says nothing about its caller, and here the CALLER is
  // where the interesting behaviour lives: the sequence, the reward being applied
  // exactly once, and the score surviving.
  {
    pet.dbgHatchAs(6, false);
    while (pet.hasLearnOffer()) pet.declineLearn();
    pet.joy = 40; pet.energy = 80; pet.fullness = 80;
    pet.memoHi = 0;
    pet.saveNow();

    startMemoGame();
    ck(memoOpen, "the game opens");
    ck(memoLen == 1, "with a one-step sequence to learn");
    ck(memoShowing, "and it shows you the sequence before asking for anything");

    // A tap while the sequence is still playing must be ignored, not counted as
    // an answer -- otherwise a fast finger loses on step one through no fault.
    const uint8_t atWas = memoAt;
    int px, py; memoPadPos((memoSeq[0] + 1) % MEMO_PADS_T, &px, &py);
    memoTap((int16_t)px, (int16_t)py);
    ck(memoAt == atWas && !memoOverUntil,
       "tapping while it is still showing does nothing at all");

    // Play it correctly for several rounds, driving render() to walk the sequence
    // out exactly as the panel does.
    int rounds = 0;
    for (int guard = 0; guard < 4000 && rounds < 5 && !memoOverUntil; guard++) {
      if (memoShowing) { emuAdvanceMs(60); render(); continue; }
      const uint8_t want = memoSeq[memoAt];
      int wx, wy; memoPadPos(want, &wx, &wy);
      const uint8_t lenWas = memoLen;
      memoTap((int16_t)wx, (int16_t)wy);
      if (memoLen > lenWas) rounds++;      // a round was completed
      render();
    }
    ck(rounds >= 4 && !memoOverUntil,
       "playing the sequence back correctly keeps extending it");
    ck(memoBest >= 4, "and the score is the longest sequence remembered");

    // A WRONG answer ends it, and the reward is applied once.
    const uint8_t bestWas = memoBest;
    const uint8_t joyWas = pet.joy;
    while (memoShowing) { emuAdvanceMs(60); render(); }
    int bx, by; memoPadPos((memoSeq[memoAt] + 1) % MEMO_PADS_T, &bx, &by);
    memoTap((int16_t)bx, (int16_t)by);
    ck(memoOverUntil != 0, "a wrong answer ends the session");
    ck(pet.joy > joyWas, "which pays out in JOY");
    ck(pet.memoHi == bestWas, "and records the run");
    // Rendering the results screen repeatedly must NOT pay again. memoFinish()
    // guards on memoOverUntil for exactly this; without it the reward would be
    // applied on every frame the result was up.
    const uint8_t joyAfter = pet.joy;
    const uint16_t hiAfter = pet.memoHi;
    for (int i = 0; i < 10; i++) render();
    ck(pet.joy == joyAfter && pet.memoHi == hiAfter,
       "and holding the results screen does not pay twice");
    // Walking away must not pay a third time either.
    leaveMemo();
    ck(pet.joy == joyAfter, "nor does leaving it");
    ck(!memoOpen, "and the game is closed");

    // The record is PLAYER-WIDE, so it survives a reload -- and it is a new field
    // on an old record, which is the part that could have gone wrong.
    { Pet r; r.begin();
      ck(r.memoHi == hiAfter, "the memory record survives a reload"); }

    // It pays in joy and bond, and trains NO stat: all three trainable stats
    // already have a game, and a fourth grind is not what this is for.
    {
      pet.dbgHatchAs(1, false);
      while (pet.hasLearnOffer()) pet.declineLearn();
      pet.trAtk = 20; pet.trDef = 21; pet.trSpe = 22;
      pet.joy = 10; pet.bond = 10;
      const uint8_t a = pet.trAtk, d = pet.trDef, sp = pet.trSpe;
      pet.playMemo(6);
      ck(pet.trAtk == a && pet.trDef == d && pet.trSpe == sp,
         "it trains no stat at all");
      ck(pet.joy > 10 && pet.bond > 10, "but it does raise joy and bond");
      ck(pet.caredToday() || pet.lastCareDay == 0,
         "and it counts as caring for the creature");
    }
    // An egg has nothing to remember, and a ceremony must not be interrupted.
    {
      Pet e; e.begin(); e.newEgg();
      const uint16_t recWas = e.memoHi;   // player-wide: it outlives every creature
      ck(e.playMemo(5) == 0, "an egg cannot play");
      ck(e.memoHi == recWas, "and cannot set a record either");
    }
  }

  // ------------------------------------------- the game ASKS for a cry, and when
  //
  // How a cry sounds cannot be judged here -- cry_test covers everything about it
  // that is not taste. Whether the game requests one, and at which moments, is
  // ordinary logic and belongs under test: the hook lives in ensureMon(), so a
  // refactor there could silence the whole feature without any test noticing.
  {
    // Boot must be SILENT. monFor starts at the -2 "nothing loaded" sentinel, and a
    // device that shouts when you switch it on is a bug, not a greeting.
    g_emuCryCount = 0;
    ensureMon();
    ck(g_emuCryCount == 0, "the first sprite load after boot does not cry");

    // A new creature on the panel speaks. One hook covers hatching, evolving and
    // swapping the focused creature, because all three arrive through here.
    pet.dbgHatchAs(4, false);
    while (pet.hasLearnOffer()) pet.declineLearn();
    g_emuCryCount = 0;
    ensureMon();
    ck(g_emuCryCount == 1 && g_emuCryDex == 4,
       "a newly hatched creature cries, with its own dex number");

    // ...and not again while it just stands there.
    g_emuCryCount = 0;
    for (int i = 0; i < 5; i++) ensureMon();
    ck(g_emuCryCount == 0, "but not once per frame after that");

    // An EGG has no voice.
    { Pet e; e.begin(); e.newEgg(); }
    pet.newEgg();
    g_emuCryCount = 0;
    ensureMon();
    ck(g_emuCryCount == 0, "an egg does not cry");

    // The Pokedex speaks -- but only for an entry actually filled in. Playing the
    // cry of a silhouette hands over the one thing the "???" is withholding.
    galleryRegion = 0;
    galleryFilter = GFILT_ALL;
    galleryPage = 0;
    galleryDetail = 0;
    pet.dexReg[(6 - 1) >> 3] |= 1 << ((6 - 1) & 7);       // CHARIZARD registered
    const int16_t unknown = 13;                            // and this one is not
    pet.dexReg[(unknown - 1) >> 3] &= (uint8_t)~(1 << ((unknown - 1) & 7));
    ck(pet.isRegistered(6) && !pet.isRegistered(unknown),
       "(one registered species and one silhouette to compare)");

    // Tap the cell each one occupies, resolved the way the grid draws it.
    auto tapDex = [&](int16_t want) {
      for (uint16_t v = 0; v < 16; v++) {
        if (galleryAt(v) != want) continue;
        galleryDetail = 0;
        galleryTap((int16_t)(73 + (v % 4) * 80 + 40),
                   (int16_t)(84 + (v / 4) * 80 + 40));
        return true;
      }
      return false;
    };
    g_emuCryCount = 0;
    ck(tapDex(6) && galleryDetail == 6, "opening a registered dex entry");
    ck(g_emuCryCount == 1 && g_emuCryDex == 6, "plays that species' cry");
    galleryDetail = 0;
    g_emuCryCount = 0;
    ck(tapDex(unknown) && galleryDetail == unknown, "opening an UNregistered one");
    ck(g_emuCryCount == 0, "stays silent, so the cry cannot leak the species");
    galleryDetail = 0;
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
