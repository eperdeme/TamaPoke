// The Game Boy music in music.h, checked as data: loop points, channel alignment,
// and real Game Boy tempo (~82 ms per 16th note at gym tempo 104, speed 12).
#include "music.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

static uint32_t span(const MusicNote *n, uint16_t from, uint16_t to) {
  uint32_t ms = 0;
  for (uint16_t i = from; i < to; i++) ms += n[i].ms;
  return ms;
}

static bool multipleOf(uint32_t a, uint32_t b, uint32_t slack) {
  if (!b) return false;
  uint32_t k = (a + b / 2) / b;
  return k >= 1 && (uint32_t)abs((int)(a - k * b)) <= slack;
}

int main() {
  char w[160];
  ck(MUSIC_TRACKS >= 4 && !strcmp(MUSIC_TBL[0].name, "gymleaderbattle"),
     "track 0 is still the gym battle theme");
  ck(MUSIC_TRACKS >= 4 && !strcmp(MUSIC_TBL[3].name, "defeatedgymleader"),
     "track 3 is still the victory fanfare");

  for (int t = 0; t < MUSIC_TRACKS; t++) {
    const MusicTrack &m = MUSIC_TBL[t];
    snprintf(w, sizeof w, "%s: loop points inside both channels", m.name);
    ck(m.loop1 < m.n1 && m.loop2 < m.n2, w);

    uint32_t b1 = span(m.ch1, m.loop1, m.n1), b2 = span(m.ch2, m.loop2, m.n2);
    uint32_t i1 = span(m.ch1, 0, m.loop1), i2 = span(m.ch2, 0, m.loop2);
    const uint32_t slack = 12;
    bool body = (b1 >= b2) ? multipleOf(b1, b2, slack) : multipleOf(b2, b1, slack);
    snprintf(w, sizeof w, "%s: channel bodies line up (%u / %u ms)", m.name, b1, b2);
    ck(body, w);
    uint32_t di = i1 > i2 ? i1 - i2 : i2 - i1;
    uint32_t fig = (i1 > i2) ? b2 : b1;
    snprintf(w, sizeof w, "%s: intros line up (%u / %u ms)", m.name, i1, i2);
    ck(di <= slack || multipleOf(di, fig, slack), w);
  }

  uint16_t first = 0;
  while (first < MUSIC_TBL[0].n1 && !MUSIC_TBL[0].ch1[first].freq) first++;
  snprintf(w, sizeof w, "gym intro 16th note is ~82 ms (got %u)", MUSIC_TBL[0].ch1[first].ms);
  ck(first < MUSIC_TBL[0].n1 && MUSIC_TBL[0].ch1[first].ms >= 78 && MUSIC_TBL[0].ch1[first].ms <= 86, w);

  printf(bad ? "%d FAILED\n" : "all passed\n", bad);
  return bad ? 1 : 0;
}
