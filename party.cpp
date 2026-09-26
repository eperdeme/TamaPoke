#include "party.h"
#include <stdlib.h>
#include <string.h>
#include "dex.h"
#include "ckpt.h"   // the alternating CRC-checked record format, shared with pet.cpp

Party party;

// Same NVS namespace as the pet on purpose: WIPE (Pet::factoryReset) calls
// clear() on it, and a factory reset that left the party behind would be a lie.
void Party::begin() {
  bool boxNeedsRewrite = false;
  // Start from empty: getBytes() leaves the destination untouched when the key
  // is missing, so without this a reload after a wipe would keep showing the
  // old party out of RAM.
  for (auto &s : slots) s = PartyMon();
  // Checked so a failure is at least visible. The panel's warning comes from
  // Pet::saveHealthy(), which shares this namespace and so fails with it.
  if (!prefs.begin("tamapoke", false))
    Serial.println("save: NVS would not open for the party");
  // The blob is raw structs, so growing PartyMon (moves[] was appended in v1.9)
  // changes its stride. Reading an older, shorter blob straight into the new
  // array would land slot 1 onward at the wrong offset and quietly invent a
  // party out of misaligned bytes -- and the dex-range check below would not
  // reliably catch it, since a stray byte is often a valid Pokedex number.
  // So migrate by length: copy each old record into the front of the new one
  // and leave moves[] zeroed for the learnset to fill in.
  size_t stored = prefs.getBytesLength("party");
  if (stored == sizeof(slots)) {
    prefs.getBytes("party", slots, sizeof(slots));
  } else if (stored > sizeof(slots) && stored % sizeof(PartyMon) == 0) {
    // A blob from a build with MORE SLOTS at our own stride. Only this case is
    // unambiguous: stored/sizeof(PartyMon) records, each laid out as we lay them
    // out, so the first PARTY_SLOTS of them are ours to keep. Anything else that
    // is merely "too long" could equally be the same slot count at a BIGGER
    // stride, where a prefix read would land slot 1 at the wrong offset and
    // invent a party out of misaligned bytes -- so that is left empty instead.
    //
    // getBytes copies NOTHING when the stored blob exceeds the buffer, so this
    // has to go through a temporary of the stored size.
    uint8_t *tmp = (uint8_t *)malloc(stored);
    if (tmp) {
      if (prefs.getBytes("party", tmp, stored) == stored)
        memcpy(slots, tmp, sizeof(slots));
      free(tmp);
    }
  } else if (stored && stored % PARTY_SLOTS == 0 && stored < sizeof(slots)) {
    size_t oldStride = stored / PARTY_SLOTS;
    uint8_t old[sizeof(slots)];
    prefs.getBytes("party", old, stored);
    for (int i = 0; i < PARTY_SLOTS; i++)
      memcpy(&slots[i], old + i * oldStride, oldStride);
    save();   // rewrite in the current layout so this only happens once
  }
  // a blob written by an older/newer build could hold nonsense; drop anything
  // that is not a real Pokedex number rather than indexing DEX_TBL with it
  for (auto &s : slots) {
    if (s.dex < 1 || s.dex > DEX_COUNT) s.dex = 0;
    s.nick[sizeof(s.nick) - 1] = 0;
  }
  // The box is a separate key and simply absent on an older save, which leaves
  // it zeroed -- exactly what an empty box is.
  for (auto &s : box) s = PartyMon();
  size_t boxStored = prefs.getBytesLength("box");
  if (boxStored == sizeof(box)) {
    prefs.getBytes("box", box, sizeof(box));
  } else if (boxStored && boxStored % BOX_V323_SLOTS == 0 &&
             boxStored / BOX_V323_SLOTS <= sizeof(PartyMon)) {
    // Every legacy box through v3.23 had 18 records. Test that dimension BEFORE
    // dividing by today's 36 slots: 18 current-size records are also divisible
    // by 36 bytes, which otherwise invents 36 half-records on upgrade.
    const size_t oldStride = boxStored / BOX_V323_SLOTS;
    uint8_t *tmp = (uint8_t *)malloc(boxStored);
    if (tmp) {
      if (prefs.getBytes("box", tmp, boxStored) == boxStored) {
        for (int i = 0; i < BOX_V323_SLOTS; i++)
          memcpy(&box[i], tmp + i * oldStride, oldStride);
        boxNeedsRewrite = true;
      }
      free(tmp);
    }
  } else if (boxStored && boxStored % sizeof(PartyMon) == 0) {
    // A current-stride blob from a build with a different slot count. Preserve
    // the shared prefix in either direction; a downgrade must not erase slots
    // it cannot display, so only the smaller-to-larger case is rewritten below.
    uint8_t *tmp = (uint8_t *)malloc(boxStored);
    if (tmp) {
      if (prefs.getBytes("box", tmp, boxStored) == boxStored) {
        const size_t copy = boxStored < sizeof(box) ? boxStored : sizeof(box);
        memcpy(box, tmp, copy);
        boxNeedsRewrite = boxStored < sizeof(box);
      }
      free(tmp);
    }
  }
  // The checkpoint is the truth about the pair; everything above is the
  // migration path for a save written before it existed, and what a backup
  // restores. It goes LAST so it overwrites those reads, and the sanity clamps
  // below are re-applied because it bypassed the ones each branch already did.
  const bool pairLoaded = loadPair();
  if (!pairLoaded && (prefs.isKey("pbA") || prefs.isKey("pbB")))
    Serial.println("save: BOTH party checkpoints failed; using legacy keys");

  for (auto &s : slots) {
    if (s.dex < 1 || s.dex > DEX_COUNT) s.dex = 0;
    s.nick[sizeof(s.nick) - 1] = 0;
  }
  for (auto &s : box) {
    if (s.dex < 1 || s.dex > DEX_COUNT) s.dex = 0;
    s.nick[sizeof(s.nick) - 1] = 0;
  }
  // Rewrite only after the checkpoint has had the last word. Doing this in the
  // legacy branch above could overwrite one half of a newer checkpoint before
  // it was read. A future larger checkpoint is deliberately left untouched.
  if (boxNeedsRewrite && (!pairLoaded || loadedPairBoxSlots < BOX_SLOTS)) persist();
}

