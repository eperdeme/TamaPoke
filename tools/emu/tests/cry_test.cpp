// PER-SPECIES CHIRPS, checked at the layer that can actually be checked.
//
// CLAUDE.md is blunt that the emulator cannot judge audio, and that is true of how
// a cry SOUNDS -- nobody can tell from a test whether Charizard is convincing.
// What is not a matter of taste, and what would otherwise only be discovered by
// flashing a board and listening to 1025 creatures one at a time:
//
//   * every species produces SOMETHING, so no creature is mute;
//   * nothing exceeds the time budget, because a cry blocks the audio task and
//     overstaying it is heard as the music stuttering;
//   * every note is inside the chip's range, so none is silently dropped;
//   * the same species always yields the SAME cry, which is what makes it an
//     identity rather than a noise generator;
//   * different species mostly differ, or the whole feature is one sound;
//   * and it RENDERS -- real samples, not silence and not clipping -- through the
//     actual GbSynth, which is the only end-to-end proof available here.
//
// It links gbsynth.cpp and nothing else. cry.h is header-only and has no Arduino
// dependency, deliberately, which is what makes this possible at all.
#include "gbsynth.h"
#include "cry.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <map>
#include <set>
#include <vector>

static int bad = 0;
static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

// Renders one cry the way audio.cpp does and reports what came out.
struct Rendered {
  int samples = 0;
  int peak = 0;
  int clipped = 0;
  double energy = 0;
};
static Rendered renderCry(int16_t dex) {
  CryNote n[CRY_NOTES];
  const uint8_t count = crySynth(dex, n);
  GbSynth syn;
  Rendered r;
  int16_t buf[512];
  for (uint8_t i = 0; i < count; i++) {
    if (n[i].noise)
      syn.noise(n[i].vol, n[i].envDir, n[i].envPeriod, n[i].ms, n[i].noisePeriod);
    else
      syn.note(0, n[i].gbFreq, n[i].duty, n[i].vol, n[i].envDir, n[i].envPeriod,
               n[i].ms);
    uint32_t left = (uint32_t)((uint64_t)n[i].ms * GB_RATE / 1000);
    while (left) {
      const size_t take = left > 512 ? 512 : left;
      syn.render(buf, take, 10);
      for (size_t s = 0; s < take; s++) {
        const int v = buf[s] < 0 ? -buf[s] : buf[s];
        if (v > r.peak) r.peak = v;
        if (v >= 32000) r.clipped++;
        r.energy += (double)v;
        r.samples++;
      }
      left -= take;
    }
  }
  return r;
}

// A cry's bytes, for comparing two species and for proving determinism.
static std::string fingerprint(int16_t dex) {
  CryNote n[CRY_NOTES];
  const uint8_t c = crySynth(dex, n);
  std::string s;
  char b[64];
  for (uint8_t i = 0; i < c; i++) {
    snprintf(b, sizeof(b), "%u:%u:%u:%d:%u:%u:%u:%u|", n[i].gbFreq, n[i].duty,
             n[i].vol, (int)n[i].envDir, n[i].envPeriod, n[i].ms, n[i].noise,
             n[i].noisePeriod);
    s += b;
  }
  return s;
}

