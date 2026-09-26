#pragma once
#include <stdint.h>
#include "dex.h"
#include "types.h"

// ---------------------------------------------------------------------------
// PER-SPECIES CHIRPS, DERIVED RATHER THAN SAMPLED.
//
// Every creature gets its own short sound, and NOT ONE BYTE OF IT IS RECORDED
// AUDIO. There is no cry data in this repository and none is downloaded: a cry is
// computed, here, from numbers already in flash -- the dex number, the typing, and
// the base stats in DEX_TBL. That is a deliberate licensing position as much as a
// technical one. The real games' cries are copyrighted assets; the sprite packs
// this project uses are separately licensed community art (see CREDITS.md), and
// there is no equivalent source for audio. So nothing is borrowed.
//
// It is also the shape the synthesiser was built for. gbsynth.h exposes two pulse
// voices with four duty cycles, volume envelopes and a noise channel, and has NO
// Arduino dependency on purpose -- so this file has none either. That is what lets
// cry_test render every one of the 1025 species and check them, on a machine with
// no board attached, which is otherwise impossible: CLAUDE.md is explicit that the
// emulator cannot judge audio, and it cannot judge how a cry SOUNDS. It can prove
// each one is audible, bounded, distinct and deterministic, which is the part that
// would otherwise only be discovered by flashing a board.
//
// The mapping is meant to be legible rather than clever:
//   BULK  (HP + DEF + SPD)  lowers the pitch. Big things sound big.
//   EDGE  (ATK + SPE + SPA) raises it and shortens the notes.
//   TYPE            picks the duty cycle -- thin and reedy for Bug and Electric,
//                   the fat 50% square for Fighting and Ground -- and decides
//                   whether a noise rattle is mixed in at all.
//   DEX NUMBER      picks the CONTOUR: which of a handful of shapes the pitch
//                   walks. It is the tie-breaker that keeps two species with
//                   similar stats and the same type from sounding identical.
//
// Everything is integer maths. No floats, no tables, no allocation: this runs on
// the audio task, whose stack is 4 KB.

// One step of a cry. Mirrors what GbSynth::note()/noise() take, so the player in
// audio.cpp is a loop and nothing has to be translated twice.
struct CryNote {
  uint16_t gbFreq;      // in the Game Boy's own units; 0 with noise = a rattle
  uint8_t duty;         // 0..3 -> 12.5 / 25 / 50 / 75 %
  uint8_t vol;          // 0..15
  int8_t envDir;        // -1 fades, +1 swells, 0 holds
  uint8_t envPeriod;
  uint16_t ms;
  uint8_t noise;        // 1 = percussion instead of a pulse
  uint16_t noisePeriod; // samples between LFSR clocks; small is bright
};

// Four steps is enough for a recognisable shape and short enough to stay inside
// the budget below. A fifth would push the longest cry past it.
#define CRY_NOTES 4
// An effect BLOCKS the audio task for its whole length and cuts across the music,
// which caps itself at 60 ms chunks to stay responsive. A cry that outstayed this
// would be heard as the music stuttering.
#define CRY_MAX_MS 300
// Never silent: a cry at volume 0 is a species with no voice, which reads as a bug.
#define CRY_MIN_VOL 6

// Hz -> the Game Boy's frequency register. Returns 0 for anything the chip cannot
// express, which the caller treats as "skip this step" rather than as silence.
static inline uint16_t cryHzToGb(uint16_t hz) {
  if (hz < 70) return 0;                       // f would go negative
  const int32_t f = 2048 - (int32_t)(131072u / hz);
  return (f < 8 || f > 2040) ? 0 : (uint16_t)f;
}

// The species' voice, in Hz, before the contour moves it. Bulk drags it down and
// edge pulls it up, then it is clamped into a band that is audible on a small
// speaker at both ends.
static inline uint16_t cryBaseHz(int16_t dex) {
  const DexEntry &e = DEX_TBL[dex];
  const int bulk = (int)e.bHp + e.bDef + e.bSpD;      // ~60..600
  const int edge = (int)e.bAtk + e.bSpe + e.bSpA;
  // 900 is the middle of the band. A heavy, sluggish creature lands near 200; a
  // light fast one near 1500.
  int hz = 900 - (bulk - edge) * 2;
  // The dex number detunes it slightly, so two species with identical stats and
  // typing are still not the same note.
  hz += (int)(dex % 17) * 7 - 56;
  if (hz < 190) hz = 190;
  if (hz > 1500) hz = 1500;
  return (uint16_t)hz;
}

// The duty cycle a type speaks with. 0 is thin and reedy, 2 is the fat square.
static inline uint8_t cryDutyFor(uint8_t type) {
  switch (type) {
    case T_BUG: case T_ELECTRIC: case T_FAIRY: case T_FLYING: return 0;
    case T_PSYCHIC: case T_GHOST: case T_ICE: case T_WATER:   return 1;
    case T_FIGHTING: case T_GROUND: case T_ROCK: case T_STEEL: return 2;
    default: return 3;
  }
}