// ---------------------------------------------------------------------------
// THE PARTY AND THE BOX ARE ONE RECORD, because a swap moves a creature between
// them and that has to commit as one thing.
//
// They were two NVS keys written one after the other, so swapPartyBox() was two
// commits. A cut between them left the creature that came OUT of the box sitting
// in both places while the one that went in was lost -- a duplicate and a loss
// from a single interrupted swap, which is worse than issue #3 because it invents
// a creature. box_test reproduces it: 0 x CHARIZARD, 2 x PIKACHU.
//
// Keeping them as separate keys and reconciling on load was the alternative, and
// it cannot be done reliably: PartyMon has no identity, so two legitimately
// identical creatures are indistinguishable from one duplicated by a torn write.
// One atomic record removes the question.
//
// The legacy "party" and "box" keys are still READ for migration and still
// WRITTEN so a downgrade and the EXPORT backup keep working -- but they are no
// longer what the firmware believes, exactly as with the pet.
//
// Dimensions travel in the header for the same reason the player record carries
// them: PARTY_SLOTS, BOX_SLOTS and sizeof(PartyMon) have all grown, and a struct
// would move every field after whichever one changed.
static constexpr uint32_t PAIR_MAGIC = 0x3142504BUL;   // "KPB1"
static constexpr uint16_t PAIR_VERSION = 1;
static constexpr size_t PAIR_FIXED = 18;   // header + the three dimensions
// What THIS build writes. The +64 is slack for a field appended to the record's
// own header, not for extra slots -- growing those is what the heap path in
// loadPair() is for.
static constexpr size_t PAIR_CAP =
    PAIR_FIXED + sizeof(PartyMon) * (PARTY_SLOTS + BOX_SLOTS) + CKPT_CRC + 64;
