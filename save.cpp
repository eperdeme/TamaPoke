#include "save.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "party.h"   // MAX_VAL is derived from the box, see below
#include "boxstore.h"

// Every key the firmware persists. Adding one here is the whole job of adding
// it to the backup; save_test fails if a key exists in NVS and not in this list.
//
// Legacy fields migrate on load; exports keep checkpoints and standalone keys.
const SaveField SAVE_FIELDS[] = {
  { "init", SK_BOOL },
  // The alternating checkpoints -- everything about the creature and the
  // player lives here now. This IS the save; nothing below is redundant with it.
  { "petA", SK_BYTES }, { "petB", SK_BYTES },
  { "plyA", SK_BYTES }, { "plyB", SK_BYTES },
  { "pbA", SK_BYTES },  { "pbB", SK_BYTES },
  // What has no checkpoint of its own
  { "bag", SK_BYTES },
  // settings, so a restored device plays the way it did
  { "lang", SK_U8 },    { "snd", SK_BOOL },   { "vol", SK_U8 },
  // Screen brightness, 1..BRIGHT_LEVELS. Here beside the volume it mirrors, and
  // NOT a bump of SAVE_VERSION: export omits a key that is absent and import
  // skips one it does not recognise, so adding a field is invisible to every
  // .tpsave already in a player's hands. Bumping the version would refuse all of
  // them outright -- saveImport() tests it for exact equality.
  { "brt", SK_U8 },
  // Which save the extra boxes on flash belong to (boxstore.h). Exported for
  // completeness, but a restore never keeps it: saveImport() installs a fresh
  // one, so boxes left over from the save being replaced read as empty.
  { "bxid", SK_U32 },
};
const uint16_t SAVE_FIELD_COUNT = sizeof(SAVE_FIELDS) / sizeof(SAVE_FIELDS[0]);

// The largest single value in the backup. It USED to be the box -- BOX_SLOTS
// whole records -- and is now the party+box CHECKPOINT, which holds both arrays
// plus its own header. Getting this wrong does not fail loudly: readField()
// returns -1 for anything longer, so the oversized key is quietly left out and
// the backup is silently partial. That already happened once, when this was the
// literal 768 and a PartyMon grew past 42 bytes.
//
// DERIVED, never a literal, and derived from the same expression party.cpp uses
// so the two cannot drift.
#define PAIR_BLOB_MAX (18 + sizeof(PartyMon) * (PARTY_SLOTS + BOX_SLOTS) + 2)
#define MAX_VAL (PAIR_BLOB_MAX > sizeof(PartyMon) * BOX_SLOTS \
                 ? PAIR_BLOB_MAX : sizeof(PartyMon) * BOX_SLOTS)

static uint16_t crc16(const uint8_t *p, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= (uint16_t)p[i] << 8;
    for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021)
                                                : (uint16_t)(c << 1);
  }
  return c;
}

static uint8_t widthOf(uint8_t kind) {
  switch (kind) {
    case SK_U8: case SK_I8: case SK_BOOL: return 1;
    case SK_U16: case SK_I16: return 2;
    case SK_U32: return 4;
    default: return 0;               // variable
  }
}

// Reads one field out of NVS. Returns its length, or -1 if the key is absent --
// absent is normal (a fresh save has no party) and is simply left out.
static int readField(Preferences &p, const SaveField &f, uint8_t *val) {
  if (!p.isKey(f.key)) return -1;
  switch (f.kind) {
    case SK_U8:   val[0] = p.getUChar(f.key, 0); return 1;
    case SK_I8:   val[0] = (uint8_t)p.getChar(f.key, 0); return 1;
    case SK_BOOL: val[0] = p.getBool(f.key, false) ? 1 : 0; return 1;
    case SK_U16: { uint16_t v = p.getUShort(f.key, 0); memcpy(val, &v, 2); return 2; }
    case SK_I16: { int16_t v = p.getShort(f.key, 0); memcpy(val, &v, 2); return 2; }
    case SK_U32: { uint32_t v = p.getUInt(f.key, 0); memcpy(val, &v, 4); return 4; }
    case SK_BYTES: {
      size_t n = p.getBytesLength(f.key);
      if (n > MAX_VAL) return -1;
      p.getBytes(f.key, val, n);
      return (int)n;
    }
    case SK_STR: {
      char tmp[64] = {0};
      p.getString(f.key, tmp, sizeof(tmp));
      size_t n = strlen(tmp);
      memcpy(val, tmp, n);
      return (int)n;
    }
  }
  return -1;
}

