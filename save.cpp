#include "save.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h>
#include <string.h>
#include "party.h"
#include "dex.h"
#include "collection_store.h"

// Namespace is part of every v2/v3 field, never inferred from its name.
const SaveField SAVE_FIELDS[] = {
  {"init", SK_BOOL}, {"petA", SK_BYTES}, {"petB", SK_BYTES},
  {"plyA", SK_BYTES}, {"plyB", SK_BYTES}, {"pbA", SK_BYTES}, {"pbB", SK_BYTES},
  {"fxA", SK_BYTES}, {"fxB", SK_BYTES},
  {"bag", SK_BYTES}, {"lang", SK_U8}, {"snd", SK_BOOL},
  {"vol", SK_U8}, {"brt", SK_U8},
  {"colv", SK_U8},
  // Upgrades deliberately retain these original keys. Preserve them too:
  // checkpoints remain authoritative, but an export must not silently drop
  // fields that are still present on an upgraded device.
  {"full", SK_U8}, {"joy", SK_U8}, {"ene", SK_U8}, {"hyg", SK_U8},
  {"poop", SK_U8}, {"wgt", SK_U8}, {"age", SK_U32}, {"dexn", SK_I16},
  {"eggT2", SK_I16}, {"spec", SK_I8}, {"eggT", SK_I8},
  {"crack", SK_U8}, {"mist", SK_U8}, {"sleep", SK_BOOL}, {"lend", SK_U8},
  {"seen", SK_U32}, {"bond", SK_U8}, {"nick", SK_STR}, {"froz", SK_BOOL},
  {"ivat", SK_U8}, {"ivdf", SK_U8}, {"ivsp", SK_U8}, {"ivhp", SK_U8},
  {"gatk", SK_U8}, {"gdef", SK_U8}, {"gspe", SK_U8},
  {"tatk", SK_U8}, {"tdef", SK_U8}, {"tspe", SK_U8}, {"mvs", SK_BYTES},
  {"mvlv", SK_U8}, {"bk", SK_BOOL}, {"shy", SK_BOOL}, {"eshy", SK_BOOL},
  {"stpk", SK_BOOL}, {"evop", SK_U8}, {"slpa", SK_U8}, {"rtpn", SK_BOOL},
  {"tnam", SK_STR}, {"avtr", SK_U8}, {"badg", SK_U16}, {"badh", SK_U16},
  {"reg", SK_U8}, {"eggR", SK_BYTES}, {"regn", SK_U8},
  {"badgX", SK_BYTES}, {"badhX", SK_BYTES}, {"dexreg", SK_BYTES}, {"dexsh", SK_BYTES},
  {"strk", SK_U16}, {"bstrk", SK_U16}, {"cday", SK_U32}, {"medal", SK_U16},
  {"tmedal", SK_U16}, {"mstone", SK_U16}, {"ghi", SK_U16}, {"shi", SK_U16},
  {"qhi", SK_U16}, {"mhi", SK_U16}, {"party", SK_BYTES}, {"box", SK_BYTES},
  {"cbA", SK_BYTES, 1}, {"cbB", SK_BYTES, 1}, {"active", SK_BOOL, 1},
};
const uint16_t SAVE_FIELD_COUNT = sizeof(SAVE_FIELDS) / sizeof(SAVE_FIELDS[0]);
static constexpr size_t MAX_VAL = COLLECTION_MAX_BYTES;
static const char *RESTORE_NS = "restore";
static const char *RESET_KEY = "rst";
static const uint8_t RESET_WANTED = 0xA5;

static bool restoreOpen(Preferences &p, bool ro = false) {
  return collectionStorageReady() && p.begin(RESTORE_NS, ro, COLLECTION_PARTITION);
}

static bool clearChecked(Preferences &p) { return p.clear(); }

static uint16_t crc16(const uint8_t *p, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= (uint16_t)p[i] << 8;
    for (int b = 0; b < 8; b++)
      c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}