// The format's OWN ceiling: the two dimensions at buf[14] and buf[15] are single
// bytes, so no record can legitimately describe more than this. A stored length
// beyond it is corruption, and refusing it is what stops a bogus length turning
// into a huge allocation.
static constexpr size_t PAIR_SANE_MAX =
    PAIR_FIXED + sizeof(PartyMon) * (255 + 255) + CKPT_CRC + 64;

bool Party::savePair() {
  const uint32_t nextGen = pairGeneration + 1;
  uint8_t buf[PAIR_CAP] = {};
  ckptWr16(buf + 12, (uint16_t)sizeof(PartyMon));
  buf[14] = PARTY_SLOTS;
  buf[15] = BOX_SLOTS;
  buf[16] = 0;                    // reserved
  buf[17] = 0;
  CkptCursor cur{ buf, PAIR_FIXED, sizeof(buf) - CKPT_CRC };
  cur.put(slots, sizeof(slots));
  cur.put(box, sizeof(box));
  if (cur.bad) return false;      // PAIR_CAP is derived, so this cannot happen

  uint16_t total = ckptSeal(buf, cur.at, PAIR_MAGIC, PAIR_VERSION, nextGen);
  const char *key = ckptSlot(nextGen, "pbA", "pbB");
  if (prefs.putBytes(key, buf, total) != total) return false;
    memset(buf, 0, sizeof(buf));
    if (ckptRead(prefs, key, PAIR_MAGIC, buf, sizeof(buf)) != total ||
      ckptRd32(buf + 8) != nextGen) return false;
  pairGeneration = nextGen;
  return true;
}

bool Party::loadPair() {
  loadedPairBoxSlots = 0;
  // A RECORD FROM A LATER BUILD IS LONGER THAN OURS, AND MUST STILL BE READ.
  //
  // Preferences::getBytes copies NOTHING when the stored blob exceeds the
  // caller's buffer -- it is not a truncating read -- so a buffer sized to this
  // build's own dimensions makes every downgrade fall back to the legacy
  // "party"/"box" keys. That fallback IS the torn-write path this record exists
  // to replace, which makes a tight buffer far worse than it looks; ckpt.h says
  // so above ckptRead() and it was still tight here. upgrade_test now pins it
  // with a hand-built record from a build with more slots.
  //
  // The stack cannot pay for open-ended tolerance: savePair() already holds two
  // PAIR_CAP buffers, and the format's own ceiling is ~24 KB. So the common case
  // stays on the stack and only an actually-larger record reaches for the heap --
  // the same shape Party::begin() already uses for an oversized legacy blob, and
  // this runs once at boot.
  size_t stored = prefs.getBytesLength("pbA");
  const size_t storedB = prefs.getBytesLength("pbB");
  if (storedB > stored) stored = storedB;
  uint8_t stack[PAIR_CAP];
  uint8_t *heap = nullptr;
  uint8_t *buf = stack;
  size_t cap = sizeof(stack);
  if (stored > cap) {
    if (stored > PAIR_SANE_MAX) {
      Serial.printf("save: party checkpoint claims %u bytes; refusing\n",
                    (unsigned)stored);
      return false;
    }
    heap = (uint8_t *)malloc(stored);
    if (!heap) return false;        // rather no read than a partial one
    buf = heap;
    cap = stored;
  }
  const bool ok = loadPairFrom(buf, cap);
  free(heap);
  return ok;
}

