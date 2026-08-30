// The bag and the wild-encounter maths.
//
// Every function under test takes its roll as an argument, so these are pinned
// at the BOUNDARIES rather than at values that happen to hold. CLAUDE.md
// § "A test that proves the transcription rather than the firmware" records
// four bugs found in one session from assertions tied to a value instead of a
// rule -- one of which passed only because 12 % 4 came full circle.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "inventory.h"
#include "wild.h"
#include "noart.h"
#include <cstdio>
uint32_t g_seed=3; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  // ---- the bag -------------------------------------------------------------
  Inventory inv; inv.begin();
  ck(inv.count(IT_POKEBALL)==5 && inv.count(IT_POTION)==3,
     "a fresh bag gets the starter kit");
  ck(inv.count(ITEM_NONE)==0, "the filler entry is never carried");

  inv.add(IT_ULTRABALL, 2);
  ck(inv.count(IT_ULTRABALL)==2, "adding a new item stacks it");
  ck(inv.consume(IT_ULTRABALL, 2) && inv.count(IT_ULTRABALL)==0,
     "consuming the last of a stack empties it");
  ck(!inv.consume(IT_ULTRABALL, 1), "and an empty stack cannot be consumed");
  ck(!inv.consume(IT_POKEBALL, 99), "nor can more than is carried");

  // The cap has to CLIP rather than wrap: counts are uint8_t, and 99 + 200
  // overflowing to 43 would silently destroy a player's supply.
  ck(inv.add(IT_POKEBALL, 200) == BAG_STACK_MAX - 5,
     "a stack fills to the cap and reports what it took");
  ck(inv.count(IT_POKEBALL)==BAG_STACK_MAX, "and stops exactly there");
  ck(inv.add(IT_POKEBALL, 10)==0, "a full stack takes nothing more");

  // Growing ITEM_TBL must be additive, exactly like the Pokedex bitmaps: a blob
  // written before an item existed has to leave every earlier count in place.
  { Preferences seed; seed.begin("tamapoke", false);
    uint8_t oldBag[4] = { 0, 7, 0, 2 };   // a build that only knew 3 items
    seed.putBytes("bag", oldBag, sizeof(oldBag));
    seed.end(); }
  Inventory grown; grown.begin();
  ck(grown.count(IT_POKEBALL)==7 && grown.count(IT_ULTRABALL)==2,
     "a bag saved by a build with fewer items keeps every count");
  ck(grown.count(IT_MASTERBALL)==0, "and the items it never knew read as zero");
  ck(grown.count(IT_POTION)==0, "no starter kit is re-granted over a real save");

  // ---- weighted drops ------------------------------------------------------
  uint32_t total = grown.weightTotal();
  ck(total == 120, "the drop table weighs what the catalogue says");
  ck(grown.weightedDrop(0)==IT_POKEBALL, "roll 0 lands on the first weight");
  ck(grown.weightedDrop(total-1)==IT_CARBOS, "and the last roll on the last");
  // The Master Ball is weight 1 of 120: it must be reachable and must not be
  // reachable one roll either side, or "rare" is an accident of arithmetic.
  uint32_t master = 0;
  for (uint32_t r=0;r<total;r++) if (grown.weightedDrop(r)==IT_MASTERBALL) master++;
  ck(master==1, "the master ball is exactly one roll in the whole table");

  ItemKey ex[1] = { IT_POKEBALL };
  ck(grown.weightTotal(ex,1)==90, "excluding an item removes its weight");
  bool never=true;
  for (uint32_t r=0;r<90;r++) if (grown.weightedDrop(r,ex,1)==IT_POKEBALL) never=false;
  ck(never, "and it can no longer be drawn");

  // ---- shiny ---------------------------------------------------------------
  ck(wildShinyThreshold(0)==100, "the base shiny rate is exactly 1/4096");
  ck(wildShinyThreshold(1)==100+4096, "each bonus point is one percentage point");
  ck(wildShinyThreshold(99)==wildShinyThreshold(WILD_SHINY_BONUS_MAX),
     "and the bonus is clamped");
  ck(wildShinyForRoll(99,0) && !wildShinyForRoll(100,0),
     "the shiny roll flips at its threshold, not near it");
  { uint8_t a=31,d=5,s=0,h=25;
    wildApplyShiny(true,a,d,s,h);
    ck(a==31 && d==20 && s==20 && h==25,
       "a shiny floors every IV at 20 without capping a better one"); }
  { uint8_t a=1,d=1,s=1,h=1;
    wildApplyShiny(false,a,d,s,h);
    ck(a==1, "and does nothing at all when it is not shiny"); }

  // ---- capture -------------------------------------------------------------
  ck(wildCaptureChance(R_COMUN,100,100,false,ITEM_CATCH_GUARANTEED)==100,
     "the master ball sentinel never rolls");
  ck(wildCaptureChance(R_COMUN,0,100,false,ITEM_CATCH_GUARANTEED)==0,
     "but not even it can catch something already fainted");
  uint8_t full = wildCaptureChance(R_COMUN,100,100,false,100);
  uint8_t hurt = wildCaptureChance(R_COMUN,1,100,false,100);
  ck(hurt>full, "weakening a target raises the chance");
  ck(wildCaptureChance(R_COMUN,50,100,true,100) >
     wildCaptureChance(R_COMUN,50,100,false,100),
     "and so does an ailment");
  ck(wildCaptureChance(R_COMUN,50,100,false,200) >
     wildCaptureChance(R_COMUN,50,100,false,100),
     "a better ball beats a worse one at the same HP");
  ck(wildCaptureChance(R_LEGENDARIO,100,100,false,100) <
     wildCaptureChance(R_COMUN,100,100,false,100),
     "a legendary is harder than a common");
  ck(wildCaptureChance(R_COMUN,1,100,false,200)<=95,
     "and nothing but the sentinel ever reaches certainty");

  // ---- level band ----------------------------------------------------------
  ck(wildLevelMin(1,false)==1 && wildLevelMax(1,false)==6,
     "a level 1 player meets 1-6");
  ck(wildLevelMin(50,false)==45 && wildLevelMax(50,false)==55,
     "and a level 50 player meets 45-55");
  ck(wildLevelMax(100,false)==MAX_LEVEL, "the band never exceeds the level cap");
  ck(wildLevelMin(50,true)==1 && wildLevelMax(50,true)==MAX_LEVEL,
     "hard opens the whole ladder");

  // ---- drops per win -------------------------------------------------------
  ck(wildDropCount(false,29)==2 && wildDropCount(false,30)==1,
     "the bonus drop flips exactly at 30%");
  ck(wildDropCount(true,30)==2 && wildDropCount(true,29)==3,
     "hard grants two, plus the same bonus roll");

  // ---- escape --------------------------------------------------------------
  ck(wildEscapeChance(50,50)==90 && wildEscapeChance(50,10)==90,
     "fleeing something at or below your level is 90%");
  ck(wildEscapeChance(50,100)==45, "and scales down by the level ratio above it");
  ck(wildEscapeChance(1,100)==10, "with a 10% floor");
  ck(wildEscapeChance(50,0)==90, "a foe with no level cannot divide by zero");

  // The foe's curve is a TENT, and its shape is the whole design: a nearly
  // beaten creature must stop running. A monotonic curve would pass a naive
  // "low HP flees more" check, so both slopes are pinned.
  ck(wildFoeEscapeChance(100,100)==0, "a healthy foe never runs");
  ck(wildFoeEscapeChance(41,100)==0, "nor does one just above the 40% threshold");
  ck(wildFoeEscapeChance(40,100)==10, "it starts considering it at 40% HP");
  ck(wildFoeEscapeChance(20,100)==20, "peaks at 20% HP");
  ck(wildFoeEscapeChance(10,100)==10, "falls back to 10% at 10% HP");
  ck(wildFoeEscapeChance(1,100)==10, "and stays there below that");
  ck(wildFoeEscapeChance(0,100)==0, "a fainted foe is not running anywhere");

  // ---- tiers and the species pool -----------------------------------------
  ck(wildTierForRoll(0)==R_LEGENDARIO, "roll 0 is the legendary slice");
  ck(wildTierForRoll(1)==R_RARO && wildTierForRoll(7)==R_RARO,
     "1-7 is rare");
  ck(wildTierForRoll(8)==R_EVO && wildTierForRoll(29)==R_EVO,
     "8-29 is evolved");
  ck(wildTierForRoll(30)==R_COMUN && wildTierForRoll(255)==R_COMUN,
     "and everything above is common");

  // Nothing art-less may ever be met: a caught creature is kept forever, so one
  // that can only draw as a dex number would be a permanent defect. 600 rolls
  // across every tier, which is enough to reach the art-less bands in Unova.
  bool artOk=true, tierOk=true, inRange=true;
  for (uint32_t r=0;r<600;r++) {
    uint8_t tier = wildTierForRoll((uint8_t)(r % 100));
    int16_t d = wildPickSpecies(REGION_ALL, tier, r*7919u);
    if (!d) continue;
    if (!speciesHasArt(d)) artOk=false;
    if (DEX_TBL[d].rarity != tier) tierOk=false;
    if (d < 1 || d > DEX_COUNT) inRange=false;
  }
  ck(artOk, "no wild encounter is a species with no art");
  ck(tierOk, "and every one matches the tier that was rolled");
  ck(inRange, "and is a real dex number");

  // A region-locked roll must stay inside that region, or the pack gating that
  // CLAUDE.md § "Phase 2 landed: region gating" describes is decorative.
  bool kanto=true;
  for (uint32_t r=0;r<200;r++) {
    int16_t d = wildPickSpecies(0, R_COMUN, r*104729u);
    if (d && (d < REGIONS[0].lo || d > REGIONS[0].hi)) kanto=false;
  }
  ck(kanto, "a Kanto roll only ever returns a Kanto species");

  // Negative check: with every region locked out there is nothing to meet, and
  // the picker has to say so rather than returning a species anyway. Without
  // this the gating above could pass by never being consulted.
  uint16_t saved = gRegionArt;
  gRegionArt = 0;
  ck(wildPickSpecies(REGION_ALL, R_COMUN, 1)==0,
     "with no sprite pack installed there is nothing to encounter");
  gRegionArt = saved;
  ck(wildPickSpecies(REGION_ALL, R_COMUN, 1)!=0,
     "and the pool comes back when the packs do");

  printf(bad?"FAILED %d\n":"OK\n", bad);
  return bad?1:0;
}