static void writeField(Preferences &p, const SaveField &f,
                       const uint8_t *val, uint16_t n) {
  switch (f.kind) {
    case SK_U8:   if (n == 1) p.putUChar(f.key, val[0]); break;
    case SK_I8:   if (n == 1) p.putChar(f.key, (int8_t)val[0]); break;
    case SK_BOOL: if (n == 1) p.putBool(f.key, val[0] != 0); break;
    case SK_U16:  if (n == 2) { uint16_t v; memcpy(&v, val, 2); p.putUShort(f.key, v); } break;
    case SK_I16:  if (n == 2) { int16_t v; memcpy(&v, val, 2); p.putShort(f.key, v); } break;
    case SK_U32:  if (n == 4) { uint32_t v; memcpy(&v, val, 4); p.putUInt(f.key, v); } break;
    case SK_BYTES: if (n) p.putBytes(f.key, val, n); break;
    case SK_STR: {
      char tmp[64] = {0};
      if (n >= sizeof(tmp)) return;
      memcpy(tmp, val, n);
      tmp[n] = 0;
      p.putString(f.key, tmp);
      break;
    }
  }
}

size_t saveExportSize() {
  size_t n = SAVE_HDR + 2;
  for (uint16_t i = 0; i < SAVE_FIELD_COUNT; i++)
    n += 1 + strlen(SAVE_FIELDS[i].key) + 1 + 2 + MAX_VAL / 16;
  n += BOX_EXTRA * (1 + 4 + 1 + 2 + BOX_SLOTS * sizeof(PartyMon));
  return n + 1024;                   // the two big blobs
}

size_t saveExport(uint8_t *out, size_t cap) {
  if (cap < SAVE_HDR + 2) return 0;
  Preferences p;
  p.begin("tamapoke", true);
  size_t at = SAVE_HDR;
  uint16_t count = 0;
  uint8_t val[MAX_VAL];
  for (uint16_t i = 0; i < SAVE_FIELD_COUNT; i++) {
    const SaveField &f = SAVE_FIELDS[i];
    int n = readField(p, f, val);
    if (n < 0) continue;             // absent: nothing to back up
    size_t klen = strlen(f.key);
    if (klen > 15) continue;         // NVS keys cannot be longer anyway
    if (at + 1 + klen + 1 + 2 + (size_t)n + 2 > cap) { p.end(); return 0; }
    out[at++] = (uint8_t)klen;
    memcpy(out + at, f.key, klen); at += klen;
    out[at++] = f.kind;
    out[at++] = (uint8_t)(n & 0xFF);
    out[at++] = (uint8_t)(n >> 8);
    memcpy(out + at, val, (size_t)n); at += (size_t)n;
    count++;
  }
  p.end();
  // The extra boxes, as keys of their own: "bx2" .. "bx10", each BOX_SLOTS raw
  // records. Not NVS keys, so not in SAVE_FIELDS -- but the wire format is the
  // same, which is what lets an installer that has never heard of them carry a
  // backup holding them, and an older firmware restore one by skipping them.
  // Only non-empty boxes are written; a restore treats a missing one as empty.
  for (uint8_t b = 0; b < BOX_EXTRA; b++) {
    if (boxStoreBoxEmpty(b)) continue;
    char key[8];
    snprintf(key, sizeof(key), "bx%u", (unsigned)(b + 2));
    const size_t klen = strlen(key);
    const size_t n = BOX_SLOTS * sizeof(PartyMon);
    if (at + 1 + klen + 1 + 2 + n + 2 > cap) return 0;
    out[at++] = (uint8_t)klen;
    memcpy(out + at, key, klen); at += klen;
    out[at++] = SK_BYTES;
    out[at++] = (uint8_t)(n & 0xFF);
    out[at++] = (uint8_t)(n >> 8);
    memcpy(out + at, boxStoreBoxData(b), n); at += n;
    count++;
  }
  out[0] = SAVE_MAGIC0; out[1] = SAVE_MAGIC1;
  out[2] = SAVE_MAGIC2; out[3] = SAVE_MAGIC3;
  out[4] = SAVE_VERSION;
  out[5] = (uint8_t)(count & 0xFF);
  out[6] = (uint8_t)(count >> 8);
  out[7] = 0;
  uint16_t c = crc16(out, at);
  out[at++] = (uint8_t)(c & 0xFF);
  out[at++] = (uint8_t)(c >> 8);
  return at;
}

// Looks a key up in the table. An unknown key is skipped rather than trusted:
// the kind on the wire decides nothing, the table does.
static const SaveField *fieldFor(const char *key, uint8_t kind) {
  for (uint16_t i = 0; i < SAVE_FIELD_COUNT; i++)
    if (SAVE_FIELDS[i].kind == kind && !strcmp(SAVE_FIELDS[i].key, key))
      return &SAVE_FIELDS[i];
  return nullptr;
}

