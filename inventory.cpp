#include "inventory.h"
#include <string.h>

Inventory bag;

// Same NVS namespace as the pet and the party on purpose: WIPE
// (Pet::factoryReset) calls clear() on it, and a factory reset that left the
// bag behind would be a lie.
void Inventory::begin() {
  memset(counts, 0, sizeof(counts));
  // Checked so a failure is at least visible; see the note in Party::begin().
  if (!prefs.begin("tamapoke", false))
    Serial.println("save: NVS would not open for the bag");
  size_t stored = prefs.getBytesLength("bag");
  if (stored) {
    // A blob from a build with FEWER items lands in the front and every key it
    // knew about keeps its meaning; one from a build with MORE is read through
    // a temporary, since getBytes copies nothing when the blob exceeds the
    // buffer. Neither case can shift an existing count onto another item,
    // because a key's position in this array never changes.
    if (stored <= sizeof(counts)) {
      prefs.getBytes("bag", counts, stored);
    } else {
      uint8_t *tmp = (uint8_t *)malloc(stored);
      if (tmp) {
        if (prefs.getBytes("bag", tmp, stored) == stored)
          memcpy(counts, tmp, sizeof(counts));
        free(tmp);
      }
    }
  } else {
    grantStarterKit();
  }
  counts[ITEM_NONE] = 0;   // the filler entry is never carried
  for (uint8_t k = 0; k < ITEM_COUNT; k++)
    if (counts[k] > BAG_STACK_MAX) counts[k] = BAG_STACK_MAX;
}

void Inventory::save() {
  prefs.putBytes("bag", counts, sizeof(counts));
}

void Inventory::clear() {
  memset(counts, 0, sizeof(counts));
  save();
}

void Inventory::grantStarterKit() {
  counts[IT_POKEBALL] = 5;
  counts[IT_POTION] = 3;
  save();
}

uint8_t Inventory::count(ItemKey k) const {
  return itemValid(k) ? counts[k] : 0;
}

uint8_t Inventory::add(ItemKey k, uint8_t n) {
  if (!itemValid(k) || !n) return 0;
  uint8_t room = (uint8_t)(BAG_STACK_MAX - counts[k]);
  uint8_t got = n < room ? n : room;
  counts[k] = (uint8_t)(counts[k] + got);
  if (got) save();
  return got;
}

bool Inventory::consume(ItemKey k, uint8_t n) {
  if (!itemValid(k) || !n || counts[k] < n) return false;
  counts[k] = (uint8_t)(counts[k] - n);
  save();
  return true;
}

uint8_t Inventory::distinctCount() const {
  uint8_t n = 0;
  for (uint8_t k = 1; k < ITEM_COUNT; k++)
    if (counts[k]) n++;
  return n;
}

ItemKey Inventory::keyAt(uint8_t i) const {
  for (uint8_t k = 1; k < ITEM_COUNT; k++) {
    if (!counts[k]) continue;
    if (!i) return k;
    i--;
  }
  return ITEM_NONE;
}

static bool excluded(ItemKey k, const ItemKey *ex, uint8_t n) {
  for (uint8_t i = 0; i < n; i++)
    if (ex[i] == k) return true;
  return false;
}

uint32_t Inventory::weightTotal(const ItemKey *exclude,
                                uint8_t excludeCount) const {
  uint32_t total = 0;
  for (uint8_t k = 1; k < ITEM_COUNT; k++) {
    if (excluded(k, exclude, excludeCount)) continue;
    total += ITEM_TBL[k].dropWeight;
  }
  return total;
}

ItemKey Inventory::weightedDrop(uint32_t roll, const ItemKey *exclude,
                                uint8_t excludeCount) const {
  uint32_t total = weightTotal(exclude, excludeCount);
  if (!total) return ITEM_NONE;
  uint32_t at = roll % total;
  for (uint8_t k = 1; k < ITEM_COUNT; k++) {
    if (excluded(k, exclude, excludeCount)) continue;
    uint32_t w = ITEM_TBL[k].dropWeight;
    if (at < w) return k;
    at -= w;
  }
  return ITEM_NONE;
}
