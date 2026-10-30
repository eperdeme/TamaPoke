#include "boxstore.h"
#include "boxfs.h"
#include "crc32.h"
#include "dex.h"
#include <Preferences.h>
#include <string.h>
#include <stdlib.h>
#include <new>

// See boxstore.h for the design. Everything here is plain C++ over boxfs.h and
// Preferences, so the emulator runs it unchanged.

// On the heap, allocated at boxStoreBegin(): ~15.5 KB, which the board places in
// PSRAM (large allocations go there) rather than in the internal RAM every
// other static competes for. Nothing here exists until the store is mounted.
static PartyMon *extra = nullptr;
static uint32_t gen[BOX_EXTRA];       // generation of the newest good copy
static int8_t newest[BOX_EXTRA];      // 0 = A, 1 = B, -1 = neither
static uint32_t saveId = 0;
static bool ready = false;

static const uint8_t MAGIC[4] = { 'T', 'P', 'B', 'X' };
static const uint8_t JMAGIC[4] = { 'B', 'X', 'J', '2' };
#define JOURNAL_KEY "bxj"
#define ID_KEY "bxid"
// magic, kind, idx, extra u16, stride u16, two records, crc. The STRIDE is
// stored, not assumed: a swap a power cut interrupted must still be finished by
// the next boot even if that boot is a build whose PartyMon has grown.
#define JOURNAL_HDR 10
#define JOURNAL_SIZE (JOURNAL_HDR + 2 * sizeof(PartyMon) + 4)
#define JOURNAL_MAX (JOURNAL_HDR + 2 * 255 + 4)

static void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

static void pathOf(uint8_t box, int which, char *out, size_t cap) {
  snprintf(out, cap, "/tpbox/b%02u%c.bin", (unsigned)(box + 2), which ? 'b' : 'a');
}

static void sanitize(PartyMon &m) {
  if (m.dex < 1 || m.dex > DEX_COUNT) m = PartyMon();
  m.nick[sizeof(m.nick) - 1] = 0;
}

// Reads one copy. True if it is whole, ours, and for this box; the slots are
// copied into `into` (which the caller has cleared) by the prefix rule.
static bool readCopyInto(uint8_t *buf, size_t cap, uint8_t box, int which,
                         PartyMon *into, uint32_t *genOut) {
  char path[24];
  pathOf(box, which, path, sizeof(path));
  size_t got = 0;
  if (!bxfsRead(path, buf, cap, &got)) return false;
  if (got < BOXFILE_HDR + 4 || memcmp(buf, MAGIC, 4)) return false;
  if (buf[5] != box + 2) return false;
  const size_t slots = buf[6], stride = buf[7];
  if (!stride || BOXFILE_HDR + slots * stride + 4 != got) return false;
  if (crc32Update(0, buf, got - 4) != rd32(buf + got - 4)) return false;
  if (rd32(buf + 12) != saveId) return false;      // another save's box
  const size_t take = stride < sizeof(PartyMon) ? stride : sizeof(PartyMon);
  for (size_t i = 0; i < slots && i < BOX_SLOTS; i++) {
    into[i] = PartyMon();
    memcpy(&into[i], buf + BOXFILE_HDR + i * stride, take);
    sanitize(into[i]);
  }
  *genOut = rd32(buf + 8);
  return true;
}

// Room for a later build's larger box (64 slots of 64 bytes), so growing either
// dimension is read, not refused.
#define READ_CAP (BOXFILE_HDR + 64 * 64 + 4)
static bool readCopy(uint8_t box, int which, PartyMon *into, uint32_t *genOut) {
  uint8_t *buf = (uint8_t *)malloc(READ_CAP);
  if (!buf) return false;
  const bool ok = readCopyInto(buf, READ_CAP, box, which, into, genOut);
  free(buf);
  return ok;
}

static void loadBox(uint8_t box) {
  PartyMon *slots = &extra[box * BOX_SLOTS];
  for (int i = 0; i < BOX_SLOTS; i++) slots[i] = PartyMon();
  newest[box] = -1;
  gen[box] = 0;
  PartyMon *tmp = new (std::nothrow) PartyMon[BOX_SLOTS];
  if (!tmp) return;
  for (int which = 0; which < 2; which++) {
    for (int i = 0; i < BOX_SLOTS; i++) tmp[i] = PartyMon();
    uint32_t g = 0;
    if (!readCopy(box, which, tmp, &g)) continue;
    if (newest[box] < 0 || g > gen[box]) {
      memcpy(slots, tmp, BOX_SLOTS * sizeof(PartyMon));
      gen[box] = g;
      newest[box] = (int8_t)which;
    }
  }
  delete[] tmp;
}

