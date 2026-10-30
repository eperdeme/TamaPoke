#pragma once
#include <Arduino.h>
#include "party.h"

// THE BOXES PAST THE FIRST.
//
// Box 1 is Party::box[], half of the party+box checkpoint in NVS, and it stays
// exactly that: every save already on a device keeps loading, and a swap inside
// box 1 is still one atomic record. NVS has no room for more (20 KB in all, and
// every slot costs twice its size because checkpoints alternate), so boxes 2 to
// BOX_EXTRA+1 live in the flash's FAT partition, one pair of files per box.
//
// The player never sees the seam. Party presents one storage of BOX_TOTAL slots
// (boxCap(), boxAt(), boxAdd(), swapPartyBox() take indices across all of it);
// an index below BOX_SLOTS is box 1, anything above is here. If the partition
// cannot be mounted, boxCap() is BOX_SLOTS and the game is what it was.
//
// ---- One box on flash ------------------------------------------------------
// Two files, A and B, written alternately, exactly like an NVS checkpoint: the
// newest one that is WHOLE wins, so a cut mid-write leaves the previous copy
// standing. "Whole" is a CRC-32 over everything before it.
//
//   off 0   4  magic "TPBX"
//   off 4   1  version (informational, as in ckpt.h; change the magic instead)
//   off 5   1  box number (2..)
//   off 6   1  slots stored
//   off 7   1  bytes per slot (sizeof(PartyMon) when written)
//   off 8   4  generation, picks the newer of A and B
//   off 12  4  save id, see below
//   off 16  .. slots * stride bytes of PartyMon
//   end-4   4  CRC-32 of everything before it
//
// Slots and stride are stored so a later build can grow either and still read
// this one -- the prefix rule ckpt.h states, applied per record.
//
// ---- Which save a box belongs to -------------------------------------------
// The files outlive the save. "Reiniciar partida" clears NVS and never touches
// this partition, and a restored backup replaces NVS wholesale -- both would
// otherwise leave the previous game's creatures sitting in boxes 2+ of a save
// that never owned them. So every save carries a random id (NVS "bxid"), every
// box file is stamped with it, and a file stamped with any other id reads as an
// empty box. A reset therefore empties the extra boxes without a single file
// operation, and so does a restore: saveImport() always installs a FRESH id and
// writes the backup's boxes under it.
//
// ---- Moving a creature across the seam ---------------------------------------
// A swap between the party (or box 1) and an extra box writes two stores that
// cannot commit together. A cut between them would duplicate one creature and
// lose another -- the bug the party+box record exists to prevent. So the swap
// first writes a JOURNAL (NVS "bxj", CRC'd) holding the final contents of BOTH
// places, then writes them, then removes it. A journal found at boot is
// replayed: setting both places to their recorded contents is idempotent, so
// it does not matter which of the two writes had landed. A journal that never
// finished writing fails its CRC and means the swap never started.

#define BOX_EXTRA 9                               // boxes 2..10
#define BOX_EXTRA_SLOTS (BOX_EXTRA * BOX_SLOTS)   // 324
#define BOX_TOTAL (BOX_SLOTS + BOX_EXTRA_SLOTS)   // 360

#define BOXFILE_HDR 16
#define BOXFILE_SIZE (BOXFILE_HDR + BOX_SLOTS * sizeof(PartyMon) + 4)

// After party.begin(). Mounts the store, loads every extra box stamped with this
// save's id (minting one if the save has none), and replays an unfinished swap.
// False: no store, so boxCap() stays at BOX_SLOTS.
bool boxStoreBegin(Party &party);
bool boxStoreReady();

// Slot i of the extra boxes, 0 .. BOX_EXTRA_SLOTS-1 (box 2 slot 0 is 0).
PartyMon &boxStoreSlot(uint16_t i);

// Persists one extra box, 0 .. BOX_EXTRA-1. False if the store refused; the
// previous copy is still on flash.
bool boxStoreSave(uint8_t box);

// Exchanges party.slots[aIdx] (or party.box[aIdx] when aIsBox1) with extra slot
// `extraIdx`, through the journal. False only if the journal could not be
// written, in which case nothing changed.
bool boxStoreSwap(Party &party, bool aIsBox1, uint8_t aIdx, uint16_t extraIdx);

uint32_t boxStoreId();

// ---- backup ----------------------------------------------------------------
// saveExport() emits each non-empty extra box as key "bx<n>" (n = 2..), value =
// BOX_SLOTS raw PartyMon records. saveImport() hands them back here.
bool boxStoreBoxEmpty(uint8_t box);
const PartyMon *boxStoreBoxData(uint8_t box);
// Writes the given boxes (nullptr = absent) under a new id and returns it in
// *newId, for the caller to install as "bxid" once NVS is replaced. False if the
// store is unavailable or refused a write; the caller must then reject the
// restore before touching NVS.
// `stride` is the size of each record IN THE BACKUP, which need not be this
// build's sizeof(PartyMon).
bool boxStoreImport(const uint8_t *const boxes[BOX_EXTRA], uint16_t stride, uint32_t *newId);
// A fresh id with nothing written, for a restore that carries no extra boxes.
uint32_t boxStoreNewId();

// Tests: forget everything in RAM, as a reboot does.
void boxStoreReset();
