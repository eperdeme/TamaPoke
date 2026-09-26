// Does an OLD save survive an update?
//
// This is the question every existing player asks, and the answer has to be
// proven rather than asserted: people have weeks of real time in these saves,
// and "install without erase" is the path the web installer recommends.
//
// So this builds a save the way an OLDER build would have written it -- a short
// Pokedex bitmap, a party blob with the pre-moves record size, and none of the
// keys added since (box, region, per-region badges, movesets) -- and then loads
// it with the current code and checks nothing was lost or misread.
//
// The dangerous one is the party. Party::begin() infers the old record size
// from the blob length, and a misread there does not fail loudly: it invents a
// party out of misaligned bytes, and a stray byte is often a valid dex number.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "save.h"
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <vector>
uint32_t g_seed=13; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

// A PartyMon as it was BEFORE moves[] was appended: identical fields in
// identical order, just without the trailing moves[]. It has to be an exact
// PREFIX of the current struct or the length-based migration cannot work -- and
// getting `medals` wrong here is what made this test fail against correct code
// the first time, by putting nick[] two bytes out.
struct OldPartyMon {
  int16_t dex;
  uint16_t level;
  uint16_t medals;
  uint8_t ivAtk, ivDef, ivSpe, ivHp;
  uint8_t trAtk, trDef, trSpe;
  uint8_t shiny;
  char nick[12];
};

