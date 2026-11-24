// forRuns (blit.h): drawing merged horizontal runs yields the exact same pixels
// as drawing pixel-by-pixel, including silhouette merging, unsigned char keys,
// and fractional-zoom telescoping in drawPmdActZ().
#include "blit.h"
#include <cstdio>
#include <vector>

static int bad = 0;
static void ck(bool ok, const char *w) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", w);
  if (!ok) bad++;
}

static std::vector<int> paint(const std::vector<uint8_t> &img, int w, int h, int s, bool runs) {
  int W = w * s, H = h * s;
  std::vector<int> out(W * H, -1);
  auto rect = [&](int x, int y, int rw, int rh, int col) {
    for (int j = 0; j < rh; j++)
      for (int i = 0; i < rw; i++) out[(y + j) * W + x + i] = col;
  };
  for (int r = 0; r < h; r++) {
    const uint8_t *row = &img[r * w];
    if (runs) {
      forRuns(w, [&](int c) { return indexedKey(row[c], false); },
              [&](int c0, int len, int k) { rect(c0 * s, r * s, len * s, s, k); });
    } else {
      for (int c = 0; c < w; c++)
        if (row[c] != 0xFF) rect(c * s, r * s, s, s, row[c]);
    }
  }
  return out;
}

// Fractional-zoom equivalence (used by drawPmdActZ: xa = c0*sc/100, xb = (c0+len)*sc/100).
static bool paintZoomEq(const std::vector<uint8_t> &row, uint32_t sc) {
  int w = (int)row.size();
  int W = (int)(w * sc / 100) + 1;
  std::vector<int> px(W, -1), rn(W, -1);
  for (int c = 0; c < w; c++) {
    if (row[c] == 0xFF) continue;
    int xa = (int)(c * sc / 100), xb = (int)((c + 1) * sc / 100);
    for (int x = xa; x < xb; x++) px[x] = row[c];
  }
  forRuns(w, [&](int c) { return indexedKey(row[c], false); },
          [&](int c0, int len, int k) {
            int xa = (int)(c0 * sc / 100), xb = (int)((c0 + len) * sc / 100);
            for (int x = xa; x < xb; x++) rn[x] = k;
          });
  return px == rn;
}

int main() {
  uint32_t seed = 12345;
  bool randOk = true;
  for (int iter = 0; iter < 200; iter++) {
    int w = 1 + iter % 40, h = 1 + (iter / 3) % 30, s = 1 + iter % 5;
    std::vector<uint8_t> img(w * h);
    for (auto &p : img) {
      seed = seed * 1664525u + 1013904223u;
      uint32_t v = (seed >> 16) % 6;
      p = v == 0 ? 0xFF : v == 1 ? 7 : (uint8_t)(v % 3);
    }
    if (paint(img, w, h, s, true) != paint(img, w, h, s, false)) randOk = false;
  }
  ck(randOk, "200 randomized images draw the exact same pixels as per-pixel loops");

  {
    std::vector<uint8_t> empty(8, 0xFF), full(8, 3), one = { 5 };
    ck(paint(empty, 8, 1, 2, true) == paint(empty, 8, 1, 2, false) &&
       paint(full, 8, 1, 2, true) == paint(full, 8, 1, 2, false) &&
       paint(one, 1, 1, 3, true) == paint(one, 1, 1, 3, false),
       "empty, full and 1-pixel rows match");
  }

  ck(indexedKey(0xFF, false) == -1 && indexedKey(0xFF, true) == -1 &&
     indexedKey(9, false) == 9 && indexedKey(9, true) == 0 && indexedKey(0, true) == 0,
     "indexedKey handles transparent and silhouette modes");

  {
    std::vector<uint8_t> img = { 1, 2, 3, 0xFF, 4, 4, 0xFF, 7 };
    auto naive = [&](bool sil) {
      std::vector<int> out(img.size(), -1);
      for (size_t c = 0; c < img.size(); c++) if (img[c] != 0xFF) out[c] = sil ? 99 : img[c];
      return out;
    };
    auto runs = [&](bool sil, int *count) {
      std::vector<int> out(img.size(), -1);
      *count = 0;
      forRuns((int)img.size(), [&](int c) { return indexedKey(img[c], sil); },
              [&](int c0, int len, int k) {
                (*count)++;
                for (int i = 0; i < len; i++) out[c0 + i] = sil ? 99 : k;
              });
      return out;
    };
    int nSil = 0, nCol = 0;
    ck(runs(true, &nSil) == naive(true) && runs(false, &nCol) == naive(false) &&
       nSil == 3 && nCol == 5,
       "silhouette merges distinct palette runs into single ink runs");
  }

  ck(charKey('.', false) == -1 && charKey('.', true) == -1 &&
     charKey('k', false) == (int)'k' && charKey('k', true) == 0 &&
     charKey((char)0xE9, false) == 0xE9 && (char)charKey((char)0xE9, false) == (char)0xE9,
     "charKey keeps '.' transparent and preserves high-bit unsigned characters");

  {
    int calls = 0;
    forRuns(0, [&](int) { return 1; }, [&](int, int, int) { calls++; });
    bool altOk = (calls == 0);
    std::vector<uint8_t> alt = { 1, 2, 1, 2, 1, 2 };
    forRuns(6, [&](int c) { return (int)alt[c]; },
            [&](int c0, int len, int k) { if (len != 1 || k != (int)alt[c0]) altOk = false; calls++; });
    ck(altOk && calls == 6, "width 0 emits nothing and alternating pixels stay exact");
  }

  {
    std::vector<uint8_t> row = { 0xFF, 2, 2, 2, 3, 3, 0xFF, 4, 4, 4, 4, 0xFF };
    ck(paintZoomEq(row, 175) && paintZoomEq(row, 230) && paintZoomEq(row, 350),
       "fractional-zoom run boundaries match per-pixel coordinates");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