// The body, once, whichever buffer it was handed. Split out so the heap path
// above has exactly one exit that frees -- an early `return false` inside this
// logic is how a malloc leaks.
bool Party::loadPairFrom(uint8_t *buf, size_t cap) {
  uint16_t n = ckptReadNewest(prefs, "pbA", "pbB", PAIR_MAGIC, buf, cap);
  if (!n) return false;
  const size_t monBytes = ckptRd16(buf + 12);
  const size_t partyN = buf[14];
  const size_t boxN = buf[15];
  const size_t body = n - CKPT_CRC;
  // Checked BEFORE anything is copied, so a record that disagrees with its own
  // length is rejected whole rather than half-applied over what is already
  // loaded. A body LONGER than the dimensions describe is a later build's
  // appended field and is ignored.
  if (!monBytes || !partyN || !boxN ||
      PAIR_FIXED + monBytes * (partyN + boxN) > body) {
    Serial.println("save: party checkpoint dimensions do not match its length");
    return false;
  }
  pairGeneration = ckptRd32(buf + 8);
  loadedPairBoxSlots = (uint8_t)boxN;
  CkptCursor cur{ buf, PAIR_FIXED, body };
  // Per-record prefix copy, so a stored PartyMon shorter than this build's lands
  // in the front of each slot and the tail keeps its initialiser -- the same
  // migration Party::begin() does by length for the legacy blobs, and the reason
  // stateVersion defaults to 0.
  for (size_t i = 0; i < partyN; i++) {
    PartyMon m;
    cur.take(&m, sizeof(m), monBytes);
    if (i < PARTY_SLOTS) slots[i] = m;
  }
  for (size_t i = 0; i < boxN; i++) {
    PartyMon m;
    cur.take(&m, sizeof(m), monBytes);
    if (i < BOX_SLOTS) box[i] = m;
  }
  return !cur.bad;
}

// Both of these write the WHOLE pair. Every existing caller therefore became
// atomic without changing, and a swap is now one commit instead of two -- fewer
// writes than before, not more.
//
// THE CHECKPOINT AND BOTH LEGACY BLOBS, ONCE. save() and boxSave() are the two
// public names because the callers say which half they changed, but the record
// does not care -- and calling them one after the other, which swapPartyBox()
// did, wrote the ~1.2 KB pair TWICE for a single swap. That is the largest blob
// in the store taking double the wear (nvsinfo.cpp exists to warn about exactly
// this), and worse: after the second write BOTH alternating slots hold post-swap
// data, so the previous complete state -- the whole reason there are two -- is
// gone until the next save.
void Party::persist() {
  if (!savePair()) Serial.println("save: party checkpoint failed");
  // Still written, both of them, on every persist. They are what a backup
  // exports and what a downgrade reads, and writing only the half the caller
  // said it touched would leave the other stale in exactly the backups nobody
  // tests until they need one.
  prefs.putBytes("party", slots, sizeof(slots));
  prefs.putBytes("box", box, sizeof(box));
}

void Party::save() { persist(); }
void Party::boxSave() { persist(); }

// ---------------------------------------------------------------------------
// THE FOCUS SWAP'S WRITE-AHEAD JOURNAL.
//
// focusSwap() in the sketch exchanges the creature on the main screen with a
// banked one. It is a TRUE exchange -- the live pet takes the slot the newcomer
// vacates -- so it needs no free slot and, in RAM, cannot lose anything.
//
// On disk it could. The party slot lives in the pair record above; the live
// creature lives in the pet record in pet.cpp. Two keys, two generation
// counters, two commits, and no ordering that makes them one:
//
//   party first  -> a cut leaves the live creature in BOTH the slot and the pet
//                   record, and the creature that was coming out of the slot is
//                   simply gone. A duplicate and a loss from one interrupted
//                   swap -- the same fault box_test reproduces for the box, one
//                   abstraction layer up.
//   pet first    -> a cut loses the OUTGOING creature instead. No better.
//
// Reconciling on load cannot fix it either, for the reason recorded above
// savePair(): PartyMon carries no identity, so a creature duplicated by a torn
// write is indistinguishable from two legitimately identical creatures.
//
// So the INTENT is written first and holds BOTH sides. Recovery does not try to
// work out which half landed -- it forces the end state, which is idempotent and
// therefore safe to repeat after a crash during recovery itself.
//
// The one thing it must NOT do is replay a swap that finished long ago and undo
// the play since. That is what petGenBefore is for: the pet record's generation
// is a monotonic counter, so `loaded > armed + 1` means the pet half committed
// AND the game saved again afterwards, and the journal is merely stale.
static constexpr uint32_t FOCUS_MAGIC = 0x3146584BUL;   // "KXF1"
static constexpr uint16_t FOCUS_VERSION = 1;
// header + petGenBefore + slot + flags + the stored record size
static constexpr size_t FOCUS_FIXED = CKPT_HDR + 4 + 1 + 1 + 2;
static constexpr size_t FOCUS_CAP = FOCUS_FIXED + sizeof(PartyMon) * 2 + CKPT_CRC + 128;
#define FOCUS_FLAG_EGG 0x01

