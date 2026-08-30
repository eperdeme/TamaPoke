#pragma once
#include <Arduino.h>
#include <Preferences.h>

// The party: pets that finished their life and were kept, rather than being
// dissolved into a single Pokedex bit like every previous ending did.
//
// Only the two endings the player CHOOSES bank a pet -- farewell and release.
// A runaway does not: it is the game's one punishing outcome, and letting a
// neglected pet come back as a team member would take the sting out of it.
#define PARTY_SLOTS 6
// The box: storage beyond the six that fight. Deliberately a SEPARATE NVS key
// rather than a bigger party blob -- growing that blob would change its stride
// and the length-based migration in begin() cannot tell a stride change from a
// slot-count change, so an existing party would be read back misaligned. A new
// key is purely additive and cannot corrupt anything.
#define BOX_SLOTS 18
#define MOVE_SLOTS 4    // the same four every trainer gets in the real games

// A stored creature -- one of the six that fight, or one in the box.
//
// It used to be frozen forever: banked at a level and never trained again. It
// still is WHILE STORED, but the record now carries the full care state as
// well, so swapping it in as the focused creature resumes exactly where it left
// off instead of handing back a blanked-out copy. Freezing is now a property of
// where the record lives, not of the record itself.
struct PartyMon {
  int16_t dex = 0;      // Pokedex number, 0 = empty slot
  uint16_t level = 1;   // cached from ageMinutes so lists need not recompute it
  uint16_t medals = 0;  // what it earned in life
  uint8_t ivAtk = 0, ivDef = 0, ivSpe = 0, ivHp = 0;
  uint8_t trAtk = 0, trDef = 0, trSpe = 0;
  uint8_t shiny = 0;
  char nick[12] = "";
  // Moves travel with the creature. 0 = empty slot (MOVE_TBL[0] is the "-"
  // filler). Appended at the END of the struct on purpose -- Party::begin()
  // migrates older, shorter blobs by length, and that only works if nothing
  // that already exists moves. Everything below obeys the same rule.
  uint8_t moves[MOVE_SLOTS] = { 0, 0, 0, 0 };

  // ---- care state, appended in v2.10 --------------------------------------
  // 0 means "this record predates care state": its fields below are merely the
  // initialisers, not something the creature earned, so `level` is the only
  // truth about its age. The default is 0 rather than 1 precisely so that a
  // record the length migration only half-filled reports itself honestly --
  // the migration memcpy's oldStride bytes over a default-constructed record
  // and leaves the tail alone, so anything defaulting to "I have real data"
  // would be a lie the moment the struct grows again.
  uint8_t stateVersion = 0;
  uint8_t fullness = 80, joy = 80, energy = 80, hygiene = 100;
  uint8_t poops = 0, weight = 0, bond = 0;
  uint8_t berryKnown = 0, careMistakes = 0;
  uint8_t evoDeclinedLv = 0, lastLearnLevel = 0;
  // The real age. `level` stays as its cached mirror so every list, the battle
  // squad builder and the box screen keep reading one byte instead of dividing.
  uint32_t ageMinutes = 0;

  bool empty() const { return dex < 1; }
  bool hasCareState() const { return stateVersion >= 1; }
};

class Party {
public:
  PartyMon slots[PARTY_SLOTS];
  PartyMon box[BOX_SLOTS];

  void begin();                 // load from NVS
  uint8_t count() const;
  bool isFull() const { return count() >= PARTY_SLOTS; }
  int firstFree() const;        // index of the first empty slot, -1 if full
  bool add(const PartyMon &m);  // into the first free slot; false if full
  void replaceAt(uint8_t i, const PartyMon &m);
  void releaseAt(uint8_t i);    // free a slot again
  void save();
  uint8_t boxCount() const;
  int boxFirstFree() const;
  bool boxAdd(const PartyMon &m);     // into the first free box slot
  void boxReleaseAt(uint8_t i);
  void boxSave();
  // Swaps a party slot with a box slot. Either may be empty, so this doubles as
  // deposit and withdraw rather than needing three separate operations.
  void swapPartyBox(uint8_t partyIdx, uint8_t boxIdx);

  // combat stats of a party member, same formula as the live pet's
  uint16_t atkOf(const PartyMon &m) const;
  uint16_t defOf(const PartyMon &m) const;
  uint16_t speOf(const PartyMon &m) const;
  uint16_t vitOf(const PartyMon &m) const;
  uint16_t spaOf(const PartyMon &m) const;
  uint16_t spdOf(const PartyMon &m) const;

private:
  Preferences prefs;
};

extern Party party;