static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p) {
  return u16(p) | ((uint32_t)u16(p + 2) << 16);
}
static uint8_t widthOf(uint8_t kind) {
  switch (kind) {
    case SK_U8: case SK_I8: case SK_BOOL: return 1;
    case SK_U16: case SK_I16: return 2;
    case SK_U32: return 4;
    default: return 0;
  }
}

// -1 is absent; -2 is a read error. A read error must NEVER omit a field.
static int readField(Preferences &p, const SaveField &f, uint8_t *val) {
  if (!p.isKey(f.key)) return -1;
  switch (f.kind) {
    case SK_U8:
      val[0] = p.getUChar(f.key, 0);
      return val[0] == p.getUChar(f.key, 255) ? 1 : -2;
    case SK_I8:
      val[0] = (uint8_t)p.getChar(f.key, 0);
      return val[0] == (uint8_t)p.getChar(f.key, -1) ? 1 : -2;
    case SK_BOOL:
      val[0] = p.getBool(f.key, false) ? 1 : 0;
      return val[0] == (p.getBool(f.key, true) ? 1 : 0) ? 1 : -2;
    case SK_U16: {
      uint16_t v = p.getUShort(f.key, 0);
      if (v != p.getUShort(f.key, UINT16_MAX)) return -2;
      memcpy(val, &v, 2); return 2;
    }
    case SK_I16: {
      int16_t v = p.getShort(f.key, 0);
      if (v != p.getShort(f.key, -1)) return -2;
      memcpy(val, &v, 2); return 2;
    }
    case SK_U32: {
      uint32_t v = p.getUInt(f.key, 0);
      if (v != p.getUInt(f.key, UINT32_MAX)) return -2;
      memcpy(val, &v, 4); return 4;
    }
    case SK_BYTES: {
      size_t n = p.getBytesLength(f.key);
      if (!n || n > MAX_VAL || p.getBytes(f.key, val, n) != n) return -2;
      return (int)n;
    }
    case SK_STR: {
      char text[64] = {};
      if (!p.getString(f.key, text, sizeof(text))) return -2;
      size_t n = strlen(text);
      if (n >= sizeof(text) - 1) return -2;
      memcpy(val, text, n);
      return (int)n;
    }
  }
  return -2;
}

static bool writeField(Preferences &p, const SaveField &f,
                       const uint8_t *v, size_t n, uint8_t *check) {
  size_t written = 0;
  switch (f.kind) {
    case SK_U8: written = p.putUChar(f.key, v[0]); break;
    case SK_I8: written = p.putChar(f.key, (int8_t)v[0]); break;
    case SK_BOOL: written = p.putBool(f.key, v[0] != 0); break;
    case SK_U16: { uint16_t x; memcpy(&x, v, 2); written = p.putUShort(f.key, x); break; }
    case SK_I16: { int16_t x; memcpy(&x, v, 2); written = p.putShort(f.key, x); break; }
    case SK_U32: { uint32_t x; memcpy(&x, v, 4); written = p.putUInt(f.key, x); break; }
    case SK_BYTES: written = p.putBytes(f.key, v, n); break;
    case SK_STR: {
      char text[64] = {};
      memcpy(text, v, n);
      written = p.putString(f.key, text);
      break;
    }
  }
  if (!written && !(f.kind == SK_STR && !n)) return false;
  int got = readField(p, f, check);
  return got >= 0 && (size_t)got == n && !memcmp(check, v, n);
}

struct Entry {
  char key[16] = {};
  uint8_t kind = 0, space = 0;
  size_t length = 0;
  const uint8_t *value = nullptr;
};

