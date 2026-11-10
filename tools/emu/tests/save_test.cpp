// The save backup. A run is weeks of real time, so the thing that matters is
// not that export/import runs -- it is that NOTHING is quietly left out, and
// that a bad blob cannot destroy the save it was meant to protect.
//
// The completeness check at the bottom is the important one: it compares the
// backup's field table against the keys the firmware ACTUALLY wrote, so adding
// a field and forgetting the backup fails here instead of silently dropping it
// from every player's export.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "save.h"
#include "collection_store.h"
#include "ckpt.h"
#include "test_nvs.h"
#include <cstdio>
#include <set>
#include <string>
#include <vector>
#ifndef TAMA_NATIVE_TEST
uint32_t g_seed=7; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
#endif
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

static void appendField(std::vector<uint8_t> &blob, uint8_t space, const char *key,
                        uint8_t kind, const uint8_t *value, size_t n) {
  const uint8_t version = blob[4];
  if (version < 3 && n > UINT16_MAX) abort();
  const uint16_t count = ckptRd16(blob.data() + 5);
  if (version >= 2) blob.push_back(space);
  blob.push_back(strlen(key));
  blob.insert(blob.end(), key, key + strlen(key));
  blob.push_back(kind);
  for (size_t b = 0; b < (version >= 3 ? 4 : 2); b++) blob.push_back((n >> (8 * b)) & 255);
  blob.insert(blob.end(), value, value + n);
  ckptWr16(blob.data() + 5, count + 1);
}

static void sealTransfer(std::vector<uint8_t> &blob) {
  const uint16_t crc = ckptCrc(blob.data(), blob.size());
  blob.push_back(crc & 255); blob.push_back(crc >> 8);
}

static std::vector<uint8_t> collectionFixture(const Party &source, uint16_t stride,
                                             uint32_t generation, bool wide = true) {
  const size_t offset = wide ? COLLECTION_FIXED : 18;
  std::vector<uint8_t> blob(offset + size_t(stride) * (PARTY_SLOTS + BOX_SLOTS) + CKPT_CRC);
  for (size_t i = 0; i < PARTY_SLOTS + BOX_SLOTS; i++) {
    const PartyMon &mon = i < PARTY_SLOTS ? source.slots[i] : source.box[i - PARTY_SLOTS];
    uint8_t *record = blob.data() + offset + i * stride;
    memcpy(record, &mon, stride < sizeof(mon) ? stride : sizeof(mon));
    if (stride > sizeof(mon)) memset(record + sizeof(mon), 0xA5, stride - sizeof(mon));
  }
  if (wide) {
    collectionSeal(blob.data(), blob.size() - CKPT_CRC, stride, PARTY_SLOTS, BOX_SLOTS, generation);
  } else {
    ckptWr16(blob.data() + 12, stride);
    ckptWr16(blob.data() + 14, PARTY_SLOTS);
    ckptWr16(blob.data() + 16, BOX_SLOTS);
    ckptSeal(blob.data(), blob.size() - CKPT_CRC, COLLECTION_LEGACY_MAGIC, 1, generation);
  }
  return blob;
}

