// Which sprite file actually gets opened.
//
// PmdMon::load() took a uint8_t while the dex reached 386, so every species
// from 256 up wrapped into Kanto: a MARSHTOMP (258) opened p002.bin and stood
// there as an IVYSAUR. Reported from a board, because nothing here looked.
//
// It is the third time this exact trap has been sprung -- DexEntry::evolvesTo
// and TrainerMon::dex were both uint8_t when the expansion landed -- so this
// checks EVERY species can be asked for and comes back as itself, rather than
// spot-checking the one that was reported.
#include "Arduino.h"
#include "Arduino_GFX_Library.h"
#include "Preferences.h"
#include "pet.h"
#include "party.h"
#include "sdmon.h"
#include "adpcm.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
uint32_t g_seed=7; FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX=0,g_touchY=0; volatile bool g_touchDown=false;
uint32_t millis(){return 0;}
void FakeESP::restart(){exit(0);}
int FakeSerial::available(){return 0;} String FakeSerial::readStringUntil(char){return String("");}
// sfxPlay comes from host_impl.cpp here -- this test links the host, not the sketch
void emuSetSpriteDir(const char *);
static int bad=0;
static void ck(bool ok,const char*w){printf("%s  %s\n",ok?"PASS":"FAIL",w); if(!ok)bad++;}

int main(){
  emuSetSpriteDir(SPRITE_DIR);

  // the exact creature that was reported, and the exact one it turned into
  {
    PmdMon a, b;
    bool okA = a.load(258, false);      // MARSHTOMP
    bool okB = b.load(2, false);        // IVYSAUR
    ck(okA, "a Hoenn sprite loads at all");
    ck(okA && a.dex == 258, "and it is MARSHTOMP, not whatever 258 truncates to");
    if (okA && okB) {
      bool same = a.palCount == b.palCount &&
                  !memcmp(a.pal, b.pal, a.palCount * sizeof(uint16_t));
      ck(!same, "MARSHTOMP's pixels are not IVYSAUR's");
    }
    a.unload(); b.unload();
  }

  // EVERY species: asked for by number, comes back as itself
  {
    int loaded = 0, wrong = 0, missing = 0;
    for (int16_t d = 1; d <= DEX_COUNT; d++) {
      PmdMon m;
      if (!m.load(d, false)) { missing++; continue; }
      loaded++;
      if (m.dex != d) { wrong++; if (wrong < 4) printf("      asked %d, got %d\n", d, m.dex); }
      m.unload();
    }
    printf("      %d sprites loaded, %d missing from the pack\n", loaded, missing);
    ck(loaded > 300, "most of the dex has a sprite to check");
    ck(wrong == 0, "every species that loads is the species that was asked for");
  }

  // THE assertion that actually bites. m.dex only records what was ASKED for,
  // so a truncation inside the filename alone would slip past it -- proven by
  // putting the bug back and watching that check still pass. This compares the
  // PIXELS of every species past 255 against the Kanto species it would wrap
  // onto, which is the thing that was really going wrong on the panel.
  {
    int checked = 0, collided = 0;
    for (int16_t d = 256; d <= DEX_COUNT; d++) {
      PmdMon hi, lo;
      if (!hi.load(d, false)) continue;
      if (!lo.load((int16_t)(d - 256), false)) { hi.unload(); continue; }
      checked++;
      if (hi.palCount == lo.palCount &&
          !memcmp(hi.pal, lo.pal, hi.palCount * sizeof(uint16_t))) {
        if (collided < 3) printf("      dex %d draws the same pixels as %d\n", d, d - 256);
        collided++;
      }
      hi.unload(); lo.unload();
    }
    printf("      compared %d species past 255 against their wrap-around twin\n", checked);
    ck(checked > 100, "the whole back half of the dex is reachable");
    ck(collided == 0, "and none of them draws the Kanto species it would truncate to");
  }

  // and nonsense is refused rather than opening some other creature's file
  {
    PmdMon m;
    ck(!m.load(0, false), "dex 0 loads nothing");
    ck(!m.load(-1, false), "nor a negative dex");
    ck(!m.load(9999, false), "nor one past the table");
  }

  // ---- Disk-backed Tier 1 ADPCM cries (sdLoadCry)
  {
    char tmpl[] = "/tmp/tamapoke-cry-XXXXXX";
    char *root = mkdtemp(tmpl);
    ck(root != nullptr, "temp dir created for cry SD test");
    if (root) {
      std::string monsDir = std::string(root) + "/mons";
      std::string criesDir = std::string(root) + "/cries";
      mkdir(monsDir.c_str(), 0700);
      mkdir(criesDir.c_str(), 0700);
      emuSetSpriteDir(monsDir.c_str());

      int16_t pcm[160];
      for (int i = 0; i < 160; i++) pcm[i] = (int16_t)((i % 20 - 10) * 1500);
      uint8_t adp[CRY_ADPCM_HDR + 80];
      size_t adpLen = adpcmEncode(pcm, 160, adp, sizeof(adp));
      ck(adpLen == sizeof(adp), "ADPCM test cry encodes cleanly");

      std::string okPath = criesDir + "/c025.adp";
      FILE *f = fopen(okPath.c_str(), "wb");
      if (f) { fwrite(adp, 1, adpLen, f); fclose(f); }

      std::string badPath = criesDir + "/c026.adp";
      f = fopen(badPath.c_str(), "wb");
      if (f) { const char badHdr[16] = "NOPE"; fwrite(badHdr, 1, sizeof(badHdr), f); fclose(f); }

      uint8_t *buf = nullptr;
      uint32_t len = 0;
      ck(sdLoadCry(25, &buf, &len) && buf && len == adpLen,
         "sdLoadCry loads valid Tier 1 TCRY cry from /cries/c025.adp");
      AdpcmDec dec;
      int16_t out[160];
      ck(buf && dec.init(buf, len) && dec.decode(out, 160, 10) == 160,
         "loaded cry decodes cleanly through AdpcmDec");
      free(buf); buf = nullptr;

      ck(!sdLoadCry(26, &buf, &len) && !buf, "sdLoadCry rejects corrupt cry header");
      ck(!sdLoadCry(27, &buf, &len) && !buf, "sdLoadCry returns false when cry file is absent");

      emuSetSpriteDir(SPRITE_DIR);
      unlink(okPath.c_str());
      unlink(badPath.c_str());
      rmdir(criesDir.c_str());
      rmdir(monsDir.c_str());
      rmdir(root);
    }
  }

  printf("%s\n", bad?"FAILURES":"all good");
  return bad?1:0;
}
