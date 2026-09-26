// THE DURABLE RESET INTENT, and the brightness setting's place in the backup.
//
// Both are about the same thing: what an existing save is worth after an upgrade.
// A reset that is not durable can leave a store that is neither the old save nor
// a new game; a setting that is not in SAVE_FIELDS is silently dropped from every
// player's backup, which is only discovered when somebody needs one.
#include "Arduino.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "save.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

uint32_t g_seed=3; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false; bool wasPressed=false;
uint32_t millis(){return 0;} void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
void sfxPlay(uint8_t){}
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  // ---- a save worth not losing by accident
  Pet pet;
  pet.begin();
  pet.dbgHatchAs(9, false);
  pet.rename("SHELL");
  pet.renameTrainer("NIALL");
  pet.badges = 0x003F;

  // ---- nothing is armed on a fresh device
  ck(!resetArmed(), "a device with no intent recorded is not armed");
  ck(!resetRecover(), "so recovery does nothing");
  { Pet q; q.begin(); ck(q.speciesId == 9, "and the save is untouched"); }

  // ---- arming must NOT itself wipe anything
  resetArm();
  ck(resetArmed(), "arming records the intent");
  { Pet q; q.begin(); ck(q.speciesId == 9,
      "and the save is STILL there -- the wipe happens on the next boot, not now"); }

  // ---- the intent lives outside the namespace the wipe clears
  //
  // THE WHOLE DESIGN RESTS ON THIS. If the flag lived in "tamapoke", clear()
  // would erase the very record saying a wipe was wanted, so a cut between the
  // clear and the acknowledgement would leave a half-wiped save and no intent to
  // finish it.
  ck(nvs().count("rst") == 0,
     "the intent is NOT a key in the game namespace");
  ck(nvsOther().count(RESET_NS) == 1 && nvsOther()[RESET_NS].count("rst") == 1,
     "it is in a namespace of its own");

  // ---- and recovery wipes, then acknowledges
  ck(resetRecover(), "recovery reports that it wiped");
  ck(!resetArmed(), "the intent is acknowledged, so it cannot fire twice");
  {
    Pet q; q.begin();
    ck(q.speciesId < 0, "the creature is gone: a fresh egg, not the old save");
    ck(q.trainerName[0] == 0, "and so is the trainer name");
  }
  ck(!resetRecover(), "a second boot does not wipe the new game");

  // ---- IDEMPOTENT: a crash between the clear and the acknowledgement
  {
    Pet p2; p2.begin(); p2.dbgHatchAs(3, false); p2.renameTrainer("AGAIN");
    resetArm();
    // the clear lands, the acknowledgement does not -- so the intent is still
    // there on the next boot
    { Preferences g; g.begin("tamapoke", false); g.clear(); g.end(); }
    ck(resetArmed(), "the intent survives the wipe it asked for");
    ck(resetRecover(), "so the next boot simply finishes the job");
    ck(!resetArmed(), "and only then acknowledges it");
    Pet q; q.begin();
    ck(q.trainerName[0] == 0, "leaving a genuinely new game");
  }

  // ---- a RESTORED BACKUP MUST NOT ARM A WIPE
  //
  // A .tpsave carrying the intent would erase the device it was restored onto.
  // saveExport/saveImport only ever open the game namespace, so this is true by
  // construction -- but "by construction" is worth pinning, because the obvious
  // place to put a new flag is SAVE_FIELDS.
  {
    Pet p3; p3.begin(); p3.dbgHatchAs(25, false); p3.renameTrainer("PIKA");
    static uint8_t buf[SAVE_TRANSFER_MAX];
    size_t n = saveExport(buf, sizeof(buf));
    ck(n > 0, "a save exports");
    resetArm();                                   // armed at the time of export
    ck(saveImport(buf, n), "and imports onto a device with an intent pending");
    // The import cleared the game namespace and wrote the backup back. The intent
    // is untouched either way; what matters is that the BACKUP never carried one.
    std::set<std::string> inTable;
    for (uint16_t i = 0; i < SAVE_FIELD_COUNT; i++) inTable.insert(SAVE_FIELDS[i].key);
    ck(inTable.count("rst") == 0, "and no backup can ever carry the reset intent");
    resetComplete();
  }

  // ---- BRIGHTNESS is a setting, so it belongs in the backup
  //
  // It is written by the sketch, which this suite does not link, so the key is
  // planted by hand -- the point being that SAVE_FIELDS lists it, and therefore
  // that a restore puts a player's screen back the way they had it.
  {
    std::set<std::string> inTable;
    for (uint16_t i = 0; i < SAVE_FIELD_COUNT; i++) inTable.insert(SAVE_FIELDS[i].key);
    ck(inTable.count("brt") == 1, "the brightness level is in the backup table");
    ck(inTable.count("vol") == 1 && inTable.count("lang") == 1,
       "beside the settings it sits with");

    Pet p4; p4.begin(); p4.dbgHatchAs(7, false); p4.renameTrainer("SQUIRT");
    { Preferences g; g.begin("tamapoke", false); g.putUChar("brt", 3); g.end(); }
    static uint8_t buf[SAVE_TRANSFER_MAX];
    size_t n = saveExport(buf, sizeof(buf));
    ck(n > 0, "a save with a brightness setting exports");
    { Preferences g; g.begin("tamapoke", false); g.putUChar("brt", 9); g.end(); }
    ck(saveImport(buf, n), "and imports");
    Preferences g; g.begin("tamapoke", true);
    ck(g.getUChar("brt", 0) == 3, "restoring puts the level back as it was");
    g.end();
  }

  // ---- AND AN OLD SAVE, WHICH HAS NO SETTING AT ALL
  //
  // Absence must mean "what this firmware has always done", never zero. A
  // brightness of zero is a black panel, so the wrong default here is the
  // difference between an upgrade and a brick.
  {
    Preferences g; g.begin("tamapoke", false);
    g.remove("brt");
    ck(!g.isKey("brt"), "a save from before the setting existed has no key");
    ck(g.getUChar("brt", 7) == 7, "and reads the default rather than zero");
    g.end();
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
