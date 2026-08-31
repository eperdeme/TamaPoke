// The UI palette is RGB565 on the panel, so test the quantized colours the
// player actually sees rather than the prettier source hex values.
#include "species.h"
#include <cmath>
#include <cstdio>

static int bad = 0;

static double linear(double value) {
  return value <= 0.04045 ? value / 12.92
                          : std::pow((value + 0.055) / 1.055, 2.4);
}

static double luminance(uint16_t color) {
  double red = ((color >> 11) & 31) / 31.0;
  double green = ((color >> 5) & 63) / 63.0;
  double blue = (color & 31) / 31.0;
  return 0.2126 * linear(red) + 0.7152 * linear(green) + 0.0722 * linear(blue);
}

static double contrast(uint16_t one, uint16_t two) {
  double a = luminance(one), b = luminance(two);
  if (a < b) { double tmp = a; a = b; b = tmp; }
  return (a + 0.05) / (b + 0.05);
}

static void ck(const char *name, uint16_t one, uint16_t two, double minimum) {
  double measured = contrast(one, two);
  bool ok = measured + 0.001 >= minimum;
  std::printf("%s  %-30s %.2f:1 (need %.1f)\n",
              ok ? "PASS" : "FAIL", name, measured, minimum);
  if (!ok) bad++;
}

int main() {
  // WCAG 2.2 normal-text contrast. Small bitmap text needs the normal-text
  // threshold; none of it qualifies for the relaxed large-text rule.
  ck("body text / day", UI_INK, UI_BG_DAY, 4.5);
  ck("secondary text / day", UI_INK_SOFT, UI_BG_DAY, 4.5);
  ck("secondary text / surface", UI_INK_SOFT, UI_TRACK, 4.5);
  ck("success text / day", UI_TEXT_OK, UI_BG_DAY, 4.5);
  ck("warning text / day", UI_TEXT_WARN, UI_BG_DAY, 4.5);
  ck("danger text / day", UI_TEXT_BAD, UI_BG_DAY, 4.5);
  ck("body text / night", UI_INK_NIGHT, UI_BG_NIGHT, 4.5);
  ck("ink / neutral surface", UI_INK, UI_TRACK, 4.5);

  // WCAG non-text contrast for meaningful UI graphics. The care hues are
  // outlined in ink, so that is their adjacent colour on a day scene; on a
  // night scene the bright fill can also meet the background directly.
  ck("day groove / day", UI_GROOVE_DAY, UI_BG_DAY, 3.0);
  ck("night groove / night", UI_GROOVE_NIGHT, UI_BG_NIGHT, 3.0);
  ck("scroll track / day", UI_SCROLL_DAY, UI_BG_DAY, 3.0);
  ck("scroll thumb / day track", UI_INK, UI_SCROLL_DAY, 3.0);
  ck("scroll track / night", UI_SCROLL_NIGHT, UI_BG_NIGHT, 3.0);
  ck("scroll thumb / night track", UI_INK_NIGHT, UI_SCROLL_NIGHT, 3.0);
  ck("OK fill / day groove", UI_BAR_OK, UI_GROOVE_DAY, 3.0);
  ck("warning fill / day groove", UI_BAR_WARN, UI_GROOVE_DAY, 3.0);
  ck("danger fill / day groove", UI_BAR_BAD, UI_GROOVE_DAY, 3.0);
  const uint16_t care[] = { UI_CARE_FOOD, UI_CARE_JOY, UI_CARE_ENE, UI_CARE_HYG };
  const char *name[] = { "food", "joy", "energy", "hygiene" };
  for (int i = 0; i < 4; i++) {
    char label[36];
    std::snprintf(label, sizeof(label), "%s / ink outline", name[i]);
    ck(label, care[i], UI_INK, 3.0);
    std::snprintf(label, sizeof(label), "%s / night", name[i]);
    ck(label, care[i], UI_BG_NIGHT, 3.0);
    for (int j = 0; j < i; j++)
      if (care[i] == care[j]) { std::printf("FAIL  duplicate care hues\n"); bad++; }
  }

  std::printf("%s\n", bad ? "FAILURES" : "palette holds");
  return bad ? 1 : 0;
}