int main() {
  // ---- EVERY species, without exception
  {
    int mute = 0, tooLong = 0, badFreq = 0, quiet = 0, noNotes = 0;
    uint16_t longest = 0;
    int16_t longestDex = 0;
    for (int16_t d = 1; d <= DEX_COUNT; d++) {
      CryNote n[CRY_NOTES];
      const uint8_t c = crySynth(d, n);
      if (!c) { noNotes++; continue; }
      uint16_t total = 0;
      for (uint8_t i = 0; i < c; i++) {
        total = (uint16_t)(total + n[i].ms);
        // 0 means the caller must SKIP the step, so it must never be handed out.
        if (!n[i].noise && (n[i].gbFreq < 8 || n[i].gbFreq > 2040)) badFreq++;
        if (n[i].vol < CRY_MIN_VOL || n[i].vol > 15) quiet++;
        if (n[i].duty > 3) badFreq++;
      }
      if (total == 0) mute++;
      if (total > CRY_MAX_MS) tooLong++;
      if (total > longest) { longest = total; longestDex = d; }
    }
    printf("      %d species, longest cry %u ms (dex %d), budget %u ms\n",
           (int)DEX_COUNT, longest, (int)longestDex, (unsigned)CRY_MAX_MS);
    ck(noNotes == 0, "every species in the dex produces a cry");
    ck(mute == 0, "and none of them is zero-length");
    ck(tooLong == 0, "none exceeds the time budget the audio task allows");
    // STRICTLY under, not merely at. Landing exactly on the budget means the clamp
    // in crySynth() is truncating the final note, which cuts the tail off the
    // contour the cry is built around -- it passes a "not too long" check while
    // quietly flattening every slow creature. The step length is derived from the
    // budget so this never has to happen.
    ck(longest < CRY_MAX_MS,
       "and none is TRUNCATED by the budget: the shape always completes");
    ck(badFreq == 0, "every note is inside the chip's range, so none is dropped");
    ck(quiet == 0, "and every note is loud enough to hear");
  }

  // ---- out-of-range input must be refused, not indexed
  {
    CryNote n[CRY_NOTES];
    ck(crySynth(0, n) == 0, "dex 0 produces nothing rather than reading DEX_TBL[0]");
    ck(crySynth(-5, n) == 0, "and neither does a negative");
    ck(crySynth(DEX_COUNT + 1, n) == 0, "nor one past the end of the dex");
    ck(crySynth(30000, n) == 0, "nor a wildly out-of-range number");
    ck(crySynth(1, nullptr) == 0, "and a null destination is refused");
  }

  // ---- DETERMINISM. A cry is an identity; if it drifts it is just noise.
  {
    int drift = 0;
    for (int16_t d = 1; d <= DEX_COUNT; d++)
      if (fingerprint(d) != fingerprint(d)) drift++;
    ck(drift == 0, "the same species always yields exactly the same cry");
    // and it does not depend on call ORDER, which a hidden static would break
    const std::string a = fingerprint(25);
    for (int16_t d = 1; d <= 400; d++) (void)fingerprint(d);
    ck(fingerprint(25) == a, "and building a thousand others does not change it");
  }

  // ---- DISTINCTNESS. Not uniqueness -- 1025 species from four notes cannot all
  // differ, and pretending otherwise would mean inventing entropy that says
  // nothing about the creature. What matters is that the feature is not one sound
  // wearing 1025 names, and that NEIGHBOURS differ, since consecutive dex numbers
  // are usually one evolution line and get compared directly.
  {
    std::map<std::string, int> seen;
    for (int16_t d = 1; d <= DEX_COUNT; d++) seen[fingerprint(d)]++;
    int worst = 0;
    for (auto &kv : seen) if (kv.second > worst) worst = kv.second;
    printf("      %d distinct cries across %d species, largest collision %d\n",
           (int)seen.size(), (int)DEX_COUNT, worst);
    ck((int)seen.size() > DEX_COUNT / 2,
       "well over half the dex has a cry of its own");
    ck(worst <= 12, "and no single cry is shared by a crowd");

    int sameAsNeighbour = 0;
    for (int16_t d = 2; d <= DEX_COUNT; d++)
      if (fingerprint(d) == fingerprint(d - 1)) sameAsNeighbour++;
    printf("      consecutive species sounding identical: %d\n", sameAsNeighbour);
    ck(sameAsNeighbour == 0,
       "no two consecutive species sound identical, so an evolution line changes");
  }

  // ---- IT READS THE CREATURE. The mapping is only worth having if it is true:
  // a bulky creature really should sound lower than a fast light one.
  {
    // WAILORD (321) is the bulkiest thing in the dex; NINJASK (291) is the fastest.
    const uint16_t bulky = cryBaseHz(321), quick = cryBaseHz(291);
    printf("      WAILORD %u Hz vs NINJASK %u Hz\n", bulky, quick);
    ck(bulky < quick, "the bulkiest creature speaks lower than the fastest");
    // and the whole band is used rather than everything piling into the middle
    uint16_t lo = 0xFFFF, hi = 0;
    for (int16_t d = 1; d <= DEX_COUNT; d++) {
      const uint16_t hz = cryBaseHz(d);
      if (hz < lo) lo = hz;
      if (hz > hi) hi = hz;
    }
    printf("      pitch band actually used: %u..%u Hz\n", lo, hi);
    ck(hi - lo > 600, "and the dex spreads across a real range of pitches");
  }

  // ---- and it comes out of the SYNTH as audible, unclipped sound.
  //
  // This is the end-to-end half: crySynth() could produce perfectly sensible
  // numbers that GbSynth renders as silence, and only rendering catches that.
  {
    int silent = 0, clipping = 0, tooShort = 0;
    const int16_t sample[] = { 1, 6, 25, 94, 131, 150, 249, 384, 493, 649,
                               721, 809, 830, 887, 905, 950, 1000, DEX_COUNT };
    for (int16_t d : sample) {
      const Rendered r = renderCry(d);
      if (r.samples < GB_RATE / 40) tooShort++;          // under 25 ms of audio
      if (r.peak < 200) silent++;
      if (r.clipped) clipping++;
    }
    ck(silent == 0, "a rendered cry is actually audible, not silence");
    ck(clipping == 0, "and does not clip");
    ck(tooShort == 0, "and is long enough to be heard");

    // master volume 0 must be true silence, the same rule synth_test holds the
    // rest of the synth to
    CryNote n[CRY_NOTES];
    const uint8_t c = crySynth(25, n);
    GbSynth syn;
    int16_t buf[256];
    syn.note(0, n[0].gbFreq, n[0].duty, n[0].vol, n[0].envDir, n[0].envPeriod,
             n[0].ms);
    syn.render(buf, 256, 0);
    int peak = 0;
    for (int i = 0; i < 256; i++) peak = std::max(peak, std::abs((int)buf[i]));
    ck(c > 0 && peak == 0, "and a muted device renders a cry as true silence");
  }

  // ---- the type mapping is total: no type falls through to an invalid duty
  {
    int badDuty = 0;
    for (uint8_t t = 0; t < TYPE_COUNT; t++)
      if (cryDutyFor(t) > 3) badDuty++;
    ck(badDuty == 0, "every type maps to a real duty cycle");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
