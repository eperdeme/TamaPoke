// The emulator's file store for the extra boxes: an in-memory map standing in
// for FFat, plus the faults a test needs to inject. See boxfs.h.
#include "boxfs.h"
#include "boxfs_host.h"
#include <cstring>

std::map<std::string, std::vector<uint8_t>> &bxfsFiles() {
  static std::map<std::string, std::vector<uint8_t>> files;
  return files;
}
static bool mounted = false;
bool &bxfsHostBroken() { static bool b = false; return b; }
int &bxfsHostWriteBudget() { static int n = -1; return n; }
int &bxfsHostTearAt() { static int n = -1; return n; }

bool bxfsMount() {
  if (bxfsHostBroken()) return false;
  mounted = true;
  return true;
}
bool bxfsReady() { return mounted; }
void bxfsHostUnmount() { mounted = false; }

bool bxfsRead(const char *path, uint8_t *buf, size_t cap, size_t *got) {
  if (!mounted) return false;
  auto it = bxfsFiles().find(path);
  if (it == bxfsFiles().end()) return false;
  size_t n = it->second.size() < cap ? it->second.size() : cap;
  memcpy(buf, it->second.data(), n);
  if (got) *got = n;
  return true;
}

bool bxfsWrite(const char *path, const uint8_t *buf, size_t n) {
  if (!mounted) return false;
  int &budget = bxfsHostWriteBudget();
  if (budget == 0) return false;            // the store refuses: nothing changes
  if (budget > 0) budget--;
  int &tear = bxfsHostTearAt();
  if (tear >= 0) {
    // A power cut mid-write: the file is replaced by its first `tear` bytes,
    // and the write reports nothing because the board would not have lived to.
    size_t keep = (size_t)tear < n ? (size_t)tear : n;
    bxfsFiles()[path].assign(buf, buf + keep);
    tear = -1;
    bxfsHostWriteBudget() = 0;              // and nothing after it runs
    return false;
  }
  bxfsFiles()[path].assign(buf, buf + n);
  return true;
}
