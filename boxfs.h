#pragma once
#include <stddef.h>
#include <stdint.h>

// THE FILE STORE BEHIND THE EXTRA BOXES.
//
// NVS is 20 KB in all and already carries the creature, the player, the party
// and box 1 twice over, so the boxes past the first live in the flash's FAT
// partition instead (the 9 MB "ffat" entry of app3M_fat9M_16MB, which nothing
// else used). This header is the whole surface boxstore.cpp needs from it, so
// the board (boxfs.cpp, FFat) and the emulator (tools/emu/boxfs_host.cpp, an
// in-memory map with fault injection) can each supply it.
//
// No partial reads or appends: every file is small and written whole.

// Mounts the partition, formatting it the first time (a blank partition reads
// as unmountable). Idempotent. False means there is nowhere to keep the extra
// boxes: the game runs with box 1 only, exactly as before this feature.
bool bxfsMount();
bool bxfsReady();

// Reads up to `cap` bytes of `path` into `buf`. False if the file is absent or
// unreadable; *got is the number of bytes read on success.
bool bxfsRead(const char *path, uint8_t *buf, size_t cap, size_t *got);

// Writes `n` bytes as the whole of `path`, replacing it. True only if all of
// them were written and the file was closed. A torn write is still possible on
// a power cut, which is why boxstore.cpp never overwrites the only good copy.
bool bxfsWrite(const char *path, const uint8_t *buf, size_t n);
