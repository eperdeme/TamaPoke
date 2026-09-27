// The CRC-32 behind the console's SUM. The web installer compares SUM with its own
// CRC-32 of the bytes it sent, so both must be the SAME algorithm -- pinned here to
// the published check value, not to a copy of either implementation.
#include "crc32.h"
#include <cstdio>

static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }

int main() {
  const uint8_t *check = (const uint8_t *)"123456789";
  ck(crc32Update(0, check, 9) == 0xCBF43926u,
     "CRC-32 matches the published check value for \"123456789\"");
  ck(crc32Update(0, check, 0) == 0, "and an empty input is zero");
  ck(crc32Update(crc32Update(0, check, 4), check + 4, 5) == 0xCBF43926u,
     "a split read chains to the same value, which is how SUM reads a file");
  printf("%s\n", bad ? "FAILURES" : "the card's checksum is the installer's checksum");
  return bad ? 1 : 0;
}