static bool nextEntry(const uint8_t *in, size_t end, size_t &at, Entry &e) {
  e = Entry{};
  if (in[4] >= 2) {
    if (at >= end) return false;
    e.space = in[at++];
    if (e.space > 1) return false;
  }
  if (at >= end) return false;
  uint8_t k = in[at++];
  size_t lengthBytes = in[4] >= 3 ? 4 : 2;
  if (!k || k > 15 || k + 1 + lengthBytes > end - at) return false;
  for (uint8_t i = 0; i < k; i++)
    if (in[at + i] < 33 || in[at + i] > 126) return false;
  memcpy(e.key, in + at, k); at += k;
  e.kind = in[at++];
  e.length = lengthBytes == 4 ? u32(in + at) : u16(in + at);
  at += lengthBytes;
  if (e.kind < SK_U8 || e.kind > SK_STR || e.length > MAX_VAL ||
      e.length > end - at) return false;
  uint8_t w = widthOf(e.kind);
  if ((w && e.length != w) || (e.kind == SK_BYTES && !e.length) ||
      (e.kind == SK_STR && (e.length >= 63 || memchr(in + at, 0, e.length))) ||
      (e.kind == SK_BOOL && in[at] > 1)) return false;
  e.value = in + at;
  at += e.length;
  return true;
}

static bool validCollection(const uint8_t *p, size_t n) {
  CollectionLayout layout;
  return collectionInspect(p, n, layout, PARTY_SLOTS, BOX_SLOTS, DEX_COUNT);
}

static bool validate(const uint8_t *in, size_t n) {
  if (!in || n < SAVE_HDR + 2 || n > SAVE_TRANSFER_MAX ||
      memcmp(in, "TKPS", 4) || (in[4] < 1 || in[4] > SAVE_VERSION) || in[7] ||
      crc16(in, n - 2) != u16(in + n - 2)) return false;
  size_t at = SAVE_HDR;
  uint16_t seen = 0;
  bool active = false, collection = false;
  while (at < n - 2) {
    size_t start = at;
    Entry e;
    if (!nextEntry(in, n - 2, at, e)) return false;
    if (e.space) {
      if (!strcmp(e.key, "active") && e.kind == SK_BOOL) active = e.value[0];
      else if ((!strcmp(e.key, "cbA") || !strcmp(e.key, "cbB")) && e.kind == SK_BYTES)
        collection |= validCollection(e.value, e.length);
      else return false;
    } else if (in[4] >= 2) {
      bool known = false;
      for (const SaveField &f : SAVE_FIELDS)
        if (!f.space && !strcmp(f.key, e.key) && f.kind == e.kind) known = true;
      if (!known) return false;
    }
    // Duplicate keys could verify against one value and finish with another.
    size_t prior = SAVE_HDR;
    while (prior < start) {
      Entry previous;
      if (!nextEntry(in, n - 2, prior, previous)) return false;
      if (previous.space == e.space && !strcmp(previous.key, e.key)) return false;
    }
    if (++seen == 0) return false;
  }
  return at == n - 2 && seen == u16(in + 5) &&
         (in[4] == 1 || (active && collection));
}

size_t saveExportSize() { return SAVE_TRANSFER_MAX; }

size_t saveExport(uint8_t *out, size_t cap) {
  if (!out || cap < SAVE_HDR + 2 || saveRestorePending()) return 0;
  if (cap > SAVE_TRANSFER_MAX) cap = SAVE_TRANSFER_MAX;
  Preferences game, collection;
  if (!game.begin("tamapoke", true) || !collectionOpen(collection, false)) return 0;
  if (!collection.isKey("active") || !collection.getBool("active", false) ||
      (!collection.isKey("cbA") && !collection.isKey("cbB"))) return 0;
  uint8_t *val = (uint8_t *)saveAlloc(MAX_VAL);
  if (!val) return 0;
  size_t at = SAVE_HDR;
  uint16_t count = 0;
  bool ok = true;
  for (const SaveField &f : SAVE_FIELDS) {
    int n = readField(f.space ? collection : game, f, val);
    if (n == -1) continue;
    size_t k = strlen(f.key);
    if (n < 0 || at + 7 + k + (size_t)n + 2 > cap) { ok = false; break; }
    out[at++] = f.space;
    out[at++] = (uint8_t)k;
    memcpy(out + at, f.key, k); at += k;
    out[at++] = f.kind;
    for (uint8_t b = 0; b < 4; b++) out[at++] = ((uint32_t)n >> (8 * b)) & 255;
    memcpy(out + at, val, n); at += n;
    count++;
  }
  free(val);
  if (!ok) return 0;
  memcpy(out, "TKPS", 4);
  out[4] = SAVE_VERSION; out[5] = count & 255; out[6] = count >> 8; out[7] = 0;
  uint16_t c = crc16(out, at);
  out[at++] = c & 255; out[at++] = c >> 8;
  return validate(out, at) ? at : 0;
}

