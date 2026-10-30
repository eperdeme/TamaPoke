// The boxes past the first (boxstore.h).
//
// What has to hold, and what each block below proves:
//   1. One storage of BOX_TOTAL slots: box 1 fills first, then the extra boxes,
//      and both survive a reboot.
//   2. A box write torn by a power cut never costs the box: the other copy wins.
//   3. The files belong to ONE save. A reset, or a save with another id, sees
//      the extra boxes as empty without a single file being touched.
//   4. A swap across the seam is a real exchange, and a cut at ANY point of it
//      ends, after the next boot, with each creature in exactly one place --
//      never duplicated, never lost. A journal that never finished writing
//      means the swap never happened.
//   5. A backup carries the extra boxes; restoring it brings them back under a
//      fresh id, and restoring one WITHOUT them empties the extra boxes instead
//      of leaving the replaced save's creatures behind.
//   6. No file store: the game is exactly what it was, 36 slots.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "boxstore.h"
#include "boxfs.h"
#include "boxfs_host.h"
#include "save.h"
#include "dex.h"
#include "crc32.h"
#include <cstdio>
#include <cstring>
#include <vector>
uint32_t g_seed = 9; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0; volatile bool g_touchDown = false; bool wasPressed = false;
uint32_t millis() { return 0; } void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; } String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
static int bad = 0;
static void ck(bool ok, const char *w) { printf("%s  %s\n", ok ? "PASS" : "FAIL", w); if (!ok) bad++; }
static PartyMon mk(int dex, int lvl) {
  PartyMon m; m.dex = dex; m.level = lvl; m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20; return m;
}

// A reboot: RAM forgotten, the store unmounted, both reloaded from "flash".
static void reboot(Party &p) {
  boxStoreReset();
  bxfsHostUnmount();
  p = Party();
  p.begin();
  boxStoreBegin(p);
}
static void freshDevice(Party &p) {
  nvs().clear();
  bxfsFiles().clear();
  bxfsHostBroken() = false;
  bxfsHostWriteBudget() = -1;
  bxfsHostTearAt() = -1;
  nvsResumeWrites();
  reboot(p);
}
// How many times each species is held across the party and every box.
static int held(const Party &p, int dex) {
  int n = 0;
  for (auto &s : p.slots) if (s.dex == dex) n++;
  for (uint16_t i = 0; i < p.boxCap(); i++) if (p.boxAt(i).dex == dex) n++;
  return n;
}

