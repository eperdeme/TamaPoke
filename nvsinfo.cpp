// How much room is left in NVS, and why anybody should care.
//
// A save rewrites every key it owns, and NVS is log-structured: each put appends
// a fresh entry and marks the old one stale, reclaiming the space only by
// garbage-collecting a whole 4 KB page. The stock app3M_fat9M_16MB table gives
// `nvs` 20 KB -- five pages, roughly 630 entries of 32 bytes -- and NVS needs a
// free page in hand to compact into.
//
// If it ever runs out, nvs_flash_init() returns ESP_ERR_NVS_NO_FREE_PAGES, and
// the Arduino core's response (esp32-hal-misc.c, initArduino) is to ERASE THE
// ENTIRE PARTITION before setup() is ever reached:
//
//     err = nvs_flash_init();
//     if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//       ... esp_partition_erase_range(partition, 0, partition->size);
//
// Every save on the device, gone, with nothing a player could ever see. The
// firmware could not see it coming either: putX() returning short was discarded
// at every call site except the checkpoints.
//
// This cannot prevent that. What it does is put the number in the log at boot
// and in the HEALTH heartbeat, so the trend is visible during a soak test rather
// than discovered by a wiped board. The real fix is fewer writes per save --
// see the checkpoint note in pet.cpp, and the ~50 legacy keys still written
// beside them.
//
// Not compiled into the emulator: its NVS is a std::map with no partition to
// exhaust, so host_impl.cpp stubs these. Same arrangement as rtcbat.cpp.
#include "nvsinfo.h"
#include <nvs.h>
#include <nvs_flash.h>

// Roughly one page's worth. NVS wants a spare page to compact into, so being
// inside a single page of the end is the point at which the next save is living
// on borrowed time.
#define NVS_LOW_ENTRIES 126

bool nvsEntryStats(uint32_t *used, uint32_t *available, uint32_t *total) {
  nvs_stats_t s;
  if (nvs_get_stats(NULL, &s) != ESP_OK) return false;
  if (used) *used = (uint32_t)s.used_entries;
  if (available) *available = (uint32_t)s.available_entries;
  if (total) *total = (uint32_t)s.total_entries;
  return true;
}

bool nvsLowOnSpace() {
  uint32_t avail = 0;
  if (!nvsEntryStats(nullptr, &avail, nullptr)) return false;
  return avail < NVS_LOW_ENTRIES;
}

void nvsReport(const char *when) {
  uint32_t used = 0, avail = 0, total = 0;
  if (!nvsEntryStats(&used, &avail, &total)) {
    Serial.printf("nvs %s: stats unavailable\n", when);
    return;
  }
  Serial.printf("nvs %s: used=%lu avail=%lu total=%lu\n", when,
                (unsigned long)used, (unsigned long)avail, (unsigned long)total);
  if (avail < NVS_LOW_ENTRIES)
    Serial.printf("nvs %s: LOW -- a full partition is erased WHOLE on the next "
                  "boot; EXPORT now\n", when);
}
