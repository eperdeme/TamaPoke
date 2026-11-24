// Renders the synth to a .wav so audio can be judged WITHOUT a board.
//
// Until now audio was the one thing nobody could check here: the emulator stubs
// sfxPlay() to silence, so the only way to hear a change was to flash a board
// and listen, which is a terrible loop for something you want to iterate on.
// This turns that into: run a command, double-click a file.
//
//   tools/emu/tamapoke-emu --wav out.wav --demo duty
//
// It drives the same GbSynth the firmware uses, so what comes out of the
// speaker should be what comes out of this file.
#include "../../gbsynth.h"
#include "../../music.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

static void put32(FILE *f, uint32_t v) { fwrite(&v, 4, 1, f); }
static void put16(FILE *f, uint16_t v) { fwrite(&v, 2, 1, f); }

static void writeWav(const char *path, const std::vector<int16_t> &s) {
  FILE *f = fopen(path, "wb");
  if (!f) { fprintf(stderr, "cannot write %s\n", path); return; }
  uint32_t bytes = (uint32_t)(s.size() * 2);
  fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVE", 1, 4, f);
  fwrite("fmt ", 1, 4, f); put32(f, 16); put16(f, 1); put16(f, 1);
  put32(f, GB_RATE); put32(f, GB_RATE * 2); put16(f, 2); put16(f, 16);
  fwrite("data", 1, 4, f); put32(f, bytes);
  fwrite(s.data(), 2, s.size(), f);
  fclose(f);
  printf("wrote %s (%.1f s, %zu samples)\n", path,
         (double)s.size() / GB_RATE, s.size());
}

// Renders whatever the synth currently has queued for `ms`, appending.
static void run(GbSynth &syn, std::vector<int16_t> &out, uint32_t ms) {
  size_t n = (size_t)((uint64_t)ms * GB_RATE / 1000);
  size_t at = out.size();
  out.resize(at + n);
  syn.render(out.data() + at, n, 8);
}

// A note in the Game Boy's own frequency units, so this reads like the data in
// the pret disassemblies rather than like Hz.
static uint16_t gbNote(int semitonesAboveC4) {
  // C4 is about 262 Hz; f = 2048 - 131072/hz
  double hz = 261.63 * pow(2.0, semitonesAboveC4 / 12.0);
  int f = (int)(2048.0 - 131072.0 / hz);
  if (f < 0) f = 0;
  if (f > 2047) f = 2047;
  return (uint16_t)f;
}

int wavMain(const char *path, const char *demo) {
  GbSynth syn;
  std::vector<int16_t> out;
  std::string d = demo ? demo : "tour";

  if (d == "duty") {
    // the four duty cycles on the same note, so the difference is obvious
    for (uint8_t dz = 0; dz < 4; dz++) {
      syn.note(0, gbNote(12), dz, 15, 0, 0, 400);
      run(syn, out, 450);
    }
  } else if (d == "env") {
    syn.note(0, gbNote(12), 2, 15, 0, 0, 600); run(syn, out, 650);   // flat
    syn.note(0, gbNote(12), 2, 15, -1, 2, 600); run(syn, out, 650);  // decays
    syn.note(0, gbNote(12), 1, 15, -1, 1, 600); run(syn, out, 650);  // fast decay
  } else if (d == "noise") {
    for (uint16_t p : {2, 4, 8, 16, 32}) {
      syn.noise(15, -1, 2, 300, p);
      run(syn, out, 350);
    }
  } else if (d == "two") {
    // a melody over a bass line: the thing one voice could never do
    static const int mel[] = {12, 14, 16, 19, 16, 14, 12, 7};
    static const int bass[] = {0, 0, 5, 5, 7, 7, 0, 0};
    for (int i = 0; i < 8; i++) {
      syn.note(0, gbNote(mel[i]), 1, 13, -1, 6, 260);
      syn.note(1, gbNote(bass[i] - 12), 2, 9, 0, 0, 260);
      run(syn, out, 270);
    }
  } else {
    int t = -1;
    if (d == "gym") t = 0;
    else if (d == "trainer") t = 1;
    else if (d == "wild") t = 2;
    else if (d == "win") t = 3;
    else {
      for (int i = 0; i < MUSIC_TRACKS; i++)
        if (d == MUSIC_TBL[i].name) { t = i; break; }
    }
    if (t >= 0) {
      // A REAL tune from music.h, both pulse channels looping past their intro.
      const MusicTrack &m = MUSIC_TBL[t];
      printf("playing %s (%u + %u events)\n", m.name, m.n1, m.n2);
      uint32_t at1 = 0, at2 = 0;
      uint16_t i1 = 0, i2 = 0;
      uint32_t clock = 0;
      const uint32_t LIMIT = 30000;
      while (clock < LIMIT && (i1 < m.n1 || i2 < m.n2)) {
        if (i1 < m.n1 && clock >= at1) {
          if (i1 >= m.n1) i1 = m.loop1;
          const MusicNote &n = m.ch1[i1++];
          if (n.freq) syn.note(0, n.freq, n.duty, n.vol, n.envDir, n.envPeriod, n.ms);
          else syn.silence(0);
          at1 += n.ms;
        }
        if (i2 < m.n2 && clock >= at2) {
          if (i2 >= m.n2) i2 = m.loop2;
          const MusicNote &n = m.ch2[i2++];
          if (n.freq) syn.note(1, n.freq, n.duty, n.vol, n.envDir, n.envPeriod, n.ms);
          else syn.silence(1);
          at2 += n.ms;
        }
        uint32_t next = LIMIT;
        if (at1 < next) next = at1;
        if (at2 < next) next = at2;
        if (next <= clock) next = clock + 5;
        run(syn, out, next - clock);
        clock = next;
        if (i1 >= m.n1 && t != 3) i1 = m.loop1;
        if (i2 >= m.n2 && t != 3) i2 = m.loop2;
      }
    } else {
    // the tour: everything at once, in the order it was built
    for (uint8_t dz = 0; dz < 4; dz++) { syn.note(0, gbNote(12), dz, 15, 0, 0, 250); run(syn, out, 280); }
    syn.note(0, gbNote(12), 2, 15, -1, 2, 700); run(syn, out, 800);
    syn.noise(15, -1, 3, 400, 4); run(syn, out, 500);
    static const int mel[] = {12, 16, 19, 24, 19, 16};
    for (int i = 0; i < 6; i++) {
      syn.note(0, gbNote(mel[i]), 1, 13, -1, 8, 220);
      syn.note(1, gbNote(mel[i] - 19), 2, 8, 0, 0, 220);
      run(syn, out, 230);
    }
    }
  }
  writeWav(path, out);
  return 0;
}