bool saveRestorePending() {
  Preferences journal;
  // An inaccessible partition cannot safely be treated as an empty journal.
  if (!restoreOpen(journal, false)) return true;
  return journal.isKey("pending");
}

bool saveRecover() {
  Preferences journal;
  if (!restoreOpen(journal)) return false;
  if (!journal.isKey("pending")) return true;
  if (!journal.getBool("pending", false)) return false;
  size_t n = journal.getBytesLength("blob");
  if (n < SAVE_HDR + 2 || n > SAVE_TRANSFER_MAX) return false;
  uint8_t *blob = (uint8_t *)saveAlloc(n);
  uint8_t *check = (uint8_t *)saveAlloc(MAX_VAL);
  if (!blob || !check) { free(blob); free(check); return false; }
  bool ok = journal.getBytes("blob", blob, n) == n && validate(blob, n);
  Preferences game, collection, system;
  if (ok) ok = game.begin("tamapoke", false) && collectionOpen(collection) &&
               system.begin(RESET_NS, false);
  if (ok) ok = clearChecked(game) && clearChecked(collection) && clearChecked(system);
  size_t at = SAVE_HDR;
  while (ok && at < n - 2) {
    Entry e;
    ok = nextEntry(blob, n - 2, at, e);
    if (!ok) break;
    SaveField f{e.key, e.kind, e.space};
    ok = writeField(e.space ? collection : game, f, e.value, e.length, check);
  }
  // Nothing can load the restored game until the reset intent is also gone.
  if (ok) ok = !system.isKey(RESET_KEY);
  if (ok) {
    ok = journal.remove("pending") && !journal.isKey("pending");
    if (ok) journal.remove("blob"); // uncommitted debris is harmless
  }
  free(check); free(blob);
  return ok;
}

bool saveImport(const uint8_t *in, size_t n) {
  if (!validate(in, n) || saveRestorePending()) return false;
  Preferences journal;
  if (!restoreOpen(journal)) return false;
  uint8_t *check = (uint8_t *)saveAlloc(n);
  if (!check) return false;
  // An uncommitted attempt is not recovery data. Reclaim it BEFORE writing its
  // replacement, so NVS never needs room for two full staging blobs.
  bool ok = !journal.isKey("blob") || (journal.remove("blob") && !journal.isKey("blob"));
  if (ok) ok = journal.putBytes("blob", in, n) == n &&
               journal.getBytesLength("blob") == n &&
               journal.getBytes("blob", check, n) == n && !memcmp(in, check, n);
  free(check);
  if (!ok) return false;
  // This is the commit point. Before it the original game is untouched; after
  // it every boot replays the complete backup until every write has verified.
  if (journal.putBool("pending", true) != 1 || !journal.getBool("pending")) return false;
  journal.end();
  return saveRecover();
}

void resetArm() {
  Preferences p;
  if (p.begin(RESET_NS, false)) p.putUChar(RESET_KEY, RESET_WANTED);
}
bool resetArmed() {
  Preferences p;
  return p.begin(RESET_NS, true) && p.getUChar(RESET_KEY, 0) == RESET_WANTED;
}
void resetComplete() {
  Preferences p;
  if (p.begin(RESET_NS, false)) p.remove(RESET_KEY);
}
bool saveClearCollection() {
  Preferences collection, journal;
  return collectionOpen(collection) && restoreOpen(journal) &&
         clearChecked(collection) && clearChecked(journal);
}
bool resetRecover() {
  if (!resetArmed()) return false;
  Preferences game;
  if (!game.begin("tamapoke", false) || !clearChecked(game) || !saveClearCollection()) return false;
  resetComplete();
  return !resetArmed();
}
