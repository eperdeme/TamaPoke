#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "items.h"

// The bag.
//
// One count per item key rather than a list of stacks: the catalogue is owned
// by the firmware and small, so a fixed array cannot fragment, cannot run out
// of slots, and needs no packing. Growing it is the same additive migration the
// Pokedex bitmaps use -- getBytes() copies only what was stored and the rest of
// the array keeps its zero initialiser, so a blob written before an item
// existed reads back with that item at zero.
//
// Its own NVS key, not part of the pet blob. Same reasoning that put the box
// under its own key: purely additive changes cannot misread an existing save.
constexpr uint8_t BAG_STACK_MAX = 99;

class Inventory {
public:
  void begin();
  void save();

  uint8_t count(ItemKey k) const;
  // Returns what was actually added -- a full stack silently swallowing a drop
  // would read as the reward system being broken.
  uint8_t add(ItemKey k, uint8_t n = 1);
  bool consume(ItemKey k, uint8_t n = 1);
  bool has(ItemKey k) const { return count(k) > 0; }
  uint8_t distinctCount() const;
  // The i-th non-empty key, for the bag list. ITEM_NONE past the end.
  ItemKey keyAt(uint8_t i) const;

  // A new player needs something to throw. Called once, on the first begin()
  // that finds an empty bag, so it can never top a real player back up.
  void grantStarterKit();

  // One weighted draw over dropWeight, excluding anything already granted this
  // settlement so a reward list can never show the same item twice. `roll` is
  // caller-supplied so the table is testable at every boundary.
  ItemKey weightedDrop(uint32_t roll, const ItemKey *exclude = nullptr,
                       uint8_t excludeCount = 0) const;
  // Total weight left once `exclude` is removed; the caller needs it to reduce
  // its own roll into range.
  uint32_t weightTotal(const ItemKey *exclude = nullptr,
                       uint8_t excludeCount = 0) const;

  void clear();

private:
  uint8_t counts[ITEM_COUNT] = { 0 };
  Preferences prefs;
};

extern Inventory bag;