bool Party::focusBegin(uint8_t slot, const PartyMon &outgoing, bool outgoingIsEgg,
                       const PartyMon &incoming, uint32_t petGenBefore) {
  if (slot >= PARTY_SLOTS) return false;
  const uint32_t nextGen = focusGeneration + 1;
  uint8_t buf[FOCUS_CAP] = {};
  ckptWr32(buf + CKPT_HDR, petGenBefore);
  buf[CKPT_HDR + 4] = slot;
  buf[CKPT_HDR + 5] = outgoingIsEgg ? FOCUS_FLAG_EGG : 0;
  ckptWr16(buf + CKPT_HDR + 6, (uint16_t)sizeof(PartyMon));
  CkptCursor cur{ buf, FOCUS_FIXED, sizeof(buf) - CKPT_CRC };
  cur.put(&outgoing, sizeof(PartyMon));
  cur.put(&incoming, sizeof(PartyMon));
  if (cur.bad) return false;            // FOCUS_CAP is derived, so unreachable

  uint16_t total = ckptSeal(buf, cur.at, FOCUS_MAGIC, FOCUS_VERSION, nextGen);
  const char *key = ckptSlot(nextGen, "fxA", "fxB");
  if (prefs.putBytes(key, buf, total) != total) return false;
  // Read back before believing it. An unwritten journal is worse than none: the
  // caller would go on to make the two unprotected writes thinking it was safe.
  uint8_t back[FOCUS_CAP];
  if (ckptRead(prefs, key, FOCUS_MAGIC, back, sizeof(back)) != total ||
      ckptRd32(back + 8) != nextGen) return false;
  focusGeneration = nextGen;
  return true;
}

bool Party::focusPending(uint8_t *slot, PartyMon *outgoing, bool *outgoingWasEgg,
                         PartyMon *incoming, uint32_t *petGenBefore) {
  uint8_t buf[FOCUS_CAP];
  uint16_t n = ckptReadNewest(prefs, "fxA", "fxB", FOCUS_MAGIC, buf, sizeof(buf));
  if (!n) return false;
  const size_t monBytes = ckptRd16(buf + CKPT_HDR + 6);
  const size_t body = n - CKPT_CRC;
  // Validated BEFORE anything is copied out, so a record disagreeing with its
  // own length is refused whole rather than half-applied.
  if (!monBytes || FOCUS_FIXED + monBytes * 2 > body) {
    Serial.println("save: focus journal dimensions do not match its length");
    return false;
  }
  focusGeneration = ckptRd32(buf + 8);
  if (petGenBefore) *petGenBefore = ckptRd32(buf + CKPT_HDR);
  if (slot) *slot = buf[CKPT_HDR + 4];
  if (outgoingWasEgg) *outgoingWasEgg = (buf[CKPT_HDR + 5] & FOCUS_FLAG_EGG) != 0;
  CkptCursor cur{ buf, FOCUS_FIXED, body };
  PartyMon out, in;
  cur.take(&out, sizeof(out), monBytes);   // prefix rule, as everywhere else
  cur.take(&in, sizeof(in), monBytes);
  if (cur.bad) return false;
  if (outgoing) *outgoing = out;
  if (incoming) *incoming = in;
  return true;
}