bool saveImport(const uint8_t *in, size_t n) {
  if (n < SAVE_HDR + 2) return false;
  if (in[0] != SAVE_MAGIC0 || in[1] != SAVE_MAGIC1 ||
      in[2] != SAVE_MAGIC2 || in[3] != SAVE_MAGIC3) return false;
  if (in[4] != SAVE_VERSION) return false;
  uint16_t count = (uint16_t)in[5] | ((uint16_t)in[6] << 8);
  uint16_t want = (uint16_t)in[n - 2] | ((uint16_t)in[n - 1] << 8);
  if (crc16(in, n - 2) != want) return false;

  // PASS ONE: walk the whole thing and prove it parses. Nothing is written
  // yet -- a restore that fell over halfway would leave a save that is neither
  // the old one nor the new one, which is the one outcome worse than no backup.
  size_t at = SAVE_HDR;
  uint16_t seen = 0;
  while (at + 4 <= n - 2) {
    uint8_t klen = in[at];
    if (!klen || klen > 15 || at + 1 + klen + 3 > n - 2) return false;
    size_t vat = at + 1 + klen;
    uint8_t kind = in[vat];
    uint16_t vlen = (uint16_t)in[vat + 1] | ((uint16_t)in[vat + 2] << 8);
    if (vlen > MAX_VAL) return false;
    if (vat + 3 + vlen > n - 2) return false;
    uint8_t w = widthOf(kind);
    if (w && vlen != w) return false;          // a scalar of the wrong width
    at = vat + 3 + vlen;
    seen++;
  }
  if (at != n - 2 || seen != count) return false;

  // The extra boxes go to flash FIRST, under a brand-new save id that nothing
  // on the device carries yet. If this fails the restore is refused with NVS
  // untouched; if power is lost after it, the old save still holds the old id
  // and never sees the files just written. Only the NVS commit below makes them
  // the live boxes, by installing that id. See "Which save a box belongs to" in
  // boxstore.h for why a restore never reuses the backup's own id.
  const uint8_t *boxes[BOX_EXTRA] = { nullptr };
  bool anyBox = false;
  // Each box is BOX_SLOTS records of whatever size the exporting build had, so
  // the stride is read off the value's length rather than assumed: a backup
  // from before (or after) PartyMon grew still restores its boxes.
  uint16_t stride = 0;
  for (at = SAVE_HDR; at + 4 <= n - 2;) {
    uint8_t klen = in[at];
    char key[16] = {0};
    memcpy(key, in + at + 1, klen);
    size_t vat = at + 1 + klen;
    uint16_t vlen = (uint16_t)in[vat + 1] | ((uint16_t)in[vat + 2] << 8);
    unsigned no = 0;
    char tail = 0;
    if (in[vat] == SK_BYTES && vlen && vlen % BOX_SLOTS == 0 &&
        (!stride || vlen / BOX_SLOTS == stride) &&
        sscanf(key, "bx%u%c", &no, &tail) == 1 && no >= 2 && no < 2 + BOX_EXTRA) {
      boxes[no - 2] = in + vat + 3;
      stride = (uint16_t)(vlen / BOX_SLOTS);
      anyBox = true;
    }
    at = vat + 3 + vlen;
  }
  uint32_t newId = 0;
  if (anyBox) {
    if (!boxStoreImport(boxes, stride, &newId)) return false;
  } else {
    newId = boxStoreNewId();
  }

  // PASS TWO: it parses, so commit. Cleared first, or keys the backup does not
  // contain would survive from whatever save happened to be on the device.
  Preferences p;
  p.begin("tamapoke", false);
  p.clear();
  at = SAVE_HDR;
  while (at + 4 <= n - 2) {
    uint8_t klen = in[at];
    char key[16] = {0};
    memcpy(key, in + at + 1, klen);
    size_t vat = at + 1 + klen;
    uint8_t kind = in[vat];
    uint16_t vlen = (uint16_t)in[vat + 1] | ((uint16_t)in[vat + 2] << 8);
    const SaveField *f = fieldFor(key, kind);
    if (f) writeField(p, *f, in + vat + 3, vlen);
    at = vat + 3 + vlen;
  }
  p.putUInt("bxid", newId);          // last: this is what makes the boxes live
  p.end();
  return true;
}

// ---------------------------------------------------------------------------
// The durable reset intent. See the essay above RESET_NS in save.h.
//
// One scalar in a namespace of its own. A single put() is the smallest thing NVS
// commits, so arming cannot itself be torn: the flag is either there or it is
// not, and both readings are safe -- absent means the save stands.
static const char *RESET_KEY = "rst";
static const uint8_t RESET_WANTED = 0xA5;   // a value, not merely a present key,
                                            // so a stray zeroed byte is not a wipe

void resetArm() {
  Preferences p;
  if (!p.begin(RESET_NS, false)) {
    Serial.println("reset: could not record the intent -- NOT restarting into a wipe");
    return;
  }
  p.putUChar(RESET_KEY, RESET_WANTED);
  p.end();
}

bool resetArmed() {
  Preferences p;
  if (!p.begin(RESET_NS, true)) return false;
  const bool armed = p.getUChar(RESET_KEY, 0) == RESET_WANTED;
  p.end();
  return armed;
}

void resetComplete() {
  Preferences p;
  if (!p.begin(RESET_NS, false)) return;
  p.remove(RESET_KEY);
  p.end();
}

bool resetRecover() {
  if (!resetArmed()) return false;
  Preferences p;
  if (p.begin("tamapoke", false)) {
    p.clear();
    p.end();
  }
  // SECOND, deliberately. Acknowledging first would let a cut in between leave a
  // save that is half wiped with nothing left to say so; this way the worst case
  // is clearing an already-empty namespace one more time.
  resetComplete();
  Serial.println("reset: completed a requested wipe before loading anything");
  return true;
}
