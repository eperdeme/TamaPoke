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
#include "cry_gb.h"
#include "adpcm.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <map>
#include <set>
#include <string>
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

  // ---- Tier 2: Authentic Gen 1 & Gen 2 Game Boy cries (#1..#251)
  {
    int missing = 0, badLen = 0;
    for (int16_t d = 1; d <= CRY_DEX; d++) {
      if (!gbCryHas(d)) missing++;
      else {
        uint32_t ms = GbCryPlayer::lengthMs(d);
        if (ms == 0 || ms > 3000) badLen++;
      }
    }
    ck(missing == 0, "all 251 Gen 1-2 species have authentic GB cry data");
    ck(badLen == 0, "all 251 GB cries have bounded non-zero duration");
    ck(!gbCryHas(0) && !gbCryHas(CRY_DEX + 1) && !gbCryHas(DEX_COUNT),
       "Gen 3+ and out-of-range species fall back past GB cry table");

    int silentGb = 0, clipGb = 0;
    const int16_t gbSample[] = { 1, 4, 6, 7, 25, 94, 150, 152, 197, 249, 251 };
    for (int16_t d : gbSample) {
      GbCryPlayer gb;
      GbSynth syn;
      if (!gb.begin(d)) { silentGb++; continue; }
      int peak = 0, clipped = 0;
      uint32_t ms;
      int16_t buf[512];
      while ((ms = gb.step(syn)) != 0) {
        uint32_t left = (uint32_t)((uint64_t)ms * GB_RATE / 1000);
        while (left) {
          size_t take = left > 512 ? 512 : left;
          syn.render(buf, take, 7);
          for (size_t i = 0; i < take; i++) {
            int v = buf[i] < 0 ? -buf[i] : buf[i];
            if (v > peak) peak = v;
            if (v >= 32000) clipped++;
          }
          left -= take;
        }
      }
      if (peak < 200) silentGb++;
      if (clipped) clipGb++;
    }
    ck(silentGb == 0, "sampled Gen 1-2 GB cries render audible sound");
    ck(clipGb == 0, "and Gen 1-2 GB cries do not clip at default volume");
  }

  // ---- Tier 1: TCRY 16 kHz 4-bit IMA-ADPCM codec
  {
    const size_t N = 4000;
    std::vector<int16_t> src(N);
    for (size_t i = 0; i < N; i++)
      src[i] = (int16_t)(16000.0 * sin(2.0 * 3.14159265 * 440.0 * (double)i / 16000.0));
    uint8_t enc[CRY_ADPCM_HDR + (N + 1) / 2];
    size_t encLen = adpcmEncode(src.data(), N, enc, sizeof(enc));
    ck(encLen == sizeof(enc) && adpcmValidHeader(enc, (uint32_t)encLen),
       "adpcmEncode produces a valid TCRY container");

    AdpcmDec dec;
    ck(dec.init(enc, (uint32_t)encLen), "AdpcmDec accepts valid TCRY stream");
    int16_t out[N];
    size_t got = dec.decode(out, N, 10);
    ck(got == N && dec.done(), "AdpcmDec decodes the full sample count and marks done");
    double errSq = 0.0, sigSq = 0.0;
    for (size_t i = 0; i < N; i++) {
      double e = (double)src[i] - (double)out[i];
      errSq += e * e;
      sigSq += (double)src[i] * (double)src[i];
    }
    double snr = 10.0 * log10(sigSq / std::max(1.0, errSq));
    printf("      IMA-ADPCM round-trip SNR %.1f dB\n", snr);
    ck(snr > 25.0, "IMA-ADPCM round-trip keeps high SNR");

    int peakPrev = 0;
    bool monoVol = true;
    for (uint8_t vol = 0; vol <= 10; vol++) {
      AdpcmDec vdec;
      vdec.init(enc, (uint32_t)encLen);
      int16_t vbuf[N];
      vdec.decode(vbuf, N, vol);
      int peak = 0;
      for (size_t i = 0; i < N; i++) peak = std::max(peak, std::abs((int)vbuf[i]));
      if (vol == 0 && peak != 0) monoVol = false;
      if (vol > 0 && peak <= peakPrev) monoVol = false;
      peakPrev = peak;
    }
    ck(monoVol, "ADPCM volume 0 is silent and levels 1..10 increase monotonically");

    uint8_t badMagic[20] = {};
    memcpy(badMagic, enc, 20);
    badMagic[0] = 'X';
    ck(!adpcmValidHeader(badMagic, 20), "bad TCRY magic is rejected");
    uint8_t badRate[20] = {};
    memcpy(badRate, enc, 20);
    badRate[4] = 0x40; badRate[5] = 0x1F; // 8000 Hz
    ck(!adpcmValidHeader(badRate, 20), "unsupported sample rate is rejected");
    uint8_t badIdx[20] = {};
    memcpy(badIdx, enc, 20);
    badIdx[8] = 89;
    ck(!adpcmValidHeader(badIdx, 20), "out-of-range IMA step index is rejected");
    ck(!adpcmValidHeader(enc, CRY_ADPCM_HDR - 1), "truncated header is rejected");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
