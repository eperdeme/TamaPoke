#include "save.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "party.h"   // MAX_VAL is derived from the box, see below

// Every key the firmware persists. Adding one here is the whole job of adding
// it to the backup; save_test fails if a key exists in NVS and not in this list.
const SaveField SAVE_FIELDS[] = {
  // the creature
  { "init", SK_BOOL },  { "full", SK_U8 },    { "joy", SK_U8 },
  { "ene", SK_U8 },     { "hyg", SK_U8 },     { "poop", SK_U8 },
  { "wgt", SK_U8 },     { "age", SK_U32 },    { "dexn", SK_I16 },
  { "eggT2", SK_I16 },  { "crack", SK_U8 },   { "mist", SK_U8 },
  { "sleep", SK_BOOL }, { "lend", SK_U8 },    { "seen", SK_U32 },
  // The alternating checkpoints. Redundant with the legacy scalars beside them
  // on purpose: these are what the firmware actually BELIEVES on load, so a
  // backup without them would restore a save the device then half-ignores.
  { "petA", SK_BYTES }, { "petB", SK_BYTES },
  { "plyA", SK_BYTES }, { "plyB", SK_BYTES },
  { "pbA", SK_BYTES },  { "pbB", SK_BYTES },
  { "bond", SK_U8 },    { "nick", SK_STR },   { "froz", SK_BOOL },
  // individual values and training
  { "ivat", SK_U8 },    { "ivdf", SK_U8 },    { "ivsp", SK_U8 },
  { "ivhp", SK_U8 },    { "tatk", SK_U8 },    { "tdef", SK_U8 },
  { "tspe", SK_U8 },
  // moves
  { "mvs", SK_BYTES },  { "mvlv", SK_U8 },
  // flags
  { "bk", SK_BOOL },    { "shy", SK_BOOL },   { "eshy", SK_BOOL },
  { "stpk", SK_BOOL },  { "evop", SK_U8 },    { "slpa", SK_U8 },    { "rtpn", SK_BOOL },
  // the player: outlives every creature, which is exactly why it must be here
  { "tnam", SK_STR },   { "avtr", SK_U8 },    { "badg", SK_U16 },
  { "reg", SK_U8 },     { "eggR", SK_BYTES }, { "regn", SK_U8 },
  { "badgX", SK_BYTES },{ "badhX", SK_BYTES },
  { "badh", SK_U16 },   { "dexreg", SK_BYTES }, { "dexsh", SK_BYTES },
  { "strk", SK_U16 },   { "bstrk", SK_U16 },  { "cday", SK_U32 },
  { "medal", SK_U16 },  { "tmedal", SK_U16 }, { "mstone", SK_U16 },
  { "ghi", SK_U16 },    { "shi", SK_U16 },    { "qhi", SK_U16 },
  { "mhi", SK_U16 },    // the memory game's record, beside the other three
  // the banked creatures, and what you are carrying
  { "party", SK_BYTES }, { "box", SK_BYTES }, { "bag", SK_BYTES },
  // settings, so a restored device plays the way it did
  { "lang", SK_U8 },    { "snd", SK_BOOL },   { "vol", SK_U8 },
  // Screen brightness, 1..BRIGHT_LEVELS. Here beside the volume it mirrors, and
  // NOT a bump of SAVE_VERSION: export omits a key that is absent and import
  // skips one it does not recognise, so adding a field is invisible to every
  // .tpsave already in a player's hands. Bumping the version would refuse all of
  // them outright -- saveImport() tests it for exact equality.
  { "brt", SK_U8 },
};
const uint16_t SAVE_FIELD_COUNT = sizeof(SAVE_FIELDS) / sizeof(SAVE_FIELDS[0]);

// The largest single value in the backup, which is the box: BOX_SLOTS whole
// records. DERIVED, never a literal. It was written as 768 when a PartyMon was
// 42 bytes, and the moment the record grew the box silently stopped being
// restored while every other field still was -- a backup that is quietly
// partial is worse than none. save_test caught exactly that.
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