void Party::focusEnd() {
  // BOTH slots, or ckptReadNewest() would keep finding the older one and replay
  // the same swap on every boot forever.
  //
  // The counter is NOT reset here. If the removes did not land -- a failing or
  // full NVS -- the record is still on the card, and zeroing the counter would
  // make the next focusBegin() write generation 1 into the same slot the stale
  // record occupies, so the two would be indistinguishable by age. focusPending()
  // re-reads the counter out of whatever record it finds, so leaving it alone
  // costs nothing and keeps the parity invariant in ckptSlot() true.
  prefs.remove("fxA");
  prefs.remove("fxB");
}

uint8_t Party::boxCount() const {
  uint8_t n = 0;
  for (auto &s : box)
    if (!s.empty()) n++;
  return n;
}

int Party::boxFirstFree() const {
  for (int i = 0; i < BOX_SLOTS; i++)
    if (box[i].empty()) return i;
  return -1;
}

bool Party::boxAdd(const PartyMon &m) {
  int i = boxFirstFree();
  if (i < 0) return false;
  box[i] = m;
  boxSave();
  return true;
}

void Party::boxReleaseAt(uint8_t i) {
  if (i >= BOX_SLOTS) return;
  box[i] = PartyMon();
  boxSave();
}

void Party::swapPartyBox(uint8_t partyIdx, uint8_t boxIdx) {
  if (partyIdx >= PARTY_SLOTS || boxIdx >= BOX_SLOTS) return;
  PartyMon t = slots[partyIdx];
  slots[partyIdx] = box[boxIdx];
  box[boxIdx] = t;
  persist();   // ONCE. This used to be save() then boxSave(), which is two full
               // pair writes for one swap -- see the note above persist().
}

uint8_t Party::count() const {
  uint8_t n = 0;
  for (auto &s : slots)
    if (!s.empty()) n++;
  return n;
}

int Party::firstFree() const {
  for (int i = 0; i < PARTY_SLOTS; i++)
    if (slots[i].empty()) return i;
  return -1;
}

bool Party::add(const PartyMon &m) {
  int i = firstFree();
  if (i < 0) return false;
  slots[i] = m;
  save();
  return true;
}

void Party::replaceAt(uint8_t i, const PartyMon &m) {
  if (i >= PARTY_SLOTS) return;
  slots[i] = m;
  save();
}

void Party::releaseAt(uint8_t i) {
  if (i >= PARTY_SLOTS) return;
  slots[i] = PartyMon();
  save();
}

// Mirrors calcStat() in pet.cpp: base + level + IV contribution + training.
// Kept in step with it by hand; there is no shared home for it that both the
// live pet and a frozen party member could use without dragging Pet in here.
static uint16_t calcStat(uint8_t base, uint8_t iv, uint16_t lvl, uint8_t tr) {
  return (uint16_t)base + lvl + (uint32_t)iv * lvl / 100 + tr;
}

uint16_t Party::atkOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bAtk, m.ivAtk, m.level, m.trAtk);
}
uint16_t Party::defOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bDef, m.ivDef, m.level, m.trDef);
}
uint16_t Party::speOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bSpe, m.ivSpe, m.level, m.trSpe);
}
uint16_t Party::vitOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bHp, m.ivHp, m.level, 10);
}
// Special reuses the physical IV and training, same rule as Pet::spaStat().
uint16_t Party::spaOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bSpA, m.ivAtk, m.level, m.trAtk);
}
uint16_t Party::spdOf(const PartyMon &m) const {
  return m.empty() ? 0 : calcStat(DEX_TBL[m.dex].bSpD, m.ivDef, m.level, m.trDef);
}