// Writes `slots` as box `box` into the copy that is NOT the newest, stamped
// with `id`, and reads it back. Never touches the newest good copy.
static bool writeBox(uint8_t box, const PartyMon *slots, uint32_t id,
                     uint32_t g, int which) {
  uint8_t *buf = (uint8_t *)malloc(2 * BOXFILE_SIZE);
  if (!buf) return false;
  uint8_t *back = buf + BOXFILE_SIZE;
  memcpy(buf, MAGIC, 4);
  buf[4] = 1;
  buf[5] = (uint8_t)(box + 2);
  buf[6] = BOX_SLOTS;
  buf[7] = (uint8_t)sizeof(PartyMon);
  wr32(buf + 8, g);
  wr32(buf + 12, id);
  memcpy(buf + BOXFILE_HDR, slots, BOX_SLOTS * sizeof(PartyMon));
  wr32(buf + BOXFILE_SIZE - 4, crc32Update(0, buf, BOXFILE_SIZE - 4));
  char path[24];
  pathOf(box, which, path, sizeof(path));
  // Read it back: a store that claims success and keeps something else would
  // otherwise only be found out at the next boot, as a lost box.
  size_t got = 0;
  const bool ok = bxfsWrite(path, buf, BOXFILE_SIZE) &&
                  bxfsRead(path, back, BOXFILE_SIZE, &got) && got == BOXFILE_SIZE &&
                  !memcmp(buf, back, BOXFILE_SIZE);
  free(buf);
  return ok;
}

bool boxStoreSave(uint8_t box) {
  if (!ready || box >= BOX_EXTRA) return false;
  const int which = newest[box] == 0 ? 1 : 0;
  if (!writeBox(box, &extra[box * BOX_SLOTS], saveId, gen[box] + 1, which)) {
    Serial.printf("boxes: box %u NOT saved\n", (unsigned)(box + 2));
    return false;
  }
  gen[box]++;
  newest[box] = (int8_t)which;
  return true;
}

PartyMon &boxStoreSlot(uint16_t i) {
  static PartyMon none;
  if (!extra || i >= BOX_EXTRA_SLOTS) { none = PartyMon(); return none; }
  return extra[i];
}

bool boxStoreReady() { return ready; }
uint32_t boxStoreId() { return saveId; }

uint32_t boxStoreNewId() {
  uint32_t id = 0;
  while (!id) id = ((uint32_t)random(0x10000) << 16) ^ (uint32_t)random(0x10000) ^ (uint32_t)millis();
  return id;
}

// ---- the journal -------------------------------------------------------------
static bool journalWrite(uint8_t kind, uint8_t idx, uint16_t ex,
                         const PartyMon &a, const PartyMon &b) {
  uint8_t buf[JOURNAL_SIZE];
  memcpy(buf, JMAGIC, 4);
  buf[4] = kind;
  buf[5] = idx;
  memcpy(buf + 6, &ex, 2);
  const uint16_t stride = (uint16_t)sizeof(PartyMon);
  memcpy(buf + 8, &stride, 2);
  memcpy(buf + JOURNAL_HDR, &a, sizeof(PartyMon));
  memcpy(buf + JOURNAL_HDR + sizeof(PartyMon), &b, sizeof(PartyMon));
  wr32(buf + JOURNAL_SIZE - 4, crc32Update(0, buf, JOURNAL_SIZE - 4));
  Preferences p;
  if (!p.begin("tamapoke", false)) return false;
  const bool ok = p.putBytes(JOURNAL_KEY, buf, sizeof(buf)) == sizeof(buf);
  p.end();
  return ok;
}

static bool journalRead(uint8_t *kind, uint8_t *idx, uint16_t *ex,
                        PartyMon *a, PartyMon *b) {
  uint8_t buf[JOURNAL_MAX];
  Preferences p;
  if (!p.begin("tamapoke", true)) return false;
  const size_t n = p.isKey(JOURNAL_KEY) ? p.getBytesLength(JOURNAL_KEY) : 0;
  const bool got = n > JOURNAL_HDR + 4 && n <= sizeof(buf) && p.getBytes(JOURNAL_KEY, buf, n) == n;
  p.end();
  if (!got || memcmp(buf, JMAGIC, 4)) return false;
  uint16_t stride = 0;
  memcpy(&stride, buf + 8, 2);
  if (!stride || JOURNAL_HDR + 2u * stride + 4 != n) return false;
  if (crc32Update(0, buf, n - 4) != rd32(buf + n - 4)) return false;
  *kind = buf[4];
  *idx = buf[5];
  memcpy(ex, buf + 6, 2);
  // the prefix rule: a shorter record leaves the rest at its initialisers
  const size_t take = stride < sizeof(PartyMon) ? stride : sizeof(PartyMon);
  *a = PartyMon(); *b = PartyMon();
  memcpy(a, buf + JOURNAL_HDR, take);
  memcpy(b, buf + JOURNAL_HDR + stride, take);
  return true;
}

static void journalClear() {
  Preferences p;
  if (!p.begin("tamapoke", false)) return;
  p.remove(JOURNAL_KEY);
  p.end();
}

