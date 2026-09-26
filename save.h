#pragma once
#include <stdint.h>
#include <stddef.h>

// A whole-save backup, over the serial console.
//
// A run is weeks of real time living in one NVS partition, and there is now a
// lot in it: the creature, the party, the box, both badge ladders, the Pokedex,
// the streak, the trainer name. Nothing could get it back out.
//
// The backup is KEY-DRIVEN, not a struct of fields. SAVE_FIELDS lists every key
// the firmware stores along with its type, and both directions walk that one
// table through the ordinary Preferences API -- which means the identical code
// runs on the board and in the emulator, so all of it is testable here. An
// explicit struct would have been a second description of the save that drifts
// the moment somebody adds a field; `save_test` compares the table against the
// keys actually present after a save, so forgetting to add one fails a test
// rather than silently dropping it from everyone's backup.

#define SAVE_MAGIC0 'T'
#define SAVE_MAGIC1 'K'
#define SAVE_MAGIC2 'P'
#define SAVE_MAGIC3 'S'
#define SAVE_VERSION 1
#define SAVE_HDR 8
// Shared EXPORT/IMPORT ceiling, including every checkpoint.
//
// Raised from 4096 when the party and box became one atomic record, then from
// 8192 when the box doubled to 36 slots. saveExport() returns 0 rather than
// truncating, so outgrowing this turns EXPORT into "EXPORT FAIL" -- the backup
// simply stops existing. save_test prints the margin and fails below a quarter
// spare.
//
// Costs two static buffers of this size in the sketch, which is why it is not
// simply enormous.
#define SAVE_TRANSFER_MAX 12288

enum SaveKind : uint8_t {
  SK_U8 = 1, SK_I8, SK_BOOL, SK_U16, SK_I16, SK_U32, SK_BYTES, SK_STR,
};

struct SaveField {
  const char *key;
  uint8_t kind;
};
extern const SaveField SAVE_FIELDS[];
extern const uint16_t SAVE_FIELD_COUNT;

// Serialises the live save. Returns the number of bytes written, or 0 if it
// would not fit in cap.
size_t saveExport(uint8_t *out, size_t cap);

// Restores one. VALIDATES THE WHOLE BLOB FIRST and only then touches NVS: a
// half-applied restore over a good save would be worse than no backup at all.
// Returns false and changes nothing if the blob is not ours, is the wrong
// version, or fails its checksum. The caller must reload afterwards.
bool saveImport(const uint8_t *in, size_t n);

// Roughly what saveExport needs, for sizing a buffer.
size_t saveExportSize();

// ---------------------------------------------------------------------------
// A FACTORY RESET THAT SURVIVES LOSING POWER PART WAY THROUGH IT.
//
// Emptying NVS is not one write, and the window either side of it is dangerous
// in both directions. Before: the firmware still holds the old creature in RAM,
// so anything that saves in that window writes it straight back into the store
// that was just cleared -- saveInhibited guards the flush hooks, but not
// Party::save() or Inventory::save(). After: a cut mid-clear leaves a store that
// is neither the old save nor a new game, and nothing records which was intended.
//
// So the INTENT is recorded first, and the wipe happens at the TOP OF THE NEXT
// BOOT, before anything has opened the game namespace or loaded a creature into
// RAM. There is then no window at all: either the intent is there and the store
// is emptied before any of it is read, or it is not and the save stands.
//
// The intent lives in a namespace of its OWN, and both halves of that are
// load-bearing:
//   * not "tamapoke", because clear() on that namespace would erase the very
//     flag that records the wipe was asked for -- so a cut between the clear and
//     the acknowledgement would leave a half-wiped save with no intent to finish
//     it. Preferences::clear() empties one namespace, not the store.
//   * not in SAVE_FIELDS, because a backup carrying it would arm a wipe on the
//     device it was restored onto. saveExport/saveImport only ever open the game
//     namespace, so this is true by construction rather than by remembering.
#define RESET_NS "tpsys"

void resetArm();        // record the intent; the caller then restarts
bool resetArmed();
void resetComplete();   // acknowledge it, once the wipe has actually happened

// Empties the game namespace if a reset was asked for. Call FIRST in setup(),
// before anything opens it. Returns true if it wiped.
//
// IDEMPOTENT: a cut between the clear and the acknowledgement simply repeats a
// clear of an already-empty store on the next boot, which is why the intent is
// cleared second and not first.
bool resetRecover();
