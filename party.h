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
// v3.23 and every earlier box had 18 slots. Keep that dimension explicit: a
// legacy blob's length alone cannot distinguish fewer current-size records from
// more shorter records once the box grows.
#define BOX_V323_SLOTS 18
#define BOX_SLOTS 36
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
  // The party and the box are ONE checkpointed record, because a swap moves a
  // creature between them. See the note above Party::savePair() in party.cpp.
  bool savePair();
  bool loadPair();
  // loadPair()'s body, given whichever buffer it chose. See the note there: a
  // record from a LATER build is longer than this one's, and the stack cannot
  // pay for that tolerance, so the oversized case is read on the heap.
  bool loadPairFrom(uint8_t *buf, size_t cap);
  // Swaps a party slot with a box slot. Either may be empty, so this doubles as
  // deposit and withdraw rather than needing three separate operations.
  void swapPartyBox(uint8_t partyIdx, uint8_t boxIdx);

  // ---- the focus swap's write-ahead journal -------------------------------
  // Exchanging the live creature with a party slot writes TWO records -- this
  // pair, and the pet's own -- and NOTHING can make those one commit: they have
  // separate keys and separate generation counters by design. See the essay
  // above Party::focusBegin() in party.cpp for what tears and how this closes it.
  //
  // The journal holds BOTH sides of the exchange, so recovery replays the whole
  // thing to a known end state rather than trying to work out which half landed
  // -- a PartyMon has no identity, so that question has no reliable answer.
  bool focusBegin(uint8_t slot, const PartyMon &outgoing, bool outgoingIsEgg,
                  const PartyMon &incoming, uint32_t petGenBefore);
  // Is a swap unfinished? Fills in whatever the caller passes. Not const: it
  // reads NVS.
  bool focusPending(uint8_t *slot, PartyMon *outgoing, bool *outgoingWasEgg,
                    PartyMon *incoming, uint32_t *petGenBefore);
  void focusEnd();

  // combat stats of a party member, same formula as the live pet's
  uint16_t atkOf(const PartyMon &m) const;
  uint16_t defOf(const PartyMon &m) const;
  uint16_t speOf(const PartyMon &m) const;
  uint16_t vitOf(const PartyMon &m) const;
  uint16_t spaOf(const PartyMon &m) const;
  uint16_t spdOf(const PartyMon &m) const;

private:
  Preferences prefs;
  // Its own counter, never shared with the pet's: if one record's write
  // succeeded and another's did not, a shared counter would advance anyway and
  // the next write would land on the slot holding the only complete copy.
  uint32_t pairGeneration = 0;
  uint8_t loadedPairBoxSlots = 0;
  // And the journal gets its OWN again, for the same reason.
  uint32_t focusGeneration = 0;
  // The checkpoint plus both legacy blobs, exactly once. save() and boxSave()
  // are names for it; calling them in sequence wrote the whole pair twice.
  void persist();
};

extern Party party;