// Puts both sides where the journal says and persists them. True when both
// stores accepted it, which is when the journal may go.
static bool applySwap(Party &party, uint8_t kind, uint8_t idx, uint16_t ex,
                      const PartyMon &a, const PartyMon &b) {
  if (kind > 1 || ex >= BOX_EXTRA_SLOTS) return true;        // nonsense: drop it
  if (kind == 0 ? idx >= PARTY_SLOTS : idx >= BOX_SLOTS) return true;
  (kind == 0 ? party.slots[idx] : party.box[idx]) = a;
  extra[ex] = b;
  const bool boxOk = boxStoreSave((uint8_t)(ex / BOX_SLOTS));
  const bool pairOk = party.savePair();
  return boxOk && pairOk;
}

bool boxStoreSwap(Party &party, bool aIsBox1, uint8_t aIdx, uint16_t extraIdx) {
  if (!ready || extraIdx >= BOX_EXTRA_SLOTS) return false;
  if (aIsBox1 ? aIdx >= BOX_SLOTS : aIdx >= PARTY_SLOTS) return false;
  PartyMon &here = aIsBox1 ? party.box[aIdx] : party.slots[aIdx];
  const PartyMon newHere = extra[extraIdx];
  const PartyMon newThere = here;
  const uint8_t kind = aIsBox1 ? 1 : 0;
  if (!journalWrite(kind, aIdx, extraIdx, newHere, newThere)) {
    Serial.println("boxes: swap journal NOT written; nothing moved");
    return false;
  }
  // From here the swap is committed: if either write below fails, the journal
  // is still in NVS and the next boot finishes the job.
  if (applySwap(party, kind, aIdx, extraIdx, newHere, newThere)) journalClear();
  return true;
}

// ---- boot ------------------------------------------------------------------
bool boxStoreBegin(Party &party) {
  ready = false;
  if (!bxfsMount()) return false;
  if (!extra) extra = new (std::nothrow) PartyMon[BOX_EXTRA_SLOTS];
  if (!extra) return false;
  {
    Preferences p;
    if (!p.begin("tamapoke", false)) return false;
    saveId = p.getUInt(ID_KEY, 0);
    if (!saveId) {
      saveId = boxStoreNewId();
      // An id that cannot be recorded would change every boot, emptying the
      // boxes each time. Refuse the feature instead.
      if (p.putUInt(ID_KEY, saveId) != 4) { p.end(); saveId = 0; return false; }
    }
    p.end();
  }
  for (uint8_t b = 0; b < BOX_EXTRA; b++) loadBox(b);
  ready = true;
  uint8_t kind, idx; uint16_t ex; PartyMon a, b;
  if (journalRead(&kind, &idx, &ex, &a, &b)) {
    Serial.println("boxes: finishing a swap a power cut interrupted");
    if (applySwap(party, kind, idx, ex, a, b)) journalClear();
  } else {
    journalClear();     // a torn journal: the swap never started
  }
  return true;
}

void boxStoreReset() {
  ready = false;
  saveId = 0;
  if (extra) for (int i = 0; i < BOX_EXTRA_SLOTS; i++) extra[i] = PartyMon();
  for (uint8_t b = 0; b < BOX_EXTRA; b++) { gen[b] = 0; newest[b] = -1; }
}

// ---- backup ----------------------------------------------------------------
bool boxStoreBoxEmpty(uint8_t box) {
  if (!ready || box >= BOX_EXTRA) return true;
  for (int i = 0; i < BOX_SLOTS; i++)
    if (!extra[box * BOX_SLOTS + i].empty()) return false;
  return true;
}

const PartyMon *boxStoreBoxData(uint8_t box) {
  return extra && box < BOX_EXTRA ? &extra[box * BOX_SLOTS] : nullptr;
}

bool boxStoreImport(const uint8_t *const boxes[BOX_EXTRA], uint16_t stride, uint32_t *newId) {
  if (!stride) return false;
  if (!bxfsMount()) return false;
  const uint32_t id = boxStoreNewId();
  PartyMon *tmp = new (std::nothrow) PartyMon[BOX_SLOTS];
  if (!tmp) return false;
  bool ok = true;
  for (uint8_t b = 0; ok && b < BOX_EXTRA; b++) {
    if (!boxes[b]) continue;
    // The backup's records may be shorter or longer than this build's: the
    // prefix rule, record by record, as everywhere else a PartyMon is read.
    const size_t take = stride < sizeof(PartyMon) ? stride : sizeof(PartyMon);
    for (int i = 0; i < BOX_SLOTS; i++) {
      tmp[i] = PartyMon();
      memcpy(&tmp[i], boxes[b] + (size_t)i * stride, take);
      sanitize(tmp[i]);
    }
    // Both copies, generation 1 and 2: whatever was in either file before
    // belongs to another id and is about to be ignored anyway, but writing
    // both leaves nothing stale on flash to puzzle over.
    ok = writeBox(b, tmp, id, 1, 0) && writeBox(b, tmp, id, 2, 1);
  }
  delete[] tmp;
  if (ok && newId) *newId = id;
  return ok;
}