// Which types rattle. Percussion is what makes a sound read as an impact rather
// than a tone, so it belongs to the heavy, mineral types -- and it is mixed in as
// ONE step rather than under the whole cry, which would just be mud.
static inline bool cryRattles(uint8_t t1, uint8_t t2) {
  const uint8_t heavy[] = { T_ROCK, T_GROUND, T_STEEL };
  for (uint8_t h : heavy)
    if (t1 == h || t2 == h) return true;
  return false;
}

// Fills up to CRY_NOTES steps and returns how many were written. Deterministic:
// the same dex always yields the same bytes, which is what makes a cry an
// identity rather than a noise generator.
static inline uint8_t crySynth(int16_t dex, CryNote *out) {
  if (dex < 1 || dex > DEX_COUNT || !out) return 0;
  const DexEntry &e = DEX_TBL[dex];
  const uint16_t base = cryBaseHz(dex);
  const uint8_t duty = cryDutyFor(e.type1);
  const bool rattle = cryRattles(e.type1, e.type2);
  // Faster creatures get shorter, sharper steps.
  //
  // The UPPER BOUND is derived, not chosen: the shape is three steps plus a tail
  // of one and a half, so the total is 4.5 * step, and 64 keeps even the slowest
  // creature at 288 ms -- inside CRY_MAX_MS with room to spare. At 82 the budget
  // clamp below was actually biting, which silently cut the tail off every sluggish
  // species and flattened the contour it exists to produce. The clamp stays as a
  // backstop; it should simply never fire.
  uint16_t step = (uint16_t)(70 - (int)e.bSpe / 6);
  if (step < 40) step = 40;
  if (step > CRY_MAX_MS * 2 / 9) step = CRY_MAX_MS * 2 / 9;   // 4.5 * step <= budget

  // Five contours, chosen by the dex number. RISE and FALL are the two obvious
  // ones; the rest exist so a run of consecutive dex numbers -- which tends to be
  // one evolution line, sharing a type and similar stats -- does not come out as
  // the same shape four times.
  const uint8_t shape = (uint8_t)(dex % 5);
  static const int8_t CONTOUR[5][CRY_NOTES] = {
    {  0,  4,  9, 14 },   // rise
    {  0, -4, -9, -14 },  // fall
    {  0,  7, -3,  4 },   // warble up
    {  0, -6,  3, -9 },   // warble down
    {  0, 10,  2, -6 },   // yelp: jump, then settle
  };

  uint8_t n = 0;
  uint16_t total = 0;
  for (uint8_t i = 0; i < CRY_NOTES; i++) {
    // A semitone is a factor of ~1.0595; approximated as 6% per step in integer
    // maths, which is close enough for a chirp and needs no float or table.
    int hz = (int)base;
    const int8_t s = CONTOUR[shape][i];
    for (int8_t k = 0; k < s; k++) hz = hz + hz / 17;      // up
    for (int8_t k = 0; k > s; k--) hz = hz - hz / 17;      // down
    if (hz < 70) hz = 70;
    if (hz > 4000) hz = 4000;
    const uint16_t f = cryHzToGb((uint16_t)hz);
    if (!f) continue;                       // outside the chip's range: drop it

    uint16_t ms = step;
    if (i == CRY_NOTES - 1) ms = (uint16_t)(step + step / 2);   // land on the tail
    if (total + ms > CRY_MAX_MS) ms = (uint16_t)(CRY_MAX_MS - total);
    if (ms < 20) break;                     // no budget left for a real step

    CryNote &c = out[n];
    c.gbFreq = f;
    c.duty = duty;
    // Loud enough to hear, quieter as the cry tails off so it decays rather than
    // stopping dead.
    const int v = 13 - i * 2;
    c.vol = (uint8_t)(v < CRY_MIN_VOL ? CRY_MIN_VOL : v);
    c.envDir = -1;
    c.envPeriod = 4;
    c.ms = ms;
    // The rattle rides the LAST step, where it reads as the creature landing
    // rather than as interference across the whole sound.
    c.noise = (rattle && i == CRY_NOTES - 1) ? 1 : 0;
    c.noisePeriod = (uint16_t)(6 + (dex % 5) * 3);
    n++;
    total = (uint16_t)(total + ms);
    if (total >= CRY_MAX_MS) break;
  }
  return n;
}

// How long a cry lasts, so a caller can decide whether it has room for one
// without building it twice.
static inline uint16_t cryLengthMs(int16_t dex) {
  CryNote n[CRY_NOTES];
  const uint8_t c = crySynth(dex, n);
  uint16_t ms = 0;
  for (uint8_t i = 0; i < c; i++) ms = (uint16_t)(ms + n[i].ms);
  return ms;
}
