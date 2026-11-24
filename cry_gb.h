#pragma once
#include <stdint.h>
#include "gbsynth.h"
#include "cries.h"

// Gen 1 and Gen 2 authentic cries (cries.h, #1-#251) played on GbSynth.
// Adapted from crenberries428/TamaPoke (MIT). Header-only with zero Arduino
// dependency so desktop tests and audioTask run the identical sequencer.
static inline bool gbCryHas(int16_t dex) {
  return dex >= 1 && dex <= CRY_DEX && CRY_MON[dex - 1].base < CRY_BASES &&
         (CRY_BASE_TBL[CRY_MON[dex - 1].base].n1 ||
          CRY_BASE_TBL[CRY_MON[dex - 1].base].n2 ||
          CRY_BASE_TBL[CRY_MON[dex - 1].base].nn);
}

class GbCryPlayer {
 public:
  static uint32_t frameMs(uint8_t frames, uint16_t tempo) {
    return (uint32_t)(((uint64_t)frames * tempo * 1000u) / (256u * 5973u / 100u));
  }

  bool begin(int16_t dex) {
    active = false;
    if (!gbCryHas(dex)) return false;
    const GbCryMon &m = CRY_MON[dex - 1];
    const GbCryBase &b = CRY_BASE_TBL[m.base];
    seq[0] = b.p1; n[0] = b.n1;
    seq[1] = b.p2; n[1] = b.n2;
    seq[2] = b.nz; n[2] = b.nn;
    for (int i = 0; i < 3; i++) { idx[i] = 0; at[i] = 0; }
    clock = 0;
    pitch = m.pitch;
    tempo = m.tempo;
    base = &b;
    active = true;
    return true;
  }

  uint32_t step(GbSynth &syn) {
    if (!active) return 0;
    uint32_t next = 0xFFFFFFFFu;
    bool any = false;
    for (int c = 0; c < 3; c++) {
      if (idx[c] > n[c]) continue;
      any = true;
      if (clock >= at[c]) {
        if (idx[c] >= n[c]) {
          idx[c] = (uint8_t)(n[c] + 1);
          continue;
        }
        const GbCryNote &e = seq[c][idx[c]++];
        uint32_t ms = (c == 2) ? frameMs(e.frames, 256) : frameMs(e.frames, tempo);
        if (!ms) ms = 1;
        if (c == 2) {
          if (e.vol) syn.noise(e.vol, e.envDir, e.envPeriod, ms, e.freq);
          else syn.silence(2);
        } else {
          int16_t set = c ? base->set2 : base->set1;
          uint16_t f = (uint16_t)((e.freq + (set >= 0 ? set : pitch)) & 0x7FF);
          if (e.vol) syn.note((uint8_t)c, f, e.duty, e.vol, e.envDir, e.envPeriod, ms);
          else syn.silence((uint8_t)c);
        }
        at[c] = clock + ms;
      }
      if (idx[c] <= n[c] && at[c] < next) next = at[c];
    }
    if (!any) { active = false; return 0; }
    if (next == 0xFFFFFFFFu) {
      uint32_t last = 0;
      for (int c = 0; c < 3; c++) if (at[c] > last) last = at[c];
      uint32_t tail = last > clock ? last - clock : 0;
      clock = last;
      active = false;
      return tail;
    }
    if (next <= clock) next = clock + 1;
    uint32_t d = next - clock;
    clock = next;
    return d;
  }

  bool done() const { return !active; }

  static uint32_t lengthMs(int16_t dex) {
    GbCryPlayer p;
    if (!p.begin(dex)) return 0;
    GbSynth s;
    uint32_t total = 0, d;
    while ((d = p.step(s)) != 0) total += d;
    return total;
  }

 private:
  const GbCryNote *seq[3] = { nullptr, nullptr, nullptr };
  uint8_t n[3] = { 0, 0, 0 };
  uint8_t idx[3] = { 0, 0, 0 };
  uint32_t at[3] = { 0, 0, 0 };
  uint32_t clock = 0;
  int16_t pitch = 0;
  uint16_t tempo = 256;
  const GbCryBase *base = nullptr;
  bool active = false;
};
