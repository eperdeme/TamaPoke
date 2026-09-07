#pragma once
#include <Arduino.h>

// How much room is left in the NVS partition, counted in ENTRIES of 32 bytes.
// `available` is the honest figure: it excludes the page NVS keeps in hand so
// that it can garbage-collect. Returns false if the stats could not be read.
bool nvsEntryStats(uint32_t *used, uint32_t *available, uint32_t *total);

// Close enough to full that the next save may start failing -- and that the
// next BOOT may not find a free page at all. See nvsinfo.cpp for why that is
// worse than it sounds.
bool nvsLowOnSpace();

// One line for the serial log, plus a warning if it is running out. `when` is a
// label ("boot", "health") so a soak test can see the trend.
void nvsReport(const char *when);