int main() {
  Party p;

  // --- 1. one storage, box 1 first ------------------------------------------
  freshDevice(p);
  ck(boxStoreReady() && p.boxCap() == BOX_TOTAL, "with a file store the box holds BOX_TOTAL");
  ck(BOX_TOTAL == 360, "ten boxes of 36");
  {
    Preferences r; r.begin("tamapoke", true);
    ck(r.getUInt("bxid", 0) != 0 && r.getUInt("bxid", 0) == boxStoreId(),
       "a save id is minted and kept in NVS");
    r.end();
  }
  for (int i = 0; i < BOX_SLOTS; i++) p.boxAdd(mk(1 + i, 10));
  ck(p.boxCount() == BOX_SLOTS && bxfsFiles().empty(), "box 1 fills first, without touching flash");
  ck(p.boxAdd(mk(150, 70)), "the 37th goes into box 2");
  ck(p.boxAt(BOX_SLOTS).dex == 150 && !bxfsFiles().empty(), "slot 36 is box 2's first, written to flash");
  reboot(p);
  ck(p.boxAt(BOX_SLOTS).dex == 150 && p.boxAt(BOX_SLOTS).level == 70 && p.boxCount() == BOX_SLOTS + 1,
     "box 2 survives a reboot");
  {
    bool all = true;
    for (int i = BOX_SLOTS + 1; i < BOX_TOTAL; i++) all &= p.boxAdd(mk(1 + i % 900, 5));
    ck(all && p.boxCount() == BOX_TOTAL, "every one of the 360 slots can be filled");
    ck(!p.boxAdd(mk(3, 3)), "and a full box refuses, as before");
    reboot(p);
    ck(p.boxCount() == BOX_TOTAL && p.boxAt(BOX_TOTAL - 1).dex == 1 + (BOX_TOTAL - 1) % 900,
       "all of them survive a reboot");
  }

  // --- 2. a torn box write ----------------------------------------------------
  {
    const uint16_t slot = BOX_SLOTS * 3 + 5;      // box 4
    const int before = p.boxAt(slot).dex;
    p.boxReleaseAt(slot);                         // one good write: newest copy = empty
    ck(p.boxAt(slot).empty(), "a release empties the slot");
    bxfsHostTearAt() = 100;                       // the next write dies 100 bytes in
    p.boxAdd(mk(25, 99));                         // lands in that slot, write torn
    bxfsHostWriteBudget() = -1;
    reboot(p);
    ck(p.boxAt(slot).empty(), "a torn write leaves the previous copy of the box standing");
    ck(p.boxAt(slot - 1).dex == 1 + (slot - 1) % 900, "and the rest of that box with it");
    (void)before;
    p.boxAdd(mk(25, 99));
    reboot(p);
    ck(p.boxAt(slot).dex == 25, "the next write of that box lands normally");
  }

  // --- 3. the files belong to one save ----------------------------------------
  {
    const size_t files = bxfsFiles().size();
    // "Reiniciar partida": NVS cleared, flash untouched
    nvs().clear();
    reboot(p);
    ck(bxfsFiles().size() == files, "a reset does not touch the files");
    ck(p.boxCount() == 0, "yet the new game's boxes are all empty");
    ck(p.boxAdd(mk(7, 1)) && p.boxCount() == 1, "and fill normally");
  }

  // --- 4. swaps across the seam ----------------------------------------------
  freshDevice(p);
  p.slots[0] = mk(6, 50);   p.slots[1] = mk(9, 51);  p.savePair();
  for (int i = 0; i < BOX_SLOTS; i++) p.boxAdd(mk(100 + i, 20));
  p.boxAdd(mk(150, 70));                          // box 2, slot 0 -> index 36
  p.swapPartyBox(0, BOX_SLOTS);
  ck(p.slots[0].dex == 150 && p.boxAt(BOX_SLOTS).dex == 6, "party <-> box 2 is a real exchange");
  reboot(p);
  ck(p.slots[0].dex == 150 && p.boxAt(BOX_SLOTS).dex == 6, "and it persists");
  {
    Preferences r; r.begin("tamapoke", true);
    ck(!r.isKey("bxj"), "a finished swap leaves no journal behind");
    r.end();
  }
  // withdraw into an empty party slot, deposit into an empty extra slot
  p.swapPartyBox(5, BOX_SLOTS);
  ck(p.slots[5].dex == 6 && p.boxAt(BOX_SLOTS).empty(), "an empty slot on either side doubles as deposit/withdraw");

  // A cut at each point. 1: flash refuses (after the journal); 2: NVS refuses
  // the party after the box landed; 3: both refuse. Every one must end, after
  // a reboot, with the two creatures exchanged exactly once.
  for (int cut = 1; cut <= 3; cut++) {
    freshDevice(p);
    p.slots[2] = mk(25, 30); p.savePair();
    for (int i = 0; i < BOX_SLOTS; i++) p.boxAdd(mk(200 + i, 20));
    p.boxAdd(mk(133, 40));                        // index 36
    if (cut & 1) bxfsHostWriteBudget() = 0;       // the box file write fails
    if (cut & 2) nvsFailWritesAfter(1);           // journal lands, the party does not
    p.swapPartyBox(2, BOX_SLOTS);
    bxfsHostWriteBudget() = -1;
    nvsResumeWrites();
    reboot(p);
    char w[96];
    snprintf(w, sizeof(w), "cut %d: after the reboot the swap is finished, nothing duplicated or lost", cut);
    ck(p.slots[2].dex == 133 && p.boxAt(BOX_SLOTS).dex == 25 && held(p, 25) == 1 && held(p, 133) == 1, w);
  }
  {
    // the journal itself never landed: nothing may have moved
    freshDevice(p);
    p.slots[2] = mk(25, 30); p.savePair();
    for (int i = 0; i < BOX_SLOTS; i++) p.boxAdd(mk(200 + i, 20));
    p.boxAdd(mk(133, 40));
    nvsFailWritesAfter(0);
    p.swapPartyBox(2, BOX_SLOTS);
    nvsResumeWrites();
    ck(p.slots[2].dex == 25 && p.boxAt(BOX_SLOTS).dex == 133, "no journal, no swap -- not even in RAM");
    // a torn journal on flash reads as no journal at all
    Preferences w; w.begin("tamapoke", false);
    uint8_t junk[40] = { 'B', 'X', 'J', '1', 0 };
    w.putBytes("bxj", junk, sizeof(junk));
    w.end();
    reboot(p);
    ck(p.slots[2].dex == 25 && p.boxAt(BOX_SLOTS).dex == 133 && held(p, 25) == 1 && held(p, 133) == 1,
       "a torn journal is ignored, and nothing moves");
    Preferences r; r.begin("tamapoke", true);
    ck(!r.isKey("bxj"), "and is cleared");
    r.end();
  }

  // --- 5. backups ---------------------------------------------------------------
  {
    freshDevice(p);
    for (int i = 0; i < BOX_SLOTS; i++) p.boxAdd(mk(300 + i, 20));
    p.boxAdd(mk(493, 80));                        // box 2
    for (int i = 0; i < 40; i++) p.boxAdd(mk(400 + i, 30));   // into boxes 2 and 3
    const uint32_t oldId = boxStoreId();
    std::vector<uint8_t> blob(SAVE_TRANSFER_MAX);
    const size_t n = saveExport(blob.data(), blob.size());
    ck(n > 0, "a backup with extra boxes fits the transfer ceiling");
    printf("      backup with 3 boxes in use: %u bytes of %u\n", (unsigned)n, (unsigned)SAVE_TRANSFER_MAX);
    // the worst case: every box full
    for (int i = 0; i < BOX_TOTAL; i++) p.boxAdd(mk(1 + i % 900, 9));
    std::vector<uint8_t> full(SAVE_TRANSFER_MAX);
    const size_t nf = saveExport(full.data(), full.size());
    printf("      backup with every box full: %u bytes of %u\n", (unsigned)nf, (unsigned)SAVE_TRANSFER_MAX);
    ck(nf > 0 && nf <= SAVE_TRANSFER_MAX * 3 / 4, "even with all 360 slots full, a quarter of the ceiling to spare");

    // A different game is played, then the backup restored over it.
    nvs().clear();
    reboot(p);
    for (int i = 0; i < 50; i++) p.boxAdd(mk(600 + i, 1));
    ck(saveImport(blob.data(), n), "the backup restores");
    reboot(p);
    ck(boxStoreId() != oldId, "under a fresh save id, not the backup's own");
    ck(p.boxAt(BOX_SLOTS).dex == 493 && p.boxAt(BOX_SLOTS).level == 80, "box 2 is back");
    ck(p.boxCount() == BOX_SLOTS + 41 && held(p, 600) == 0,
       "exactly the backup's creatures: nothing from the game it replaced");

    // a backup taken with no extra boxes in use empties them on restore
    freshDevice(p);
    std::vector<uint8_t> small(SAVE_TRANSFER_MAX);
    const size_t ns = saveExport(small.data(), small.size());
    for (int i = 0; i < BOX_SLOTS + 10; i++) p.boxAdd(mk(700 + i, 1));
    ck(saveImport(small.data(), ns), "a backup without extra boxes restores");
    reboot(p);
    ck(p.boxCount() == 0, "and leaves the extra boxes empty, not holding the replaced save's");

    // the store refuses during the restore: refused whole, NVS untouched
    freshDevice(p);
    p.slots[0] = mk(151, 60); p.savePair();
    bxfsHostWriteBudget() = 0;
    ck(!saveImport(blob.data(), n), "a restore whose boxes cannot be written is refused");
    bxfsHostWriteBudget() = -1;
    reboot(p);
    ck(p.slots[0].dex == 151, "and the save on the device is untouched");
  }

  // --- 7. records of another size: a backup or a journal from a build whose ----
  // PartyMon is shorter or longer than this one's must still be read, by the
  // prefix rule -- the record is allowed to grow, so both directions happen.
  for (int stride : { 44, (int)sizeof(PartyMon) + 8 }) {
    char w[100];
    // a backup holding one extra box at that stride
    freshDevice(p);
    std::vector<uint8_t> blob;
    auto put8 = [&](uint8_t v) { blob.push_back(v); };
    for (char c : { 'T', 'K', 'P', 'S' }) put8((uint8_t)c);
    put8(SAVE_VERSION); put8(1); put8(0); put8(0);          // version, count 1
    put8(3); put8('b'); put8('x'); put8('2'); put8(SK_BYTES);
    const uint16_t vlen = (uint16_t)(BOX_SLOTS * stride);
    put8(vlen & 0xFF); put8(vlen >> 8);
    for (int i = 0; i < BOX_SLOTS; i++) {
      std::vector<uint8_t> rec(stride, 0);
      PartyMon m = mk(i == 0 ? 445 : 0, 55);
      memcpy(rec.data(), &m, stride < (int)sizeof(PartyMon) ? stride : sizeof(PartyMon));
      blob.insert(blob.end(), rec.begin(), rec.end());
    }
    uint16_t c = 0xFFFF;
    for (uint8_t b : blob) { c ^= (uint16_t)b << 8;
      for (int k = 0; k < 8; k++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1); }
    put8(c & 0xFF); put8(c >> 8);
    ck(saveImport(blob.data(), blob.size()), "a backup with box records of another size restores");
    reboot(p);
    snprintf(w, sizeof(w), "and its box 2 is back, read by the prefix rule (stride %d)", stride);
    ck(p.boxAt(BOX_SLOTS).dex == 445 && p.boxAt(BOX_SLOTS).level == 55 && p.boxCount() == 1, w);

    // an unfinished swap journalled by that build is still finished here
    p.slots[1] = mk(25, 30); p.savePair();
    std::vector<uint8_t> j = { 'B', 'X', 'J', '2', 0, 1, 0, 0, 0, 0 };
    const uint16_t ex = 0, st = (uint16_t)stride;
    memcpy(&j[6], &ex, 2); memcpy(&j[8], &st, 2);
    for (int side = 0; side < 2; side++) {
      std::vector<uint8_t> rec(stride, 0);
      PartyMon m = side == 0 ? mk(445, 55) : mk(25, 30);   // party gets 445, box gets 25
      memcpy(rec.data(), &m, stride < (int)sizeof(PartyMon) ? stride : sizeof(PartyMon));
      j.insert(j.end(), rec.begin(), rec.end());
    }
    const uint32_t jc = crc32Update(0, j.data(), j.size());
    for (int k = 0; k < 4; k++) j.push_back((uint8_t)(jc >> (8 * k)));
    { Preferences pw; pw.begin("tamapoke", false); pw.putBytes("bxj", j.data(), j.size()); pw.end(); }
    reboot(p);
    snprintf(w, sizeof(w), "a swap journalled with %d-byte records is finished after the reboot", stride);
    ck(p.slots[1].dex == 445 && p.boxAt(BOX_SLOTS).dex == 25 && held(p, 445) == 1 && held(p, 25) == 1, w);
  }

  // --- 6. no file store ---------------------------------------------------------
  {
    nvs().clear();
    bxfsFiles().clear();
    bxfsHostBroken() = true;
    reboot(p);
    ck(!boxStoreReady() && p.boxCap() == BOX_SLOTS, "without a file store the box is 36, as before");
    for (int i = 0; i < BOX_SLOTS; i++) p.boxAdd(mk(1 + i, 1));
    ck(!p.boxAdd(mk(40, 1)), "and the 37th is refused rather than lost somewhere");
    std::vector<uint8_t> b(SAVE_TRANSFER_MAX);
    ck(saveExport(b.data(), b.size()) > 0, "backups still work");
    bxfsHostBroken() = false;
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}