int main(){
  // --- a save worth losing: a raised creature, a full party, a filled box,
  // both ladders, a Pokedex with holes in it, a named trainer
  Pet pet;
  pet.begin();
  party.begin();
  pet.dbgHatchAs(6,true);
  pet.ageMinutes = 72UL*MINUTES_PER_LEVEL;
  pet.ivAtk=31; pet.ivDef=7; pet.ivSpe=22; pet.ivHp=19;
  pet.trAtk=64; pet.trDef=31; pet.trSpe=90;
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.rename("SCORCH");
  pet.avatar = 6;        // past the old four, so a stale & 3 mask would break it
  pet.badges = 0x00BF; pet.badgesHard = 0x000A;
  pet.streak = 12; pet.bestStreak = 19; pet.totalMedals = 41;
  pet.gameHi = 33; pet.strHi = 21; pet.spdHi = 9;
  pet.bond = 77; pet.careMistakes = 2; pet.weight = 55;
  pet.dexReg[0] = 0x5A; pet.dexReg[9] = 0xC3; pet.dexShinyReg[3] = 0x11;
  for (int i=0;i<PARTY_SLOTS;i++){ PartyMon m; m.dex=20+i*7; m.level=40+i;
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=20+i; m.shiny=(i==2);
    snprintf(m.nick,sizeof(m.nick),"P%d",i);
    m.moves[0]=1+i; m.moves[1]=9; party.replaceAt(i,m); }
  for (int i=0;i<BOX_SLOTS;i++){ PartyMon m; m.dex=1+(i*3)%DEX_COUNT; m.level=10+i%91;
    m.ivAtk=m.ivDef=m.ivSpe=m.ivHp=11; party.box[i]=m; }
  party.boxSave();
  // renameTrainer() persists, and save() writes every field -- so this is also
  // what commits everything set above. save() itself is private on purpose.
  pet.renameTrainer("DYLAN");

  // --- COMPLETENESS. Everything the firmware stored must be in the table.
  // This is what stops the backup rotting: add a key to pet.cpp and forget
  // save.cpp, and this fails instead of quietly dropping it from every export.
  //
  // It runs HERE, against the store the firmware just wrote -- not after a
  // restore. After an import the store only holds what the backup put back, so
  // a forgotten key would be missing from both sides and the check would pass
  // itself. It did exactly that until this was moved.
  {
    std::set<std::string> inTable;
    for (uint16_t i=0;i<SAVE_FIELD_COUNT;i++)
      if (!SAVE_FIELDS[i].space) inTable.insert(SAVE_FIELDS[i].key);
    int missing = 0;
    for (auto &kv : readTestNvs()) {
      if (inTable.count(kv.first)) continue;
      printf("      key '%s' is stored but NOT backed up\n", kv.first.c_str());
      missing++;
    }
    ck(missing==0, "every key the firmware writes is in the backup table");
    size_t originalFields = 0;
    std::set<std::string> allFields;
    for (uint16_t i=0;i<SAVE_FIELD_COUNT;i++) {
      const SaveField &f = SAVE_FIELDS[i];
      if (!f.space) originalFields++;
      allFields.insert(std::to_string(f.space) + "/" + f.key);
    }
    ck(inTable.size()==originalFields && allFields.size()==SAVE_FIELD_COUNT,
       "and the namespaced table has no duplicates");
  }

  std::vector<uint8_t> storage(SAVE_TRANSFER_MAX);
  uint8_t *buf = storage.data();
  size_t n = saveExport(buf, storage.size());
  ck(n > 0, "a save exports");
  ck(n < storage.size(), "and fits a heap buffer");
  // Both full collection copies must fit, with space for growing player data.
  ck(n && n < storage.size() * 3 / 4,
     "with room left for the dex and the record layouts to keep growing");
  printf("      (%u bytes of %u)\n", (unsigned)n, (unsigned)storage.size());
  if (!n) return 1;
  ck(buf[4] == 3, "exports are version 3, which old firmware refuses");
  {
    Preferences collection;
    ck(collectionOpen(collection), "the dedicated collection namespace opens");
    std::set<std::string> captured;
    size_t at = SAVE_HDR;
    while (at < n - 2) {
      uint8_t space = buf[at++], k = buf[at++];
      std::string key((char *)buf + at, k); at += k;
      at++; size_t len = ckptRd32(buf + at); at += 4;
      if (space == 1) {
        std::vector<uint8_t> original(len);
        bool matches = key == "active" ? len == 1 && buf[at] == 1
          : collection.getBytes(key.c_str(), original.data(), len) == len &&
            !memcmp(original.data(), buf + at, len);
        ck(matches, "a collection field is captured byte-for-byte, not omitted");
        captured.insert(key);
      }
      at += len;
    }
    ck(captured.count("cbA") && captured.count("cbB") && captured.count("active"),
       "both collection checkpoint slots AND activation are backed up");
    ck(saveExport(buf, n - 1) == 0, "an undersized output buffer fails, never truncates");
    // Re-export because the unsuccessful call may have filled the prefix.
    n = saveExport(buf, storage.size());
  }

  // --- a mangled blob must never be applied
  {
    // Snapshotted whole rather than checking one legacy key's presence: since
    // the checkpoint became the only thing save()/persist() write, there is no
    // single scalar key guaranteed to exist that would prove this on its own.
    NvsStore before = readTestNvs();
    std::vector<uint8_t> t(buf, buf+n);
    t[0] = 'X';
    ck(!saveImport(t.data(), t.size()), "a blob with the wrong magic is refused");
    t = std::vector<uint8_t>(buf, buf+n);
    t[4] = SAVE_VERSION + 9;
    ck(!saveImport(t.data(), t.size()), "and one from a future version");
    t = std::vector<uint8_t>(buf, buf+n);
    t[SAVE_HDR + 3] ^= 0xFF;                   // flip a byte in the middle
    ck(!saveImport(t.data(), t.size()), "a single flipped byte fails the checksum");
    t = std::vector<uint8_t>(buf, buf+n-1);    // chopped short
    ck(!saveImport(t.data(), t.size()), "and a truncated blob is refused");
    ck(!saveImport(buf, 3), "as is one too short to hold a header");
    t = std::vector<uint8_t>(buf, buf+n);
    t[SAVE_HDR] = 2; // unsupported namespace, with an otherwise valid checksum
    ckptWr16(t.data()+t.size()-2, ckptCrc(t.data(), t.size()-2));
    ck(!saveImport(t.data(), t.size()), "an unknown namespace is rejected rather than skipped");
    t.assign(buf, buf + n);
    size_t lengthAt = SAVE_HDR + 2 + t[SAVE_HDR + 1] + 1;
    ckptWr32(t.data() + lengthAt, UINT32_MAX);
    ckptWr16(t.data()+t.size()-2, ckptCrc(t.data(), t.size()-2));
    ck(!saveImport(t.data(), t.size()),
       "a checksummed u32 field length beyond the body is rejected before replacement");
    t.assign(buf, buf + lengthAt + 3);
    sealTransfer(t);
    ck(!saveImport(t.data(), t.size()),
       "a truncated u32 field header cannot be mistaken for a short field");
    // and none of that touched the live save
    ck(readTestNvs() == before, "a refused import leaves the save alone");
  }

  // --- the real thing: wipe everything, restore, and compare
  pet.factoryReset();
  ck(readTestNvs().empty() || readTestNvs().count("party")==0, "the wipe really emptied NVS");
  ck(saveImport(buf, n), "the backup imports");

  Pet p2; Party q2;
  p2.begin(); q2.begin();
  ck(p2.speciesId==6 && p2.shiny, "the creature is back, shiny and all");
  ck(p2.ageMinutes==72UL*MINUTES_PER_LEVEL, "at the age it was");
  ck(p2.ivAtk==31 && p2.ivDef==7 && p2.ivSpe==22 && p2.ivHp==19, "with its IVs");
  ck(p2.trAtk==64 && p2.trDef==31 && p2.trSpe==90, "and its training");
  ck(!strcmp(p2.trainerName,"DYLAN"), "the trainer name survives");
  ck(!strcmp(p2.nick,"SCORCH"), "so does the nickname");
  ck(p2.avatar==6, "and the avatar, including one past the original four");
  ck(p2.badges==0x00BF && p2.badgesHard==0x000A, "both badge ladders");
  ck(p2.streak==12 && p2.bestStreak==19 && p2.totalMedals==41, "streak and medals");
  ck(p2.gameHi==33 && p2.strHi==21 && p2.spdHi==9, "every minigame record");
  ck(p2.bond==77 && p2.careMistakes==2 && p2.weight==55, "bond, mistakes, weight");
  // A superset, not an equality: begin() re-registers the LIVE species on every
  // load (pet.cpp:1011, seeding the dex for saves older than it existed), so
  // Charizard's bit is set on top of whatever was restored. What the backup
  // owes us is that no bit is LOST.
  ck((p2.dexReg[0] & 0x5A)==0x5A && p2.dexReg[9]==0xC3 &&
     (p2.dexShinyReg[3] & 0x11)==0x11, "the Pokedex, holes and all");
  ck((p2.dexReg[0] & ~0x5A) == 0x20,
     "and the only extra bit is the live creature re-registering itself");
  bool moves = true;
  for (int i=0;i<MOVE_SLOTS;i++) if (p2.moves[i]!=pet.moves[i]) moves=false;
  ck(moves, "and the moveset");

  ck(q2.count()==PARTY_SLOTS, "the whole party is back");
  bool party_ok = true;
  for (int i=0;i<PARTY_SLOTS;i++){
    const PartyMon &m = q2.slots[i];
    if (m.dex != 20+i*7 || m.level != 40+i) party_ok = false;
    if (m.moves[0] != 1+i || m.moves[1] != 9) party_ok = false;
    char want[8]; snprintf(want,sizeof(want),"P%d",i);
    if (strcmp(m.nick, want)) party_ok = false;
  }
  ck(party_ok, "with every level, moveset and nickname");
  ck(q2.slots[2].shiny, "and a banked shiny is still shiny");
  ck(q2.boxCount()==BOX_SLOTS, "the box comes back full");
  ck(q2.box[7].dex==1+7*3 && q2.box[7].level==17, "with the right creatures in it");
  ck(q2.box[BOX_SLOTS-1].dex==1+((BOX_SLOTS-1)*3)%DEX_COUNT &&
     q2.box[BOX_SLOTS-1].level==10+(BOX_SLOTS-1)%91,
     "including the highest collection slot");

  // --- a restore must not leave anything of whatever was there before
  {
    Pet p3; p3.begin();
    p3.badges = 0xFFFF;
    p3.renameTrainer("GHOST");
    ck(saveImport(buf, n), "a second restore over a different save");
    Pet p4; p4.begin();
    ck(!strcmp(p4.trainerName,"DYLAN") && p4.badges==0x00BF,
       "overwrites it rather than merging with it");
  }

#ifndef TAMA_NATIVE_TEST
  // Every commit boundary is interrupted, including the staging writes, each
  // namespace clear, each replayed field, and removal of the replay marker.
  {
    const NvsStore original = nvs();
    const auto otherOriginal = nvsOther();
    bool safe = true, interruptedReplay = false, stageUntouched = false;
    int boundaries = 0;
    for (int budget = 0; budget < 64; budget++) {
      nvs() = original; nvsOther() = otherOriginal;
      nvs()["foreign"] = {42};
      const auto before = nvs();
      nvsFailWritesAfter(budget);
      const bool imported = saveImport(buf, n);
      const bool pending = saveRestorePending();
      if (!imported && !pending) {
        stageUntouched = true;
        if (nvs() != before) safe = false;
      }
      if (!imported && pending) interruptedReplay = true;
      nvsResumeWrites();
      if (!saveRecover() || saveRestorePending()) safe = false;
      if (pending || imported) {
        Pet recovered; Party recoveredParty;
        recovered.begin(); recoveredParty.begin();
        if (strcmp(recovered.trainerName, "DYLAN") || nvs().count("foreign") ||
            recoveredParty.box[BOX_SLOTS-1].dex != q2.box[BOX_SLOTS-1].dex) safe = false;
      }
      boundaries++;
      if (imported) break;
    }
    ck(safe && stageUntouched && interruptedReplay && boundaries > 5,
       "every interrupted restore either leaves the old save or replays completely at boot");
    printf("      (%d write-budget boundaries)\n", boundaries);
    nvsResumeWrites();
    nvs() = original; nvsOther() = otherOriginal;

    nvsFailWritesAfter(2); // blob and marker commit; replacement cannot begin
    ck(!saveImport(buf, n) && saveRestorePending(), "an interrupted replay keeps its durable intent");
    nvsResumeWrites();
    const auto beforeCorrupt = nvs();
    Preferences journal;
    journal.begin("restore", false, COLLECTION_PARTITION);
    std::vector<uint8_t> corrupt(n);
    journal.getBytes("blob", corrupt.data(), corrupt.size());
    corrupt[n/2] ^= 0x80;
    journal.putBytes("blob", corrupt.data(), corrupt.size());
    ck(!saveRecover() && saveRestorePending() && nvs() == beforeCorrupt,
       "a corrupt committed stage blocks boot without touching the old save");
    nvs() = original; nvsOther() = otherOriginal;

    nvsFailWritesAfter(2);
    saveImport(buf, n);
    nvsResumeWrites();
    resetArm();
    ck(resetArmed() && resetRecover() && !saveRestorePending(),
       "a factory reset clears a pending restore instead of resurrecting it");
    ck(saveRecover() && nvs().empty(), "boot recovery cannot replay a factory-reset collection");
    nvs() = original; nvsOther() = otherOriginal;

    nvsFailWritesAfter(1); // staging lands, but its commit marker does not
    ck(!saveImport(buf, n) && !saveRestorePending(),
       "an interrupted staging write is not a committed restore");
    nvsResumeWrites();
    Preferences orphan;
    orphan.begin("restore", false, COLLECTION_PARTITION);
    ck(orphan.isKey("blob") && nvs() == original,
       "uncommitted staging leaves the original game intact");
    const auto staged = nvsOther();
    nvsFailWritesAfter(0);
    ck(!saveImport(buf, n) && nvsOther() == staged && nvs() == original,
       "a retry stops if removing the orphan stage fails");
    nvsResumeWrites();
    nvsFailWritesAfter(1); // only the orphan removal can land
    ck(!saveImport(buf, n) && !orphan.isKey("blob") && !saveRestorePending() &&
       nvs() == original,
       "the orphan is removed before allocating another full staging blob in NVS");
    nvsResumeWrites();
    ck(saveImport(buf, n) && !saveRestorePending(),
       "a retry after orphan cleanup completes the whole restore");
    nvs() = original; nvsOther() = otherOriginal;
  }
#endif

  // A v1 save has no namespace byte. Its old party/box blobs must migrate only
  // after the full backup has been safely replayed into the original namespace.
  {
    std::vector<uint8_t> legacy{'T', 'K', 'P', 'S', 1, 0, 0, 0};
    size_t at = SAVE_HDR;
    while (at < n - 2) {
      uint8_t space = buf[at++];
      uint8_t k = buf[at++];
      std::string key((char *)buf + at, k);
      at += k;
      uint8_t kind = buf[at++];
      size_t len = ckptRd32(buf + at); at += 4;
      if (!space && key != "pbA" && key != "pbB" && key != "fxA" && key != "fxB" &&
          key != "colv" && key != "nick") {
        appendField(legacy, 0, key.c_str(), kind, buf + at, len);
      }
      at += len;
    }
    auto add = [&](const char *key, const void *value, size_t len, uint8_t kind = SK_BYTES) {
      appendField(legacy, 0, key, kind, (const uint8_t *)value, len);
    };
    add("party", q2.slots, sizeof(q2.slots));
    add("box", q2.box.data(), sizeof(PartyMon) * 36);
    add("nick", "", 0, SK_STR);
    sealTransfer(legacy);
    ck(saveImport(legacy.data(), legacy.size()), "a v1 backup is accepted without namespace ambiguity");
    Party migrated; migrated.begin();
    printf("      (v1 party=%d/%d box35=%d/%d box36=%d)\n",
           migrated.slots[2].dex, q2.slots[2].dex,
           migrated.box[35].dex, q2.box[35].dex, migrated.box[36].dex);
    ck(migrated.slots[2].dex == q2.slots[2].dex &&
       migrated.box[35].dex == q2.box[35].dex && !migrated.box[36].dex,
       "v1 party and 36-slot box migrate without inventing upper collection records");
    ck(saveImport(buf, n), "the full v3 collection can replace a migrated v1 save");
  }

  {
    std::vector<uint8_t> legacy{'T', 'K', 'P', 'S', 2, 0, 0, 0};
    size_t at = SAVE_HDR;
    while (at < n - 2) {
      uint8_t space = buf[at++], k = buf[at++];
      std::string key((char *)buf + at, k); at += k;
      uint8_t kind = buf[at++];
      size_t length = ckptRd32(buf + at); at += 4;
      if (space && (key == "cbA" || key == "cbB")) {
        const auto old = collectionFixture(q2, sizeof(PartyMon), key == "cbA" ? 7 : 8, false);
        appendField(legacy, space, key.c_str(), kind, old.data(), old.size());
      } else appendField(legacy, space, key.c_str(), kind, buf + at, length);
      at += length;
    }
    sealTransfer(legacy);
    ck(saveImport(legacy.data(), legacy.size()), "a v2 backup with u16 lengths and KBC2 checkpoints imports");
    Party migrated; migrated.begin();
    ck(migrated.slots[2].dex == q2.slots[2].dex &&
       migrated.box[BOX_SLOTS-1].dex == q2.box[BOX_SLOTS-1].dex,
       "the v2 collection retains its party and highest box slot");
    ck(saveImport(buf, n), "a v3 restore replaces the imported v2 collection");
  }

  {
    const auto futureA = collectionFixture(q2, 56, 9001);
    const auto futureB = collectionFixture(q2, 56, 9002);
    ck(futureA.size() > UINT16_MAX && futureB.size() > UINT16_MAX,
       "each future-stride collection checkpoint individually exceeds 65535 bytes");
    Preferences collection;
    collectionOpen(collection);
    collection.putBytes("cbA", futureA.data(), futureA.size());
    collection.putBytes("cbB", futureB.data(), futureB.size());
    std::vector<uint8_t> future(SAVE_TRANSFER_MAX);
    const size_t futureN = saveExport(future.data(), future.size());
    ck(futureN > futureA.size() + futureB.size(),
       "v3 exports both individual wide checkpoints without narrowing their lengths");
    if (futureN) {
      future.resize(futureN);
      ck(saveImport(future.data(), future.size()),
         "a v3 restore round-trips individual checkpoints above the u16 field limit");
      std::vector<uint8_t> actual(futureA.size());
      bool exact = collection.getBytes("cbA", actual.data(), actual.size()) == actual.size() &&
                   actual == futureA;
      exact = exact && collection.getBytes("cbB", actual.data(), actual.size()) == actual.size() &&
              actual == futureB;
      ck(exact, "all future record suffixes survive export, staging and replay byte-for-byte");
      Party restored; restored.begin();
      ck(restored.slots[2].dex == q2.slots[2].dex &&
         restored.box[BOX_SLOTS-1].dex == q2.box[BOX_SLOTS-1].dex &&
         restored.box[BOX_SLOTS-1].level == q2.box[BOX_SLOTS-1].level,
         "the real collection reader loads known prefixes of larger future records");
      ck(!restored.writable() && !restored.savePair(),
         "unknown future fields cannot be silently removed by an older reader");

      auto invalid = futureA;
      ckptWr16(invalid.data() + COLLECTION_FIXED + 56 * (PARTY_SLOTS + BOX_SLOTS - 1),
               DEX_COUNT + 1);
      ckptWr16(invalid.data() + invalid.size() - 2, ckptCrc(invalid.data(), invalid.size() - 2));
      std::vector<uint8_t> rejected{'T', 'K', 'P', 'S', 3, 0, 0, 0};
      const uint8_t active = 1;
      appendField(rejected, 1, "cbA", SK_BYTES, invalid.data(), invalid.size());
      appendField(rejected, 1, "cbB", SK_BYTES, invalid.data(), invalid.size());
      appendField(rejected, 1, "active", SK_BOOL, &active, 1);
      sealTransfer(rejected);
      ck(!saveImport(rejected.data(), rejected.size()),
         "valid transfer and snapshot CRCs cannot hide an invalid collection dex prefix");
      ck(collection.getBytes("cbB", actual.data(), actual.size()) == actual.size() && actual == futureB,
         "the rejected collection leaves the wide saved records untouched");
    }
    ck(saveImport(buf, n), "the original v3 backup still restores after a future-stride collection");
  }

  {
    const std::vector<uint8_t> good(buf, buf + n);
    Preferences collection;
    collectionOpen(collection);
    std::vector<uint8_t> oversized(COLLECTION_MAX_BYTES + 1);
    collection.putBytes("cbA", oversized.data(), oversized.size());
    ck(saveExport(buf, storage.size()) == 0, "an oversized stored checkpoint fails export explicitly");
    ck(saveImport(good.data(), good.size()), "the verified backup can repair the oversized checkpoint");
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