int main(){
  // If this ever fails, the migration cannot work at all: Party::begin() infers
  // the old record size from the blob length and copies that many bytes into
  // the front of each new record.
  ck(sizeof(OldPartyMon) < sizeof(PartyMon),
     "the old record is shorter than the new one");
  ck(offsetof(OldPartyMon, nick) == offsetof(PartyMon, nick),
     "and every field before moves[] sits at the same offset");

  // --- write a save the way an older build would have
  {
    Preferences p;
    p.begin("tamapoke", false);
    p.clear();
    p.putBool("init", true);
    p.putUChar("full", 71); p.putUChar("joy", 63); p.putUChar("ene", 55);
    p.putUChar("hyg", 88);
    p.putShort("dexn", 59);                 // an Arcanine
    p.putUInt("age", 39UL * MINUTES_PER_LEVEL);   // level() is 1 + age/rate
    p.putUChar("ivat", 27); p.putUChar("ivdf", 14);
    p.putUChar("ivsp", 30); p.putUChar("ivhp", 9);
    p.putUChar("tatk", 55); p.putUChar("tdef", 12); p.putUChar("tspe", 61);
    p.putUShort("strk", 9); p.putUShort("bstrk", 14);
    p.putUShort("tmedal", 23);
    p.putUShort("badg", 0x001F);             // five Kanto badges, easy
    p.putUShort("badh", 0x0003);
    p.putString("nick", "BLAZE");
    p.putUChar("bond", 64);

    // the Pokedex as 19 bytes, which is what 151 species needed
    uint8_t oldDex[19] = {0};
    // byte b bit k is dex b*8 + k + 1, so dex 151 is byte 18 bit 6
    oldDex[0] = 0xFF; oldDex[1] = 0xFF; oldDex[7] = 0x40; oldDex[18] = 0x40;
    p.putBytes("dexreg", oldDex, sizeof(oldDex));
    uint8_t oldShiny[19] = {0};
    oldShiny[3] = 0x08;
    p.putBytes("dexsh", oldShiny, sizeof(oldShiny));

    // a party in the OLD record layout, no moves[]
    OldPartyMon op[PARTY_SLOTS];
    memset(op, 0, sizeof(op));
    for (int i = 0; i < 4; i++) {
      op[i].dex = (int16_t)(20 + i * 11);
      op[i].level = (uint16_t)(44 + i);
      op[i].ivAtk = op[i].ivDef = op[i].ivSpe = op[i].ivHp = (uint8_t)(15 + i);
      op[i].trAtk = 30; op[i].shiny = (i == 1); op[i].medals = (uint16_t)(3 + i);
      snprintf(op[i].nick, sizeof(op[i].nick), "OLD%d", i);
    }
    p.putBytes("party", op, sizeof(op));
    p.end();
    printf("      wrote an old save: %zu-byte party records, 19-byte dex\n",
           sizeof(OldPartyMon));
  }

  // --- now load it with the CURRENT code, as an update would
  Pet pet; Party pty;
  pet.begin(); pty.begin();

  ck(pet.speciesId == 59, "the creature survives the update");
  ck(pet.level() == 40, "at the level it was");
  ck(pet.ivAtk == 27 && pet.ivDef == 14 && pet.ivSpe == 30 && pet.ivHp == 9,
     "with its IVs intact");
  ck(pet.trAtk == 55 && pet.trDef == 12 && pet.trSpe == 61, "and its training");
  ck(!strcmp(pet.nick, "BLAZE"), "the nickname survives");
  ck(pet.streak == 9 && pet.bestStreak == 14 && pet.totalMedals == 23,
     "streak and medals survive");
  ck(pet.bond == 64, "and the bond");

  // badges: the old key holds Kanto and must still read as Kanto
  ck(pet.badgeMask(0, false) == 0x001F, "the Kanto badges are still there");
  ck(pet.badgeMask(0, true) == 0x0003, "including the hard-mode ones");
  ck(pet.badgeMask(1, false) == 0 && pet.badgeMask(2, false) == 0,
     "and the new regions start empty rather than inheriting them");

  // the Pokedex: a 19-byte blob must land in the front of the 49-byte array
  ck(pet.isRegistered(1) && pet.isRegistered(8) && pet.isRegistered(16),
     "the old Pokedex bits keep their meaning");
  ck(pet.isRegistered(151), "including the very last Kanto species");
  ck(pet.isShinyRegistered(28), "and the shiny registrations");
  bool strayGen2 = false;
  for (int d = 152; d <= DEX_COUNT; d++) if (pet.isRegistered(d)) strayGen2 = true;
  ck(!strayGen2, "and nothing in Johto or Hoenn is falsely registered");

  // THE DANGEROUS ONE: a party blob of the old stride
  ck(pty.count() == 4, "the party still has four members");
  bool ok = true;
  for (int i = 0; i < 4; i++) {
    const PartyMon &m = pty.slots[i];
    char want[8]; snprintf(want, sizeof(want), "OLD%d", i);
    if (m.dex != 20 + i * 11 || m.level != 44 + i) ok = false;
    if (strcmp(m.nick, want)) ok = false;
    if (m.ivAtk != 15 + i) ok = false;
  }
  ck(ok, "every banked creature keeps its species, level, IVs and nickname");
  ck(pty.slots[2].medals == 5, "and the medals it earned in life");
  bool freshMoves = true;
  for (int i = 0; i < 4; i++)
    if (pty.slots[i].moves[0] || pty.slots[i].moves[3]) freshMoves = false;
  ck(freshMoves, "the moveset a pre-moves save never had comes up empty, not garbage");
  ck(pty.slots[1].shiny, "a banked shiny is still shiny");
  ck(pty.boxCount() == 0, "the box, which did not exist then, comes up empty");

  // the new settings take their defaults rather than garbage
  ck(pet.region < REGION_COUNT, "the egg region defaults sanely");

  // --- and the whole thing can then be backed up and restored
  {
   static uint8_t buf[SAVE_TRANSFER_MAX];
    size_t n = saveExport(buf, sizeof(buf));
    ck(n > 0, "an upgraded save exports");
    pet.factoryReset();
    ck(saveImport(buf, n), "and imports again");
    Pet p2; Party q2; p2.begin(); q2.begin();
    ck(p2.speciesId == 59 && !strcmp(p2.nick, "BLAZE") && q2.count() == 4,
       "with the upgraded contents intact");
  }

  // ---- DOWNGRADE: a save written by a build with a BIGGER dex
  //
  // Every migration above tests GROWTH, which is the safe direction: getBytes
  // copies the shorter stored blob into the front of the bigger array and the
  // rest keeps its zero initialiser. Shrinking is the dangerous one and was
  // never tested, because the emulator's getBytes used to truncate. Hardware
  // does not -- Preferences::getBytes reads the stored length first and returns
  // 0 WITHOUT COPYING if it exceeds the caller's buffer:
  //
  //     if (len > maxLen) { log_e("not enough space in buffer"); return 0; }
  //
  // So flashing a build with a smaller DEX_COUNT over a newer save leaves
  // dexReg, dexShinyReg, the badge arrays and eggByRegion ENTIRELY ZERO: the
  // Pokedex, every badge past Kanto and the egg memory, gone. The creature
  // survives, because it is all scalars. That is what "it rolled my game back"
  // looks like from the player's side.
  {
    Preferences pr;
    pr.begin("tamapoke", false);
    // a dex bitmap from a build whose DEX_COUNT was larger than ours
    std::vector<uint8_t> big(sizeof(((Pet*)nullptr)->dexReg) + 24, 0);
    big[(25 - 1) >> 3] |= 1 << ((25 - 1) & 7);      // PIKACHU registered
    big[(151 - 1) >> 3] |= 1 << ((151 - 1) & 7);    // MEW registered
    pr.putBytes("dexreg", big.data(), big.size());
    std::vector<uint8_t> bigb(sizeof(((Pet*)nullptr)->badgesX) + 8, 0);
    bigb[0] = 0xFF;                                  // all eight Johto badges
    pr.putBytes("badgX", bigb.data(), bigb.size());
    pr.end();

    // and a party/box blob from a build with MORE slots at the same stride
    //
    // TWO SAVES, not one, because the party and box gained a CHECKPOINT (pbA/pbB)
    // and the checkpoint is deliberately the truth about the pair -- the legacy
    // "party"/"box" blobs are the migration path and what a downgrade reads. This
    // block used to write only the legacy blobs and then assert on them, which
    // after the checkpoint landed described a save no build can produce: a device
    // whose party came from a LATER build would carry that build's checkpoint too,
    // and the checkpoint would rightly win. It read as a firmware bug and was a
    // stale test.
    //
    // So both real shapes are covered: a save from BEFORE checkpoints existed
    // (legacy keys only), and a save from a build with more slots (checkpoint
    // present, and oversized).
    {
      // -- shape 1: legacy keys only, as a pre-checkpoint build left them
      Preferences pr2; pr2.begin("tamapoke", false);
      pr2.remove("pbA");
      pr2.remove("pbB");
      std::vector<uint8_t> bigp(sizeof(PartyMon) * (PARTY_SLOTS + 3), 0);
      PartyMon m; m.dex = 149; m.level = 100;
      snprintf(m.nick, sizeof(m.nick), "DRAGO");
      memcpy(bigp.data(), &m, sizeof(m));
      pr2.putBytes("party", bigp.data(), bigp.size());
      std::vector<uint8_t> bigb2(sizeof(PartyMon) * (BOX_SLOTS + 5), 0);
      PartyMon b; b.dex = 6; b.level = 100;
      memcpy(bigb2.data(), &b, sizeof(b));
      pr2.putBytes("box", bigb2.data(), bigb2.size());
      pr2.end();
      Party pq; pq.begin();
      ck(pq.slots[0].dex == 149 && pq.slots[0].level == 100,
         "a party written by a build with MORE slots keeps its first six");
      ck(pq.box[0].dex == 6, "and so does the box");
    }
    {
      // -- shape 2: a CHECKPOINT from a build with more slots. Its dimensions
      // travel in its own header, so the extra records are consumed and dropped
      // rather than misaligning ours. Built by hand: the running build cannot
      // write a record with dimensions it does not have.
      const uint8_t moreParty = PARTY_SLOTS + 2, moreBox = BOX_SLOTS + 4;
      const size_t mon = sizeof(PartyMon);
      const size_t fixed = 18;                     // PAIR_FIXED
      std::vector<uint8_t> blob(fixed + mon * (moreParty + moreBox) + 2, 0);
      memcpy(blob.data() + 0, "\x4B\x50\x42\x31", 4);   // PAIR_MAGIC "KPB1"
      uint16_t ver = 1; memcpy(blob.data() + 4, &ver, 2);
      uint16_t total = (uint16_t)blob.size(); memcpy(blob.data() + 6, &total, 2);
      uint32_t gen = 9; memcpy(blob.data() + 8, &gen, 4);
      uint16_t ms = (uint16_t)mon; memcpy(blob.data() + 12, &ms, 2);
      blob[14] = moreParty;
      blob[15] = moreBox;
      // slot 0 of the party, and slot 0 of the box, are what we assert on
      PartyMon pm; pm.dex = 144; pm.level = 70;
      snprintf(pm.nick, sizeof(pm.nick), "ICER");
      memcpy(blob.data() + fixed, &pm, mon);
      PartyMon bm; bm.dex = 9; bm.level = 55;
      memcpy(blob.data() + fixed + mon * moreParty, &bm, mon);
      // CRC-16/CCITT-FALSE over everything before the trailing crc, same as ckpt.h
      uint16_t crc = 0xFFFF;
      for (size_t i = 0; i + 2 < blob.size(); i++) {
        crc ^= (uint16_t)blob[i] << 8;
        for (int k = 0; k < 8; k++)
          crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
      }
      memcpy(blob.data() + blob.size() - 2, &crc, 2);

      Preferences pr3; pr3.begin("tamapoke", false);
      pr3.putBytes("pbA", blob.data(), blob.size());
      pr3.remove("pbB");
      pr3.end();
      Party pq2; pq2.begin();
      ck(pq2.slots[0].dex == 144 && pq2.slots[0].level == 70 &&
         !strcmp(pq2.slots[0].nick, "ICER"),
         "a pair CHECKPOINT from a build with more slots keeps its first six");
      ck(pq2.box[0].dex == 9 && pq2.box[0].level == 55,
         "and the supported prefix of its box");
    }

    Pet q; q.begin();
    ck(q.isRegistered(25) && q.isRegistered(151),
       "a Pokedex written by a LATER build still loads after downgrading");
    ck(q.badgeCountIn(1, false) == 8,
       "and so do the badges it recorded for a region this build still has");
  }

  // ---- A PLAYER CHECKPOINT FROM BEFORE memoHi EXISTED
  //
  // The player record gained a scalar, which moves where its ARRAYS start. That is
  // the dangerous kind of change: loadPlayerSnapshot() validates the array
  // dimensions against the stored body length, so a build that simply raised its
  // idea of the fixed-block size would find every existing record too short,
  // reject it, and fall back to the legacy keys -- the torn-write path the
  // checkpoint exists to replace, on every device in the field.
  //
  // The fix is that the fixed-block length now TRAVELS IN THE RECORD, at the byte
  // that used to be a reserved zero. A v1 record reads 0 there and 0 means 42.
  // This builds such a record by hand -- the running build cannot write one -- and
  // proves the badges, the streak and the three older minigame records all survive
  // while the new one simply reads as "no record yet".
  {
    Preferences pr; pr.begin("tamapoke", false);
    pr.remove("plyA"); pr.remove("plyB");

    const size_t FIXED_V1 = 42;
    const size_t dexBytes = sizeof(((Pet*)nullptr)->dexReg);
    // SIX, not GYM_REGIONS - 1. The record has to be built the way the OLD build
    // wrote it, and that build had seven ladders -- so it stored six extra badge
    // masks where this one stores eight. Using the current count would quietly
    // stop testing the array growth that Galar and Paldea caused, which is the
    // other half of what could go wrong here.
    const size_t badgeN = 6;
    static_assert(GYM_REGIONS - 1 > 6,
                  "this fixture is the SEVEN-ladder layout; if the build shrank to "
                  "that, it is no longer testing growth");
    const size_t nameN = 12;
    const size_t body = FIXED_V1 + 2*dexBytes + 4*badgeN + 2*REGION_COUNT + nameN;
    std::vector<uint8_t> b(body + 2, 0);
    memcpy(b.data() + 0, "TKY1", 4);                       // PLAYER_MAGIC
    uint16_t v = 1;  memcpy(b.data() + 4, &v, 2);          // version 1
    uint16_t tot = (uint16_t)b.size(); memcpy(b.data() + 6, &tot, 2);
    uint32_t gen = 3; memcpy(b.data() + 8, &gen, 4);
    uint16_t db = (uint16_t)dexBytes; memcpy(b.data() + 12, &db, 2);
    b[14] = REGION_COUNT;
    b[15] = (uint8_t)badgeN;
    b[16] = (uint8_t)nameN;
    b[17] = 2;                                             // avatar
    b[18] = 0;                                             // region KANTO
    b[19] = 0;                    // THE RESERVED ZERO: "the fixed block is 42"
    uint16_t badg = 0x00FF; memcpy(b.data() + 20, &badg, 2);
    uint16_t badh = 0x0003; memcpy(b.data() + 22, &badh, 2);
    uint16_t strk = 21;     memcpy(b.data() + 24, &strk, 2);
    uint16_t bstk = 34;     memcpy(b.data() + 26, &bstk, 2);
    uint32_t cday = 1000;   memcpy(b.data() + 28, &cday, 4);
    uint16_t tmed = 7;      memcpy(b.data() + 32, &tmed, 2);
    uint16_t mstn = 7;      memcpy(b.data() + 34, &mstn, 2);
    uint16_t ghi = 41;      memcpy(b.data() + 36, &ghi, 2);
    uint16_t shi = 52;      memcpy(b.data() + 38, &shi, 2);
    uint16_t qhi = 13;      memcpy(b.data() + 40, &qhi, 2);
    // the arrays begin at 42, NOT at 44 -- that is the whole point
    size_t at = FIXED_V1;
    std::vector<uint8_t> dex(dexBytes, 0);
    dex[(25 - 1) >> 3] |= 1 << ((25 - 1) & 7);             // PIKACHU
    memcpy(b.data() + at, dex.data(), dexBytes); at += dexBytes;   // dexReg
    at += dexBytes;                                                // dexShinyReg
    uint16_t jx = 0x000F; memcpy(b.data() + at, &jx, 2);            // badgesX[0]
    at += 2 * badgeN;
    at += 2 * badgeN;                                               // badgesHardX
    at += 2 * REGION_COUNT;                                         // eggByRegion
    memcpy(b.data() + at, "OLDHAND", 7);                            // trainerName
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i + 2 < b.size(); i++) {
      crc ^= (uint16_t)b[i] << 8;
      for (int k = 0; k < 8; k++)
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    memcpy(b.data() + b.size() - 2, &crc, 2);
    pr.putBytes("plyA", b.data(), b.size());
    pr.remove("mhi");            // and no legacy key for it either
    pr.end();

    Pet p; p.begin();
    ck(!strcmp(p.trainerName, "OLDHAND"),
       "a v1 player record still loads: the trainer name is intact");
    ck(p.badges == 0x00FF && p.badgesHard == 0x0003, "with both Kanto ladders");
    ck(p.badgeCountIn(1, false) == 4, "and the Johto badges after the scalars");
    ck(p.badgeCountIn(GYM_REGIONS - 1, false) == 0 &&
       p.badgeCountIn(GYM_REGIONS - 2, false) == 0,
       "while the ladders that record never knew start empty, not inheriting");
    ck(p.streak == 21 && p.bestStreak == 34, "the streak survives");
    ck(p.gameHi == 41 && p.strHi == 52 && p.spdHi == 13,
       "and all three of the older minigame records");
    ck(p.isRegistered(25), "the Pokedex still lines up, so the arrays were not shifted");
    ck(p.memoHi == 0,
       "while the field that record never had reads as no-record-yet, not as dex bits");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
