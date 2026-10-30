// The board's file store for the extra boxes: FFat on the "ffat" partition.
// See boxfs.h. The emulator links tools/emu/boxfs_host.cpp instead.
#include "boxfs.h"
#include <Arduino.h>
#include <FFat.h>

static bool mounted = false;
static bool tried = false;

bool bxfsMount() {
  if (mounted) return true;
  // One attempt per boot. A partition that will not mount or format is not
  // going to start working on the next call, and each attempt can take
  // seconds -- the box screen must not stall every time it is opened.
  if (tried) return false;
  tried = true;
  // formatOnFail: a partition no build has written to yet does not mount.
  // That is the normal first boot after this update, and formatting it loses
  // nothing because nothing was ever stored there.
  mounted = FFat.begin(true, "/ffat", 4, "ffat");
  Serial.printf("boxes: file store %s\n", mounted ? "ready" : "UNAVAILABLE");
  if (mounted) FFat.mkdir("/tpbox");
  return mounted;
}

bool bxfsReady() { return mounted; }

bool bxfsRead(const char *path, uint8_t *buf, size_t cap, size_t *got) {
  if (!mounted) return false;
  File f = FFat.open(path, FILE_READ);
  if (!f) return false;
  size_t n = f.read(buf, cap);
  f.close();
  if (got) *got = n;
  return true;
}

bool bxfsWrite(const char *path, const uint8_t *buf, size_t n) {
  if (!mounted) return false;
  File f = FFat.open(path, FILE_WRITE);
  if (!f) return false;
  size_t w = f.write(buf, n);
  f.flush();
  f.close();
  return w == n;
}
