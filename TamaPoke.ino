// TamaPoke - tamagotchi pixel art inspirado en la gen 1
// para Waveshare ESP32-S3-Touch-AMOLED-1.75
//
// Librerias (Library Manager o repo de Waveshare):
//   - "GFX Library for Arduino" (moononournation), con soporte CO5300 QSPI
//   - "SensorLib" (Lewis He), driver tactil CST9217
//
// Placa: ESP32S3 Dev Module | Flash 16MB | PSRAM: OPI PSRAM | USB CDC On Boot: Enabled
//
// Los sprites y la tabla de especies se generan con tools/sprites.py (emit).

#include <Arduino.h>
#include <Wire.h>
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include "pin_config.h"
#include "species.h"
#include "dex.h"
#include "types.h"
#include "moves.h"
#include "battle.h"
#include "trainers.h"
#include "link.h"
#include "linknow.h"
#include "backs.h"
#include "badges.h"
#include "avatars.h"
#include <stdarg.h>
#include <algorithm>
#include "party.h"
#include "collection_store.h"
#include "save.h"
#include "pet.h"
#include "items.h"
#include "inventory.h"
#include "wild.h"
#include "sdmon.h"
#include "rtcbat.h"
#include "imu.h"
#include "steps.h"
#include "nvsinfo.h"
#include "i18n.h"
#include "uifont.h"
#include "audio.h"
#include "power.h"
#include "blit.h"

// Version del firmware. Subir este numero en cada release (y manifest.json para
// el instalador web). Se muestra en la pantalla de ajustes y por serie al arrancar.
#define FW_VERSION "5.0.0"

// Antialiased bold Fredoka UI font renderer (uifont.h, SIL OFL 1.1; adapted from
// crenberries428/TamaPoke, MIT). Overrides write(buf, len) on Arduino_Canvas so
// every gfx->print() / printf() at textSize >= 2 draws smooth 4-bit alpha-blended
// glyphs inside the caller's [cursor_x, cursor_x + len * 6 * textsize_x] box.
class UiCanvas : public Arduino_Canvas {
public:
  using Arduino_Canvas::Arduino_Canvas;

  static int measureUi(const UiFont &f, const uint8_t *s, size_t len) {
    int w = 0;
    for (size_t i = 0; i < len; i++)
      if (s[i] >= 32 && s[i] < 127) w += f.g[s[i] - 32].adv;
    return w;
  }

  size_t write(const uint8_t *buf, size_t len) override {
    if (!buf || !len) return 0;
    // Stay on the 5x7 bitmap path for size-1 micro-labels or non-ASCII bytes.
    if (textsize_x <= 1) {
      for (size_t i = 0; i < len; i++) Arduino_Canvas::write(buf[i]);
      return len;
    }
    for (size_t i = 0; i < len; i++) {
      if (buf[i] < 32 || buf[i] >= 127) {
        for (size_t j = 0; j < len; j++) Arduino_Canvas::write(buf[j]);
        return len;
      }
    }
    const int monoW = (int)len * 6 * textsize_x;
    const int monoH = 7 * textsize_y;
    // Choose the bold Fredoka size that matches textsize_x, stepping down only if
    // a long string would overflow its allocated box or the round panel chord.
    static const UiFont *const LADDER[] = { &UIF_TINY, &UIF_SMALL, &UIF_MID, &UIF_BIG, &UIF_HUGE };
    int idx = (textsize_x >= 5) ? 4 : (textsize_x == 4) ? 3 : (textsize_x == 3) ? 2 : 1;
    int cyMid = cursor_y + monoH / 2;
    int dy = cyMid - 233;
    int maxChord = (abs(dy) < 228) ? (int)(2.0f * sqrtf((float)(228 * 228 - dy * dy))) - 16 : 440;
    int maxW = (len >= 11) ? (monoW + 18) : (monoW + 32);
    if (maxW > maxChord) maxW = maxChord;
    while (idx > 0 && measureUi(*LADDER[idx], buf, len) > maxW) idx--;
    const UiFont &f = *LADDER[idx];
    const int propW = measureUi(f, buf, len);
    int x = cursor_x + (monoW - propW) / 2;
    int baseY = cursor_y + (monoH + f.ascent) / 2;
    uint16_t *fb = getFramebuffer();
    const int fr = textcolor >> 11, fg = (textcolor >> 5) & 63, fb_ = textcolor & 31;
    for (size_t k = 0; k < len; k++) {
      const UiGlyph &g = f.g[buf[k] - 32];
      for (int j = 0; j < g.h; j++) {
        int py = baseY + g.yo + j;
        if (py < 0 || py >= LCD_HEIGHT) continue;
        for (int i = 0; i < g.w; i++) {
          int n = j * g.w + i;
          uint8_t a = (f.bits[g.off + (n >> 1)] >> ((n & 1) ? 0 : 4)) & 15;
          if (!a) continue;
          int px = x + g.xo + i;
          if (px < 0 || px >= LCD_WIDTH) continue;
          uint16_t &dst = fb[(size_t)py * LCD_WIDTH + px];
          if (a == 15) dst = textcolor;
          else {
            uint16_t bg = dst;
            int r = (bg >> 11) + (fr - (bg >> 11)) * a / 15;
            int gg = ((bg >> 5) & 63) + (fg - ((bg >> 5) & 63)) * a / 15;
            int b = (bg & 31) + (fb_ - (bg & 31)) * a / 15;
            dst = (uint16_t)((r << 11) | (gg << 5) | b);
          }
        }
      }
      x += g.adv;
    }
    cursor_x += monoW;
    return len;
  }
};

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(
  bus, LCD_RESET, 0 /*rotation*/, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
// Framebuffer completo en PSRAM: dibujamos todo y hacemos flush() (sin parpadeo)
Arduino_Canvas *gfx = new UiCanvas(LCD_WIDTH, LCD_HEIGHT, panel);

TouchDrvCST92xx touch;
Pet pet;

// sprite animado de la SD para la especie actual (si existe el archivo)
SdMon mon;          // sprite B/N (respaldo y minijuego si no hay PMD)
PmdMon pmd;         // sprite PMD multi-accion (pantalla principal)
PmdMon evoPmd;      // forma anterior, solo durante el parpadeo de evolucion
int16_t monFor = -2;
bool monShinyFor = false;

// comportamiento del bicho en pantalla
struct {
  uint8_t mode = 0;     // 0 idle, 1 paseo, 2 gesto one-shot
  uint8_t act = PMD_IDLE;
  uint32_t t0 = 0;      // inicio de la animacion en curso
  uint32_t until = 0;   // fin del estado actual
  float x = 233, targetX = 233;
} beh;
#define PET_GROUND 304  // linea de suelo de la mascota
PmdMon galleryPmd;  // sprite grande de la vista detalle de la galeria (PMD/TPK2, legal)

// galeria pokedex
bool galleryOpen = false;
bool galleryDirty = false;
// 16 to a page, and the Pokedex is browsed ONE REGION AT A TIME. Three
// generations flat is 25 pages of swiping to reach Hoenn, which is not a
// Pokedex, it is a scroll. A vertical swipe changes region and a horizontal one
// pages within it, so nothing is ever more than ten pages from the front.
// ALL is deliberately not offered here -- it is the thing being replaced.
#define GAL_PER_PAGE 16
#define GAL_REGIONS (REGION_COUNT - 1)          // the real regions, not ALL
#define GAL_LO (REGIONS[galleryRegion % GAL_REGIONS].lo)
#define GAL_HI (REGIONS[galleryRegion % GAL_REGIONS].hi)
#define GAL_SPAN (GAL_HI - GAL_LO + 1)
#define GAL_PAGES ((GAL_SPAN + GAL_PER_PAGE - 1) / GAL_PER_PAGE)
uint8_t galleryRegion = 0;
// Both the Pokedex and the gym ladder now open on a REGION CHOOSER rather than
// dropping you into whichever region was last viewed. The vertical swipe that
// changes region still works, but it is invisible, so on its own it meant the
// Johto and Hoenn content looked absent.
bool galleryPick = false;
bool gymPick = false;
uint8_t rpickPage = 0;      // the region chooser is paged; shared by all 3 modes
uint8_t galleryPage = 0;    // GAL_PAGES paginas de GAL_PER_PAGE
// ---------------------------------------------------------------------------
// POKEDEX FILTERS. A view setting, so RAM only -- deliberately NO save field:
// every one of these is derived from data the save already holds, and a filter
// somebody left on is not worth a migration or a line in the backup.
//
// RAISED and CAUGHT ARE GENUINELY DIFFERENT SETS HERE, which is what makes both
// worth offering rather than being two names for one bit. registerSpecies() only
// ever fires for the LIVE creature -- on hatch, on a focus swap, on revive -- so
// a wild capture that went straight into the box has never touched dexReg. The
// dex knows what you have RAISED; the party and box know what you HAVE.
// Separated Pokedex views: ALL (everything raised, hatched or caught), HATCHED
// (your eggs and what they became), CAUGHT (wild captures and what they became),
// and SHINY (shinies from any source). ALL / HATCHED / CAUGHT show the full
// region with silhouettes for missing entries; SHINY filters to shinies only.
enum : uint8_t { GFILT_ALL = 0, GFILT_HATCHED, GFILT_CAUGHT, GFILT_SHINY, GFILT_COUNT };
uint8_t galleryFilter = GFILT_ALL;
static inline uint8_t gvWhich() {
  return galleryFilter == GFILT_HATCHED ? DEXV_HATCHED
       : galleryFilter == GFILT_CAUGHT  ? DEXV_CAUGHT
                                        : DEXV_COMPLETE;
}
static inline bool gvShiny() { return galleryFilter == GFILT_SHINY; }
bool galleryMember(int16_t dex) { return pet.inDex(gvWhich(), gvShiny(), dex); }
// Published so view_test can walk every filter without keeping its own copy of
// how many there are -- a test that restates a count silently stops covering the
// mode somebody adds next.
uint8_t galleryFilterCount() { return GFILT_COUNT; }
#define GAL_VIEWS GFILT_COUNT
#define GAL_VBTN_Y 61
#define GAL_VBTN_H 21
#define GAL_VBTN_GAP 4
void galleryViewRect(uint8_t i, int *x, int *w);
int8_t galleryViewAt(int16_t x, int16_t y);
int16_t galleryDetail = 0;  // dex en vista detalle, 0 = rejilla

bool screenOff = false;       // pulsacion corta del boton PWR
// Cached from nvsLowOnSpace(), refreshed at boot and on the HEALTH heartbeat
// rather than every frame -- nvs_get_stats() is not free, and drawSaveWarning()
// runs on every render. See nvsinfo.cpp: a full partition is erased WHOLE on
// the next boot, so this is the only advance notice a player ever gets.
bool gNvsLow = false;
bool cardOpen = false;        // ficha del bicho (deslizar vertical)
bool kbOpen = false;
enum : uint8_t { KB_PET = 0, KB_TRAINER };
uint8_t kbTarget = KB_PET;          // teclado para renombrar al bicho
char nameBuf[12] = "";
uint8_t nameLen = 0;
#define CARD_PAGES 4   // profile, stats, moves, progress -- medals moved to
                       // the player card, where the totals already live
uint8_t cardPage = 0;         // 0 perfil, 1 stats+medallas
// Menu overlay: opened by tapping the pet's name on the main screen. The
// horizontal swipe is already taken by the Pokedex (it pages through 10 pages
// internally), so a hub on that axis would be ambiguous; the header was inert
// and is the only free surface left.
bool menuOpen = false;
#define MENU_W 280
#define MENU_X (CX - MENU_W / 2)
#define MENU_Y 56
#define MENU_H 354
#define MENU_ROW_H 44
#define MENU_ROW_GAP 4
// 7 rows by name: STATS / BAG / POKEDEX / JUKEBOX / SETTINGS / RETIRE / CLOSE.
// Panel spans (93..373) x (56..410), max corner radius ~226 <= 233 bezel.
enum : uint8_t { MENU_STATS = 0, MENU_BAG, MENU_DEX, MENU_JUKE, MENU_SETTINGS, MENU_RETIRE, MENU_CLOSE, MENU_ROWS };
#define MENU_ROW_Y(i) (MENU_Y + 11 + (i) * (MENU_ROW_H + MENU_ROW_GAP))

// Jukebox modal, reached from MENU_JUKE. Active track comes from
// audioCurrentTrack() so a battle override never leaves a stale row lit.
bool jukeOpen = false;
uint8_t jukePage = 0;
#define JUKE_PER_PAGE 5
#define JUKE_PAGES ((TUNE_COUNT + JUKE_PER_PAGE - 1) / JUKE_PER_PAGE)
#define JUKE_ROW_H 48
#define JUKE_ROW_GAP 6
#define JUKE_ROW_Y(i) (90 + (i) * (JUKE_ROW_H + JUKE_ROW_GAP))
#define JUKE_NAV_Y 372
#define JUKE_NAV_H UI_TAP_MIN
#define JUKE_BEAT_MS 400.0f

// Party screen. partyPick != 0 means the newcomer needs a slot: the player
// either taps someone to replace or lets it go.
bool partyOpen = false;
bool partyPick = false;
bool handoverStored = false;  // collection committed; retry only the pet acknowledgement
bool focusBlocked = false;
uint8_t partyDetail = 0;   // 0 = the grid, else slot + 1
// The box, reached from the party screen. `boxSwapFrom` is the party slot
// waiting for something to trade with, 0 when nothing is pending.
bool boxOpen = false;
uint8_t boxPage = 0;
uint8_t boxSwapFrom = 0;   // party slot + 1, armed from the party side
uint16_t boxSel = 0;       // box slot + 1, armed from the box side
// How the box is ORDERED ON SCREEN. RAM only, like the Pokedex filter, and for a
// stronger reason: see boxSlotFor(). Sorting the array itself would rewrite half
// of a CRC'd checkpoint every time somebody glanced at the box.
enum : uint8_t { BSORT_SLOT = 0, BSORT_DEX, BSORT_LEVEL, BSORT_COUNT };
uint8_t boxSort = BSORT_SLOT;
uint8_t boxSortCount() { return BSORT_COUNT; }
// The box gets the same detail sheet the party has. Tapping a box slot used to
// yank the creature into the party immediately, which is a surprising amount to
// happen from one tap -- and left nowhere to put a RELEASE button.
uint16_t boxDetail = 0;       // box slot + 1 whose sheet is open
bool boxJumpOpen = false;
// The RELEASE confirm, on whichever sheet is up. One flag rather than two,
// because only one sheet can be open at a time and partyDetail/boxDetail
// already say which.
bool releaseConfirm = false;
#define BOX_PER_PAGE 6
uint32_t partyBannerUntil = 0;   // "<name> joined the party!"
char partyBannerName[14] = "";
// The party and box moved from a 2x3 grid to a RING; see partySlotPos(). A grid
// on a circle spends its corners on glass that is not there -- the old 150x70
// cells put the outer corners of the top and bottom rows within a few pixels of
// the bezel, and 70 px is 6.7 mm, under the finger floor, on a screen whose
// controls have been reported as hard to hit twice already. Six 104 px discs fit
// inside radius 181 with room to spare, and hand the middle of the panel to the
// hub instead of to two more cell corners.
#define PSLOT_CY 224       // the ring leaves a prompt band under the title and
#define PSLOT_RING 112     // clears the CLOSE bar below
#define PSLOT_R 50         // 100 px across -- 9.5 mm
// 94 px: the hub is the one control here that had room to reach the finger
// floor outright. The slots' inner edge is 62 px from the ring centre, so it
// can grow to 47 without touching them.
#define PSLOT_HUB_R 47
#define PSLOT_PROMPT_Y 68

bool clockOpen = false;       // pantalla de ajuste de hora (deslizar abajo)
int clockH = 12, clockM = 0;  // hora en edicion
// Settings is paged: 0 is the clock, 1 is sound/volume/brightness/language and
// the reset. Declared up here with the other screen state because openClock()
// resets both, and uiRimTarget() pages it -- both of which sit above the
// settings screen's own section.
#define CLOCK_PAGES 3   // the clock, the options, and GAME (speed, farewell, eggs)
uint8_t clockPage = 0;
// The reset confirm, on the settings screen. Its own flag rather than another
// choiceKind: that dialog is drawn over the MAIN screen, and this one is modal
// over settings.
bool resetConfirm = false;

// escena de bano: espuma sobre el bicho y limpieza al reventar
uint32_t bathUntil = 0;
bool bathPending = false;
struct { int16_t x, y; uint8_t r, ph; } bubbles[14];
uint32_t feedMenuUntil = 0;   // selector de comida abierto hasta este millis

// minijuego "toques": mantener la pokeball en el aire
bool gameOpen = false;
uint32_t gameOverUntil = 0;
// The ball game used to run until you missed three times or walked away, so a
// session had no length and the reward no shape. The bag is 10 s and the
// reaction test 15 s; this sits between them, and whichever comes first --
// the clock or three misses -- ends it.
#define GAME_MS 20000UL
uint32_t gameUntil = 0;
float ballX, ballY, ballVX, ballVY, gamePetX;
uint8_t gameScore, gameMisses;
float hitX, hitY;             // ultimo golpe (anillo de impacto)
uint32_t hitTime = 0;
bool gameNewHi = false;

// The type quiz. Declared here with the other minigame flags because loop()
// and uiCurrentScreen() read it thousands of lines before its own section.
bool memoOpen = false;

// saco de entrenamiento (entrena la fuerza)
bool sackOpen = false;
uint32_t sackUntil = 0, sackOverUntil = 0;
uint16_t sackHits = 0;
float sackShake = 0;
uint8_t sackGain = 0;
bool sackNewHi = false;

// training submenu (the 5th icon): routes to the trainer for each stat.
// DEF has no minigame -- it rises on its own from good wellbeing -- so its row
// is informational and does not respond to a tap.
bool trainOpen = false;

// move picker, opened from the MOVES card page. Most learnsets are level 0, so
// a level-up "you learned a move" prompt would almost never fire -- the moveset
// is edited on demand instead.
bool movePickOpen = false;
uint8_t movePickSlot = 0;   // which of the 4 slots is being replaced
uint8_t movePickParty = 0;  // 0 = the live pet, else the party slot + 1
uint8_t movePickPage = 0;
bool evoPickOpen = false;
uint8_t evoPickPage = 0;
#define MOVE_ROW_Y(i) (96 + (i) * 58)
#define MOVE_PICK_PER_PAGE 5
#define MOVE_PICK_Y(i) (76 + (i) * 58)
#define EVO_PICK_PER_PAGE 3
#define EVO_PICK_ROW_H UI_TAP_FINGER
#define EVO_PICK_Y(i) (82 + (i) * (EVO_PICK_ROW_H + 4))
// level-up learn prompt: modal, and deliberately without a timeout -- it
// decides what the creature is for the rest of its life, and once banked into
// the party, forever.
#define LEARN_ROW_Y(i) (104 + (i) * 56)
#define LEARN_SKIP_Y 334

// ---------- battle ----------
// The move menu is a 2x2 grid rather than four stacked rows: the round panel
// has to fit both creatures, both HP bars and the menu, and four full-width
// rows do not leave room for the sprites.
bool battleOpen = false;

enum : uint8_t {
  SCR_STARTER = 0, SCR_REGION, SCR_GALLERY, SCR_DEXPICK, SCR_MOVEPICK, SCR_BOX,
  SCR_PARTY, SCR_KEYBOARD, SCR_CARD, SCR_PLAYER, SCR_CLOCK, SCR_GYM, SCR_GYMPICK,
  SCR_EXPLORE, SCR_LAN, SCR_PICK, SCR_BATTLE, SCR_WIN, SCR_LEARN, SCR_TRAIN, SCR_MENU,
  SCR_BAGSCR, SCR_GAME, SCR_JUKEBOX, SCR_MAIN, SCR_TRANSFER, SCR_MEDALS, SCR_EVOPICK, SCR_COUNT
};
extern const char *const SCREEN_NAME[SCR_COUNT];   // const is internal linkage in C++

// Declared HERE, above every use. tools/emu/build.sh generates a proto.h with
// every prototype at the top, so the emulator compiled these happily while
// arduino-cli -- which relies on the IDE's auto-prototyping and does not always
// produce one -- did not. A green emulator build is not proof the firmware
// builds; only arduino-cli is.
void bootReport();
uint8_t uiCurrentScreen();
// The gesture model, defined next to uiCurrentScreen() but used by handleTouch()
// ~1000 lines above it.
int uiTileIndex();
void uiTileGo(int i);
void onRim(int dir);
uint8_t uiRimPages();
uint8_t uiRimPage();
bool uiOnRim(int16_t x, int16_t y);
float uiRimAngle(int16_t x, int16_t y);
void uiDrawRimBar();
void uiChrome();
uint8_t learnableList(uint8_t *out, uint8_t max);
uint8_t pickCandidates();
void renderEvoPick();
void evoPickTap(int16_t x, int16_t y);
bool evoPickRowRect(uint8_t row, int *x, int *y, int *w, int *h);
// Declared here because renderMonSheet() and the sheet's tap handlers use both
// ~4000 lines above where they are defined. The emulator generates a proto.h and
// would compile it either way; arduino-cli would not, and that has shipped once.
void uiConfirmRects(int *b1Top, int *b1Bot, int *b2Top, int *b2Bot);
void renderParty();
void renderBox();
void renderMedals();
void drawSaveWarning();
// The bag and wild encounters are defined ~3000 lines below render() and the
// touch handler that reach them. arduino-cli does not always auto-prototype,
// and the emulator's generated proto.h would hide that -- see the note above.
void renderBag();
void bagTap(int16_t x, int16_t y);
uint8_t bagPages();
uint8_t btlBagPages();   // the battle bag, paged by the rim like every other list
bool startWildBattle(uint8_t region, bool hard);
PartyMon wildToPartyMon();
bool focusSwap(uint8_t slot);
bool clearHandover();
// Declared here because setup() calls it ~5000 lines above its definition, and
// arduino-cli does not always auto-prototype -- see the note at the top of this
// block. The emulator's generated proto.h would have hidden it.
void focusRecover();
// The GAME settings page (clockPage == 2) and random starters.
int gameStepAt(int r, int16_t x, int16_t y);
int uiGameRowY(int r);
int uiGameEggY();
int uiGameStepY();
bool gameEggAt(int16_t x, int16_t y);
bool gameStepToggleAt(int16_t x, int16_t y);
void setStepCounter(bool on);
void drawStepPill();
int uiGameBtnX(int step);
void lifeSpanText(char *out, size_t cap);
void renderGameOptions();
void gameOptionsTap(int16_t x, int16_t y);
int16_t randomStarterIn(uint8_t region);
int16_t randomStarter();
// The Pokedex filter and the box's on-screen order. Both are reached by
// uiRimTarget(), ~4000 lines above where they are defined, because the number of
// PAGES now depends on them -- and both are non-static so a test can drive the
// firmware's own answer instead of recomputing it.
bool galleryPass(int16_t dex);
bool galleryMember(int16_t dex);
uint16_t galleryCount();
int16_t galleryAt(uint16_t visual);
uint8_t galleryPages();
const char *galleryFilterName();
void galleryClampPage();
void galleryViewRect(uint8_t i, int *x, int *w);
int8_t galleryViewAt(int16_t x, int16_t y);
// The type quiz. render() and onTap() reach these ~2000 lines above where they
// are defined, and arduino-cli does not always auto-prototype.
void startMemoGame();
void renderMemo();
void memoTap(int16_t x, int16_t y);
void leaveMemo();
void quizOptRect(int i, int *x, int *y, int *w, int *h);
int quizOptAt(int16_t x, int16_t y);
uint16_t boxSlotFor(uint16_t visual);
bool boxHubHold(int16_t x, int16_t y);
void boxJumpRect(int dir, int *x, int *y, int *w, int *h);
const char *boxSortName();
uint8_t galleryFilterCount();
uint8_t boxSortCount();
// The derived personality's name, and today's checklist. Both are read by the
// card and the player screen, which sit either side of where they are defined.
const char *personalityName();
int uiGoalCount();
bool uiGoalMet(int i);
int uiGoalsMet();
void drawConfirmPanel(const char *q, const char *sub1, const char *sub2,
                      uint16_t subCol, const char *o1, uint16_t c1, uint16_t t1,
                      const char *o2, uint16_t c2, uint16_t t2);
void openJukebox();
bool jukeAudible();
void drawMusicNotes(uint32_t now);
void renderJukebox();
void jukeTap(int16_t x, int16_t y);
void jukeSwipe(int dir);
void jukeRowRect(int i, int *x, int *y, int *w, int *h);
void jukeNavRect(int which, int *x, int *y, int *w, int *h);
const char *tuneName(uint8_t t);
const char *const SCREEN_NAME[SCR_COUNT] = {
  "starter", "region", "gallery", "dexpick", "movepick", "box",
  "party", "keyboard", "card", "player", "clock", "gym", "gympick",
  "explore", "lan", "pick", "battle", "win", "learn", "train", "menu",
  "bag", "minigame", "jukebox", "main", "transfer", "medals", "evopick"
};

// Badge art for a region, or nullptr when that region has none yet.
//
// NEVER `BADGES_ART[region % BADGE_REGIONS]`. The moment GYM_REGIONS outgrew
// BADGE_REGIONS -- which happened when Kalos landed and the upstream badge set
// stops at Unova -- 5 % 5 = 0 silently dressed Kalos in KANTO's badges, on both
// the win screen and the player card. A wrong badge is worse than an honest
// blank: it claims you won something you did not. The callers draw a
// placeholder instead, and static_assert below stops the array being indexed
// out of range whatever the two counts are.
static_assert(BADGE_REGIONS <= GYM_REGIONS,
              "more badge sheets than ladders -- BADGES_ART would be indexed by a region that does not exist");
static const BadgeArt *badgeArtFor(uint8_t region, uint8_t i) {
  if (region >= BADGE_REGIONS || i >= TRAINER_GYMS) return nullptr;
  return &BADGES_ART[region][i];
}

// Does this region have its own badge art? Split out with a primitive return
// type so the tests can ask: the emulator's genproto.py emits every prototype
// ABOVE the includes, where the BadgeArt struct does not exist yet.
bool badgeArtExists(uint8_t region, uint8_t i) { return badgeArtFor(region, i) != nullptr; }

Combatant btlYou, btlFoe;
bool btlOver = false;
bool btlWon = false;
bool btlNewBadge = false;
uint32_t btlWinUntil = 0;   // the win screen is up
// A trainer fight is a run of 1v1s: both sides queue their squad and the next
// one steps up when the current one faints. This is the whole difficulty curve
// -- no gating, just attrition, so one strong creature sweeps Brock and dies
// four deep into Lance.
// The ladder is now sequential: a leader opens once the previous one is beaten,
// tracked per difficulty so hard mode is its own run. This replaces the earlier
// "no gating, attrition is the gate" rule -- with both ladders level-capped,
// nothing stopped you opening with Lance and simply losing, which read as a
// dead end rather than a challenge.
// reaction test (trains SPEED)
bool spdOpen = false;
uint32_t spdUntil = 0, spdOverUntil = 0, spdBorn = 0;
int16_t spdX = 0, spdY = 0;
uint16_t spdHits = 0, spdMisses = 0;
uint8_t spdGain = 0;
bool spdNewHi = false;

bool gymOpen = false;
bool gymHard = false;   // which ladder the list is showing

// LAN battle. `lanOpen` is the pairing screen; once both squads are known the
// normal battle screen takes over with btlLink set.
bool lanOpen = false;
Link lan;
// What the shared region chooser is being used FOR. It only changes the
// subtitle and whether there is a way back: at first boot every count would
// read zero, which tells the player nothing, and there is nowhere to go back to.
#define RPICK_FOR_GYMS  0
#define RPICK_FOR_DEX   1
#define RPICK_FOR_START 2
// Three rows is what the round panel fits above the BACK label. The dex now
// lists four regions and will list more, so the chooser PAGES rather than
// growing -- and every paged screen in this sketch has to be driven by
// swipe_test, which is why that test exists at all.
#define RPICK_PER_PAGE  3
extern uint8_t rpickPage;
static void renderRegionPick(uint8_t mode);   // the region chooser, defined below
// Not static: swipe_test drives these so it asks the FIRMWARE for the page
// count instead of recomputing it from its own copy of RPICK_PER_PAGE, which
// would prove the transcription rather than the screen.
uint8_t rpickRegions(uint8_t mode);           // rows this mode lists (gyms: 3)
uint8_t rpickPageCount(uint8_t mode);
uint8_t rpickModeNow();                       // which chooser is up, 0xFF for none
uint8_t rpickModeNow();                       // which chooser is up, or 0xFF
static bool rpickSwipe(int dir);              // true if it handled the gesture
// NOT static: explore_test drives it, because the sprite-pack gate it enforces
// now covers the gym ladders as well and a greyed row that still answered a tap
// is exactly the fault that change fixes.
int regionPickTap(int16_t x, int16_t y, uint8_t mode);
static void drawEggRegion();          // defined with the egg screen helpers
static int eggRegionTap(int16_t x, int16_t y);
static void drawBtlBack();
static void btlLinkPoll();   // defined with the battle code, called from render()
static void btlSwitchTo(uint8_t i);
static void btlResolve(uint8_t yourMove);
// The peer's whole team, kept live. A trainer's replacements are built fresh
// from TRAINERS[] because they only ever arrive once; a linked opponent can
// switch OUT and back IN, so its creatures have to remember how battered they
// are. Host side only -- the guest takes absolute health off the wire.
Combatant btlFoeSquad[TRAINER_TEAM_MAX];
uint8_t btlFoeSquadN = 0;
uint8_t btlMyAct = 0;        // host: our own action, latched until theirs lands
// Which ladder the gym screen and the current fight belong to. The battle keeps
// its own copy so that leaving the gym list mid-fight cannot retarget the badge.
// The BOX button on the party screen. It was 150x32 with a pixel-exact hit
// test, which is a small target on a round panel -- reported as hard to press,
// same complaint as the battle grid. Now bigger AND padded: the drawn size grew
// too, so the button looks like the size it actually is rather than hiding a
// generous hit area behind a small graphic.
// The smallest a button may be. Three separate "hard to hit" reports -- the
// battle grid's bottom row, the party screen's BOX, and the LAN button -- were
// all the same mistake: a control sized to fit its label rather than a finger.
//
// 44 is the number everyone quotes, but it is 44 POINTS. This panel is 466 px
// across 1.75 in = 266 ppi, so a pixel is 0.095 mm and 44 px is 4.2 mm, under
// half a fingertip. It survives as the HARD FLOOR because several laid-out
// screens cannot grow without being redesigned, but nothing new may use it.
#define UI_TAP_MIN 44
// 9 mm -- what watchOS and Wear OS both land on -- in this panel's pixels.
// Every control laid out from here on is held to THIS by hit_test.
#define UI_TAP_FINGER 94

// The LAN battle button on the gym region chooser.
#define LANBTN_W 190
#define LANBTN_H UI_TAP_MIN
#define LANBTN_X (233 - LANBTN_W / 2)
#define LANBTN_Y 336

// The BOX button is the hub in the middle of the party ring now (PSLOT_HUB_R),
// so it has no rectangle of its own any more.
// CLOSE sits below BOX with a real gap between them. They used to touch at
// y=372, and BOX's padding then reached to 380 -- so the top of CLOSE was
// inside BOX's hit area and taps there opened the box instead of closing the
// screen. Padding one button into its neighbour just moves the problem along.
#define PARTYCLOSE_Y 376
#define PARTYCLOSE_H UI_TAP_MIN
#define PARTYCLOSE_X 133
#define PARTYCLOSE_W 200

// The confirm panel, in ONE place. Three callers draw it -- the evolve/farewell/
// retire dialog on the main screen, and letting a banked creature go from the
// party or box sheet -- and hit_test asserts against the same numbers through
// uiConfirmRects(). Copies of this geometry in the tap handler are exactly how
// a YES button ends up somewhere the drawing is not.
#define CONFIRM_X 73
#define CONFIRM_Y 156
#define CONFIRM_W 320
#define CONFIRM_H 188
#define CONFIRM_BTN_X 93
#define CONFIRM_BTN_W 280
#define CONFIRM_BTN_H 52
#define CONFIRM_B1_Y 206
#define CONFIRM_B2_Y 268

// The two buttons on the party/box detail sheet. The primary action owns the
// panel centre. Party: MAKE ACTIVE / TO BOX. Box: TO PARTY / RELEASE.
//
// THEY ARE NOT EQUAL HALVES, and that is the whole point. The first version
// centred the pair on 233 with a dead gap between them -- which put the gap at
// the CENTRE OF THE PANEL, the one place a thumb naturally lands, so BRING BACK
// "did not work" and was reported as broken. Worse, every build up to v3.5 drew
// BRING BACK as a FULL-WIDTH button centred exactly there, so muscle memory
// aimed straight at the dead zone.
//
// The primary action now owns the centre. RELEASE is narrower, offset right,
// and still over UI_TAP_MIN -- it is irreversible, so it should be reachable
// but never the thing you hit by aiming at the middle. The gap between them is
// wider than before, not narrower.
#define PDET_BTN_Y 326
#define PDET_BTN_H 64
#define PDET_L_X 64
#define PDET_L_W 200
#define PDET_R_X 282
#define PDET_R_W 120
static_assert(PDET_L_X < 233 && 233 < PDET_L_X + PDET_L_W,
              "the panel centre must land on the sheet's PRIMARY button, not between the two");
static_assert(PDET_R_X > PDET_L_X + PDET_L_W + 8,
              "the destructive button needs a real dead gap before it");

uint8_t gymRegion = 0;
uint8_t btlRegion = 0;
#define TRAINERS (TRAINER_SETS[gymRegion % GYM_REGIONS].list)
#define BTL_TRAINERS (TRAINER_SETS[btlRegion % GYM_REGIONS].list)
bool gShowAllAvatars = false;  // emulator screenshot aid, never set on hardware
// Emulator-only console commands. A RUNTIME FLAG, not a #ifdef, and deliberately:
// tests/run.sh compiles these same sources twice -- once with g++ for the
// emulator and once with arduino-cli for the board -- and treats any divergence
// between the two as the bug it is hunting. A -D would give the firmware build
// different code to compile, which is exactly what that check exists to catch.
// So the board COMPILES this and simply never sets the flag; main_sdl.cpp does.
// Same shape as gShowAllAvatars above, for the same reason.
bool gEmuDebug = false;
bool btlPetIn = false;       // was the live pet in the squad?
uint8_t btlTrainGain = 0;    // what the win trained, for the win screen
uint8_t btlTrainWhich = 0;
bool btlLink = false;      // this fight is against another device
bool btlLinkHost = false;
static bool gymUnlocked(uint8_t idx, bool hard) {
  return idx == 0 || pet.hasBadge(gymRegion, idx - 1, hard);
}

// Team select. Candidate 0 is the live pet, 1..PARTY_SLOTS are the banked
// members, so one bitmask covers the whole pool.
bool pickOpen = false;
// The team picker serves the gym ladder and the LAN screen both. PICK_LAN is
// not a trainer index: squadCap() already returns an uncapped six for anything
// past the roster, which is what a LAN battle wants -- two players who know
// each other can bring what they like.
#define PICK_LAN 0xFF
static void lanOffer(bool host);
uint8_t pickTrainer = 0;
bool pickHard = false;
// Which region the picker was opened FOR, snapshotted when it opens -- the same
// arrangement as btlRegion, and for the same reason. gymRegion belongs to the
// chooser and the player can move it; anything the picker decides has to keep
// pointing at the ladder it was opened from. Forgetting to fill in btlRegion is
// what sent Brock's Onix into every region's gym (issue #4), so the two reads
// this alias replaces are the same shape one screen earlier.
uint8_t pickRegion = 0;
#define PICK_TRAINERS (TRAINER_SETS[pickRegion % GYM_REGIONS].list)
bool lanWantHost = true;   // which button opened the picker
uint16_t squadMask = 0xFFFF;   // everything, until the player says otherwise
uint8_t pickPage = 0;
#define PICK_PER_PAGE 6
#define PICK_CELL_W 150
#define PICK_CELL_H 74
#define PICK_X(i) (78 + ((i) % 2) * (PICK_CELL_W + 10))
#define PICK_Y(i) (86 + ((i) / 2) * (PICK_CELL_H + 6))
#define PICK_GO_Y 350
// BACK beside FIGHT on the team-select screen. There was no way out of it but a
// swipe, which is invisible -- the same complaint as everywhere else.
#define PICK_BTN_W 155
#define PICK_BTN_H UI_TAP_MIN
#define PICK_BACK_X (233 - PICK_BTN_W - 7)
#define PICK_GO_X (233 + 7)
bool playerOpen = false;
// One badge page per gym region. Three ladders will not fit on one page, and
// the page you are on IS the region -- no extra control needed. Derived from
// GYM_REGIONS rather than written out, so adding a ladder adds its own page.
uint8_t playerPage = 0;
#define PLAYER_PAGES GYM_REGIONS
// The creature's own screen, between the trainer card and the team: TODAY's
// checklist first, the rim turns to its medals. Both are about the creature in
// front of you, where the trainer card is about you.
bool medalsOpen = false;
uint8_t medalsPage = 0;
#define MEDALS_PAGES 2
#define playerBadgeRegion (playerPage % GYM_REGIONS)
uint8_t gymPage = 0;
#define GYM_ROWS 5
#define GYM_ROW_Y(i) (110 + (i) * 50)
#define GYM_ROW_H 44
#define GYM_ROW_INSET 18   // how far a row stops short of the glass, so the
                           // arc scrollbar at RIM_BAR_R has its own lane
void gymRowRect(int i, int *x, int *y, int *w, int *h);
// LAN remains attached to the trainer ladder; Explore is a peer destination on
// the tile axis and no longer hides behind GYMS.
#define GYMBTN_Y 380
#define GYMBTN_W 190
#define GYMBTN_H UI_TAP_MIN
#define GYMBTN_X (CX - GYMBTN_W / 2)
int8_t btlTrainer = -1;      // index into TRAINERS, -1 = a one-off fight
bool btlHard = false;
Combatant btlSquad[TRAINER_TEAM_MAX + 1];
uint8_t btlSquadN = 0, btlSquadAt = 0;
uint8_t btlFoeAt = 0;

// ---------------------------------------------------------------------------
// The bag, and wild encounters.
bool exploreOpen = false;
bool exploreHard = false;
uint8_t exploreRegion = 0xFF;
#define EXPLORE_REGION_X 78
#define EXPLORE_REGION_Y 170
#define EXPLORE_REGION_W 310
#define EXPLORE_REGION_H UI_TAP_FINGER
#define EXPLORE_REGION_ARROW_W UI_TAP_FINGER
#define EXPLORE_DIF_Y 272
#define EXPLORE_DIF_H UI_TAP_MIN
#define EXPLORE_BTN_X 78
#define EXPLORE_BTN_Y 326
#define EXPLORE_BTN_W 310
#define EXPLORE_BTN_H 72
#define EXPLORE_BTN_PAD 11
bool bagOpen = false;
uint8_t bagPage = 0;
#define BAG_PER_PAGE 5
#define BAG_ROW_Y(i) (112 + (i) * 58)

// A wild fight is a different fight, not a trainer fight with a flag: a ball
// works, the foe can run, and RUN is a roll rather than a certainty. Every one
// of those is asked through btlWild, so there is no second opinion about what
// kind of battle is on screen.
bool btlWild = false;
// The wild creature's durable state, kept BESIDE the Combatant because a
// Combatant carries no IVs and none of what a stored record needs. Without it a
// capture would have to re-roll the individual it just caught, and the creature
// you fought would not be the creature you kept.
int16_t wildDex = 0;
uint8_t wildLvl = 1;
uint8_t wildIv[4] = { 0, 0, 0, 0 };   // atk, def, spe, hp
bool wildShiny = false;
bool wildCaught = false;
// What a win handed over, for the settlement lines on the win screen.
ItemKey wildDrops[4] = { ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE };
uint8_t wildDropN = 0;
uint8_t btlBagPage = 0;

// Animation. Deliberately built on the thumbnails the screen already draws
// rather than on PmdMon: three PmdMon blobs are live already, and the battle
// has to stay graceful on a board with no SD at all (S_NO_SPRITES). Index 0 is
// you, 1 is the foe.
uint32_t btlLungeUntil[2] = { 0, 0 };   // acted: leans toward the opponent
uint32_t btlHitUntil[2] = { 0, 0 };     // was hit: jitters and flashes
uint16_t btlHpShown[2] = { 0, 0 };      // bars ease toward the real value
// Two streamed sprites, so the creatures can actually swing and flinch. They
// cost ~135 KB of PSRAM each on average and are freed when the fight ends. The
// player's side is NOT the global `pmd`: the active creature may be a banked
// party member rather than the live pet.
PmdMon btlPmd[2];
int16_t btlPmdDex[2] = { 0, 0 };
// A faint used to swap the next creature in instantly, inside the same call
// that resolved the turn -- which is why it felt like a jump cut. The swap is
// now deferred: the fainted one drops out of frame, and the replacement slides
// in only once the player dismisses that message.
uint32_t btlFaintUntil[2] = { 0, 0 };
uint32_t btlEnterUntil[2] = { 0, 0 };
int8_t btlSwapWho = -1;        // 0 = your side, 1 = the foe's, -1 = nothing due
#define BTL_FAINT_MS 700
#define BTL_ENTER_MS 420
// battle menu: 0 = FIGHT/POKEMON, 1 = the moves, 2 = the switch list
uint8_t btlMenu = 0;
#define BTL_LUNGE_MS 260
#define BTL_HIT_MS 420
char btlMsg[6][40];
uint8_t btlMsgCount = 0;   // queued lines; a tap shows the next
#define BTL_CELL_W 160
#define BTL_CELL_H 54
#define BTL_GRID_X 69
#define BTL_GRID_Y 270
#define BTL_GRID_H (BTL_CELL_H * 2 + 8)
#define BTL_CELL_X(i) (BTL_GRID_X + ((i) % 2) * (BTL_CELL_W + 8))
#define BTL_CELL_Y(i) (BTL_GRID_Y + ((i) / 2) * (BTL_CELL_H + 8))

// A cell's HIT area is bigger than the cell that is drawn. On the board the two
// bottom buttons were much harder to hit than the top two: the drawn cells are
// only 44 px tall, there was an 8 px dead gap between the rows, and everything
// below the bottom row was dead too -- so a finger landing low, or a touch panel
// reading a few pixels high, missed entirely. Nothing else is tappable in this
// area while the grid is open, so the slop costs nothing.
//
// The gap between the two rows and the two columns is split down the middle, and
// the bottom row additionally claims the empty space beneath it.
#define BTL_HIT_PAD 4
// The bottom row keeps extra room downward, but not so much that it reaches the
// BACK bar below it -- the mistake made once already with BOX and CLOSE.
#define BTL_HIT_BOTTOM 6
// BACK, under the grid: the move and switch screens had no way out except
// choosing something.
// The gym list's EASY/HARD pill. It was 24 px tall, which is half a fingertip.
// Sits between the title and the first leader row. At 72 with a 44 px height it
// ran to 116 and overlapped the first row, which begins at 110 -- introduced
// when the pill was enlarged to a real tap target.
#define GYMDIF_Y 60
#define GYMDIF_H UI_TAP_MIN
#define BTL_BACK_W 190
#define BTL_BACK_H UI_TAP_MIN
#define BTL_BACK_X (233 - BTL_BACK_W / 2)
#define BTL_BACK_Y 396
#define BTL_HIT_X0(i) (BTL_CELL_X(i) - BTL_HIT_PAD)
// The far edges stop one pixel short so the four boxes TILE: the gap between
// two cells is split down the middle with no pixel left over and none shared.
#define BTL_HIT_X1(i) (BTL_CELL_X(i) + BTL_CELL_W + BTL_HIT_PAD - 1)
#define BTL_HIT_Y0(i) (BTL_CELL_Y(i) - BTL_HIT_PAD)
#define BTL_HIT_Y1(i) (BTL_CELL_Y(i) + BTL_CELL_H - 1 + \
                       ((i) / 2 ? BTL_HIT_BOTTOM : BTL_HIT_PAD))

// Which cell a point falls in, or -1. Exposed (not static) so a test can sweep
// the panel and prove there are no dead pixels between the cells -- the bug that
// made the bottom row hard to press was a gap, not a wrong rectangle.
int btlCellIndexAt(int16_t x, int16_t y);

// Used by BOTH the move grid and the switch grid. They had a copy each of the
// same rectangle test, which is exactly how two halves of one control drift.
static inline bool btlCellHit(int i, int16_t x, int16_t y) {
  return x >= BTL_HIT_X0(i) && x <= BTL_HIT_X1(i) &&
         y >= BTL_HIT_Y0(i) && y <= BTL_HIT_Y1(i);
}
// The training menu, now FOUR rows: the type quiz joins the three stat
// trainers. Retuned rather than stretched -- at the old 56 px pitch a fourth row
// would end at y 442, and the panel is bounded by the glass, not by taste.
//
// The panel is 312 wide, so its corners sit 156 from the centre, and the BOTTOM
// pair is what binds: sqrt(233^2 - 156^2) = 173, so the panel cannot reach past
// y = 233 + 173 = 406. At 326 tall it ends at 404 and its worst corner is at
// radius 231 of 233. The first attempt at this was 332 tall and put those corners
// at 236 -- off the glass, which is the mistake the gym ladder made before
// uiSafeHalfWidthFor() existed, so the numbers above are checked rather than
// judged. Rows are UI_TAP_MIN tall with a 10 px gap.
#define TRAIN_X (CX - 156)
#define TRAIN_Y 78
#define TRAIN_W 312
#define TRAIN_H 326
#define TRAIN_ROW_H UI_TAP_MIN
#define TRAIN_ROW_GAP 10
#define TRAIN_ROW_Y(i) (TRAIN_Y + 46 + (i) * (TRAIN_ROW_H + TRAIN_ROW_GAP))
// FOUR, and referred to by name: the tap handler used to compare `i` against
// literals, which is how removing an icon once silently made BATH the wake button.
#define TRAIN_ROWS 4
#define TRAIN_ROW_ATK 0
#define TRAIN_ROW_SPE 1
#define TRAIN_ROW_DEF 2
#define TRAIN_ROW_MEMO 3

// Settings page 2's rows. See the essay above renderClockOptions().
#define SET_ROWS 5
#define SET_ROW_H UI_TAP_MIN
#define SET_ROW_GAP 10
#define SET_ROW_Y(i) (92 + (i) * (SET_ROW_H + SET_ROW_GAP))
#define SET_ROW_INSET 16
#define SET_SOUND 0
#define SET_VOL   1
#define SET_BRIGHT 2
#define SET_LANG  3
#define SET_RESET 4

// The type quiz (it took the memory game's place). See the essay above startMemoGame().
#define QUIZ_MS 20000UL      // one clock for the whole game, like the ball game
#define QUIZ_LIVES 3         // ...or three mistakes, whichever comes first
#define QUIZ_OPTS 3          // three attacking types, exactly one super effective
#define QUIZ_FLASH_MS 450    // how long a right / wrong answer shows before the next
#define QUIZ_OPT_W 240
#define QUIZ_OPT_H 54
#define QUIZ_OPT_Y(i) (168 + (i) * (QUIZ_OPT_H + 10))

// Today's care checklist rows. See the essay above renderPlayerGoals().
#define GOAL_ROW_H UI_TAP_MIN
#define GOAL_ROW_GAP 8
#define GOAL_ROW_Y(i) (104 + (i) * (GOAL_ROW_H + GOAL_ROW_GAP))

// las 9 especies con sprite propio en flash (respaldo sin SD): dex -> indice
int flashIdxForDex(int16_t dex) {
  static const int8_t IDX[10] = { -1, 3, 4, 5, 0, 1, 2, 6, 7, 8 };
  return (dex >= 1 && dex <= 9) ? IDX[dex] : -1;
}

#define CX 233  // centro de la pantalla redonda
#define CY 233
#define UI_R 233   // the glass is a CIRCLE, and every full-width layout must ask
#define HEADER_NAME_Y 52
#define HEADER_NAME_H 24
#define PET_CY 202  // centro vertical del sprite

// Half the usable width at row y: the chord of the panel at that height.
//
// A layout that picks ONE width for every row is wrong twice -- it wastes the
// middle, where the full 466 px is available, and it hangs the corners of its
// top and bottom rows off the glass. Both had shipped: the gym ladder drew five
// identical 326 px rows over y 110..374, where the real chord runs 448 down to
// 372. Derive it, never restate it; this is the only place the circle is solved.
int uiSafeHalfWidth(int y) {
  int dy = y - CY;
  if (dy <= -UI_R || dy >= UI_R) return 0;
  return (int)sqrtf((float)(UI_R * UI_R - dy * dy));
}

// The narrower of the two chords a box spanning y0..y1 must fit inside. A
// rounded row is widest at its vertical middle, but its CORNERS are what leave
// the circle, so it has to be sized by its worst edge rather than its centre.
int uiSafeHalfWidthFor(int y0, int y1) {
  int a = uiSafeHalfWidth(y0), b = uiSafeHalfWidth(y1);
  return a < b ? a : b;
}

// An arc segment: Arduino_GFX has no arc primitive. The band is built from
// solid quads (two triangles per slice, edges ~3 px long) so it is one even
// stroke, with a round cap at each end. Stamping overlapping dots instead left
// a scalloped, patchy edge that read as "drawn in" (issue #23).
// Angles are degrees clockwise from 12 o'clock.
void uiArc(int cx, int cy, int r, int a0, int a1, int w, uint16_t col) {
  if (a1 < a0) { int t = a0; a0 = a1; a1 = t; }
  int rad = w / 2;
  if (rad < 1) rad = 1;
  int ri = r - rad, ro = r + rad;
  int n = (int)(((a1 - a0) * 0.01745329f * r) / 3.0f) + 1;   // ~3 px per slice
  float step = (float)(a1 - a0) / (float)n;
  int px0 = 0, py0 = 0, px1 = 0, py1 = 0;
  for (int i = 0; i <= n; i++) {
    float ang = ((float)a0 + step * (float)i - 90.0f) * 0.01745329f;
    float c = cosf(ang), s = sinf(ang);
    int x0 = cx + (int)lroundf(c * ri), y0 = cy + (int)lroundf(s * ri);
    int x1 = cx + (int)lroundf(c * ro), y1 = cy + (int)lroundf(s * ro);
    if (i > 0) {
      gfx->fillTriangle(px0, py0, px1, py1, x1, y1, col);
      gfx->fillTriangle(px0, py0, x1, y1, x0, y0, col);
    }
    px0 = x0; py0 = y0; px1 = x1; py1 = y1;
  }
  // round caps
  float e0 = ((float)a0 - 90.0f) * 0.01745329f, e1 = ((float)a1 - 90.0f) * 0.01745329f;
  gfx->fillCircle(cx + (int)lroundf(cosf(e0) * r), cy + (int)lroundf(sinf(e0) * r), rad, col);
  gfx->fillCircle(cx + (int)lroundf(cosf(e1) * r), cy + (int)lroundf(sinf(e1) * r), rad, col);
}

// The gesture model. Its tables and handlers live down beside uiCurrentScreen(),
// but handleTouch() and onSwipe() sit ~1000 lines above them.
#define TILE_COUNT 7   // PLAYER . MEDALS . PARTY . [PET] . EXPLORE . GYM . DEX
#define TILE_PET 3
#define RIM_R0 186     // a drag starting outside this radius pages, not swipes
#define RIM_STEP 20    // degrees of arc per page
#define RIM_BAR_R 222  // where the arc scrollbar is drawn
// 2 to 5 o'clock. Angles are clockwise from 12, so this is the right flank --
// the top is the title's and the bottom is the tile dots'.
#define RIM_BAR_A0 55
#define RIM_BAR_A1 145

// A gym row's rectangle -- as wide as the panel allows AT ITS OWN HEIGHT, so
// the ladder bulges in the middle instead of throwing away 66 px there and
// hanging its top and bottom corners off the glass. The draw path and the tap
// path both ask this; they used to share the literals 70 and 396.
void gymRowRect(int i, int *x, int *y, int *w, int *h) {
  int ry = GYM_ROW_Y(i);
  int half = uiSafeHalfWidthFor(ry, ry + GYM_ROW_H) - GYM_ROW_INSET;
  if (x) *x = CX - half;
  if (y) *y = ry;
  if (w) *w = half * 2;
  if (h) *h = GYM_ROW_H;
}

static const uint16_t INK_K = 0x18C4;  // spriteColor('k')

// botones de icono siguiendo el arco inferior de la pantalla redonda
// (los exteriores van mas altos para no salirse del circulo)
struct Btn {
  int16_t cx, cy;
  const char *const *icon;
};
// Five across the arc: spacing tightened 62 -> 54 so the outer pair stays far
// enough in to keep its old y. Lifting them instead would have run the row into
// the ENE/HYG bars, which end at y=361 -- the buttons are 52 tall, so any centre
// above 387 overlaps them.
// Four, not five. The ball had its own icon here until it became DEFENCE's
// trainer and moved into the training menu -- at which point tapping it just
// opened the same menu the dumbbell does, two icons for one destination.
//
// They sit on the panel's curve: y = 372 - dx^2/1100, the outer pair lifted so
// the row stays on glass. At 80 px and 88 px apart the outermost reaches radius
// 220 of 233 -- the row cannot grow again without losing an icon, because four
// UI_TAP_FINGER targets need 392 px of chord and reach radius 222.
// What the creature is eating, remembered from the food picker so the animation
// can draw it (0 apple, 1 blueberry, 2 pear, 3 candy). Not saved.
static uint8_t eatItem = 0;

#define BTN_COUNT 4
// Referred to by NAME, never by literal index. Removing the ball icon shifted
// every index by one and drawButtons() still had `i != 2` meaning LIGHT -- which
// silently made the BATH button the one that wakes the pet.
#define BTN_FOOD  0
#define BTN_LIGHT 1
#define BTN_BATH  2
#define BTN_TRAIN 3
Btn buttons[BTN_COUNT] = {
  {  92, 352, SPR_ICON_FOOD },   // comer
  { 186, 370, SPR_ICON_LIGHT },  // luz
  { 280, 370, SPR_ICON_CLEAN },  // bano
  { 374, 352, SPR_ICON_TRAIN },  // entrenar
};
// The GRAPHIC is 72 px and the HIT AREA is 94. They were the same 80 px, which
// looked heavy without actually reaching the finger floor -- the usual answer
// here is a target bigger than the thing it draws, which is what the battle
// grid and the egg pill already do. At a 94 px pitch the four hit discs are
// exactly tangent: no overlap to make a tap ambiguous, no gap to swallow one.
#define BTN_HALF 36
#define BTN_HIT 47

// grietas del huevo (pixeles 'k' sobre el sprite)
static const uint8_t CRACK1[][2] = { {15,8},{16,9},{15,10} };
static const uint8_t CRACK2[][2] = { {11,13},{12,14},{11,15},{20,12},{19,13},{20,14} };
// estrellas del modo noche
#define STAR_COUNT 40

bool wasPressed = false;
// eleccion de inicial (primera partida): Bulbasaur / Charmander / Squirtle, 3 filas
// The first-boot starter list is the FRONT of each region's starter array in
// dex.h -- not a copy of it. That array is also the pool a region's first egg
// is drawn from (pet.cpp rollInRegion), where Kanto's five deliberately include
// Pikachu and Eevee; the choice screen shows the canonical three and leaves the
// rest to the egg. starter_test pins the first three of every region, so
// reordering that array cannot silently change the first screen anyone sees.
#define STARTER_SHOWN 3
int16_t starterOf(uint8_t region, uint8_t i) {
  const RegionInfo &rg = REGIONS[region % REGION_COUNT];
  if (i >= rg.starterCount) i = 0;
  return rg.starters[i];
}
uint8_t starterCountShown(uint8_t region) {
  uint8_t n = REGIONS[region % REGION_COUNT].starterCount;
  return n < STARTER_SHOWN ? n : STARTER_SHOWN;
}

// First boot runs region -> starter. This is NOT persisted: a reset between the
// two lands back on the region, which is the harmless direction to fail in --
// nothing has been chosen yet, and pet.setRegion() is idempotent.
bool starterRegionDone = false;   // not static: starter_test drives both steps
#define STARTER_ROW_Y 110
#define STARTER_ROW_H 70
#define STARTER_ROW_GAP 8
// Two random buttons, neither of which says which starter it chose: the egg
// hatches as a surprise, the same as every egg after it.
//  * Under the three rows of the starter screen: one of THAT region's three.
//  * On the region screen, where the gyms' LAN button sits elsewhere: one of the
//    canonical three of EVERY region whose sprites are on the card, all equally
//    likely. No region was chosen, so the eggs after it come from all of them.
#define STARTER_RAND_X 110
#define STARTER_RAND_W 246
#define STARTER_RAND_Y 350
#define STARTER_RAND_H 48
int16_t randomStarterIn(uint8_t region) {
  const uint8_t n = starterCountShown(region);
  return starterOf(region, n ? (uint8_t)random(n) : 0);
}
int16_t randomStarter() {
  int16_t pool[REGION_COUNT * STARTER_SHOWN];
  int n = 0;
  for (uint8_t r = 0; r < REGION_COUNT; r++) {
    if (r == REGION_ALL || !regionAvailable(r)) continue;
    for (uint8_t i = 0; i < starterCountShown(r); i++) pool[n++] = starterOf(r, i);
  }
  return n ? pool[random(n)] : starterOf(pet.region, 0);
}
// boton-CTA de evolucion (centrado, mitad de pantalla)
#define EVO_BTN_W 256
#define EVO_BTN_H 64
#define EVO_BTN_X (CX - EVO_BTN_W / 2)
#define EVO_BTN_Y 172
// boton-CTA de despedida (mas ancho: lleva el nombre + frase)
#define FAR_BTN_W 408
#define FAR_BTN_H 58
#define FAR_BTN_X (CX - FAR_BTN_W / 2)
#define FAR_BTN_Y 176

// The food popup. It had its rectangle written out twice -- once where it is
// drawn and once in the tap handler -- which is the shape that drifts the
// moment either moves, and it had to move: the home row grew up into it.
#define FEED_MENU_X 101
#define FEED_MENU_Y 236
#define FEED_MENU_W 264
#define FEED_MENU_H 64
// el CST9217 avisa por el pin INT cuando hay datos tactiles; lo usamos para no
// leer el bus I2C mientras el chip esta dormido (esa lectura se colgaba ~1s)
volatile bool gTouchIrq = false;
void IRAM_ATTR touchIsr() { gTouchIrq = true; }
uint32_t lastRender = 0;
bool gFirstFrame = true;
// proteccion del AMOLED: atenuado por inactividad
uint32_t lastInteract = 0;
uint8_t dimStage = 0;        // 0 despierto, 1 atenuado (90s), 2 casi apagado (5min)

// ---------------------------------------------------------------------------
// BRIGHTNESS: the player's setting, and what the panel is actually showing.
//
// The setting is a CEILING on the automatic ladder, not a replacement for it.
// The dim stages and the sleep dimming are AMOLED protection rather than
// preferences -- a setting that overrode them would let somebody park a static
// screen at full brightness for a week -- so the level raises or lowers the
// attended brightness and the ladder still walks down from wherever that lands.
//
// gBrightLevel is persisted under "brt", exactly as the volume is under "vol",
// and BRIGHT_DEFAULT is chosen so that brightBase() returns the numbers this
// firmware has always used. An existing save has no "brt" key, reads the
// default, and behaves bit-identically -- which is the whole requirement: a
// missing setting must mean today's behaviour, never zero.
#define BRIGHT_LEVELS 10
#define BRIGHT_DEFAULT 7
uint8_t gBrightLevel = BRIGHT_DEFAULT;
// What the panel is set to RIGHT NOW. It used to be a function-local static
// inside updateBrightness(), which meant the one fact "the screen is dark" was
// unreachable from anywhere else -- and made the setting unable to take effect,
// since a level change that happened to compute the same target was cached away.
uint8_t gBrightNow = 255;

// The attended, undimmed brightness for a level. THE one place the setting turns
// into a number.
uint8_t brightBase(uint8_t level, bool usb) {
  if (level < 1) level = 1;
  if (level > BRIGHT_LEVELS) level = BRIGHT_LEVELS;
  const uint8_t v = usb ? 180 : 145;     // what it has always been at the default
  if (level >= BRIGHT_DEFAULT)
    return (uint8_t)(v + (uint16_t)(255 - v) * (level - BRIGHT_DEFAULT) /
                             (BRIGHT_LEVELS - BRIGHT_DEFAULT));
  // Below the default it scales down, with a floor: 0 belongs to the PWR button,
  // and a settings screen able to reach it could leave a player with a black
  // panel and no visible way back.
  const uint16_t lo = (uint16_t)v * level / BRIGHT_DEFAULT;
  return (uint8_t)(lo < 8 ? 8 : lo);
}

void brightLoad() {
  Preferences p;
  if (!p.begin("tamapoke", true)) return;
  uint8_t v = p.getUChar("brt", BRIGHT_DEFAULT);
  p.end();
  if (v < 1 || v > BRIGHT_LEVELS) v = BRIGHT_DEFAULT;   // junk reads as default
  gBrightLevel = v;
}

void brightSetLevel(uint8_t level) {
  if (level < 1) level = 1;
  if (level > BRIGHT_LEVELS) level = BRIGHT_LEVELS;
  gBrightLevel = level;
  gBrightNow = 255;      // force updateBrightness() to re-apply on the next pass
  Preferences p;
  if (!p.begin("tamapoke", false)) return;
  p.putUChar("brt", gBrightLevel);
  p.end();
}
bool swallowGesture = false; // el toque que despierta no acciona nada
uint32_t holdStart = 0;     // pulsacion larga sobre el bicho
uint32_t confirmUntil = 0;  // dialogo "soltar?" activo hasta este millis
uint8_t choiceKind = 0;     // dialogo de decision: 0 ninguno, 1 evolucion, 2 despedida
uint32_t choiceUntil = 0;   // se cierra solo a este millis
int16_t tX0, tY0, tXl, tYl; // gesto en curso (inicio y ultima posicion)
uint32_t tStart = 0;
bool holdFired = false;
// Rim-drag state. Decided at touch-down: a gesture that starts outside RIM_R0
// is a scroll for its whole life, and never also resolves as a swipe or a tap.
bool rimActive = false;
bool rimMoved = false;
float rimA0 = 0;
int rimSteps = 0;

// A factory reset has emptied NVS and we are waiting to restart into a new game.
// Nothing may write the in-RAM creature back over it in the meantime.
bool saveInhibited = false;

// Flush a pending save because power is about to go away: the PWR key on its way
// to the AXP2101's hardware cut, the battery's low warning, or a software
// restart. THE single answer for all three, so the one case where it must NOT
// happen cannot be forgotten by whoever adds the fourth -- see CLAUDE.md § "A
// rule enforced in one path but not in its twin", which is a list of what
// happens when three callers each keep their own copy of a guard.
//
// Deliberately ignores the dim/off gate that the periodic autosave in loop()
// obeys. That gate exists so a ~1 s flash write is never visible mid-animation;
// a visible stutter is a far smaller price than the last few minutes of a
// creature's life.
void flushBeforePowerLoss(const char *why) {
  if (saveInhibited || focusBlocked) return;
  if (!pet.savePending()) return;
  Serial.printf("save: flushing (%s)\n", why);
  pet.flushSave();
}

void setup() {
  Serial.setRxBufferSize(8192);  // la transferencia a SD llega en bloques de 2 KB
  Serial.begin(115200);
  // CRITICO: sin esto, Serial.print BLOQUEA el juego cuando no hay un
  // monitor serie abierto en el host (el bufer TX del USB CDC se llena
  // y nadie lo vacia) -> con timeout 0 los mensajes se descartan
  Serial.setTxTimeoutMs(0);
  Serial.printf("TamaPoke fw v%s\n", FW_VERSION);
  bootReport();   // why the last run ended, and what it was doing
  // BEFORE anything opens the game namespace or loads a creature into RAM. A
  // wipe asked for on the previous run finishes here, where there is no window
  // for the old creature to be written back over it. See save.h § RESET_NS.
  if ((resetArmed() && !resetRecover()) || !saveRecover()) {
    Serial.println("save: boot recovery failed; gameplay blocked. Keep your backup and retry after checking power.");
    while (true) delay(1000);
  }
  loadLang();  // idioma guardado (ES por defecto)
  brightLoad();  // brillo guardado (por defecto: el de siempre)
  levelSpeedLoad();     // minutos por nivel (por defecto: 60, el juego original)
  farewellLevelLoad();  // nivel de despedida (por defecto: 73, el juego original)
  eggPreferNewLoad();   // huevos: priorizar lineas nuevas (por defecto: si)
  stepCounterLoad();    // contador de pasos: activado por defecto ("stepen")
  Wire.begin(IIC_SDA, IIC_SCL);
  // CST9217 (tactil), AXP2101 (PMU) y PCF85063 (RTC) comparten este bus I2C.
  // Red de seguridad para PMU/RTC (SensorLib NO respeta este timeout en el
  // tactil; el cuelgue del tactil dormido se resuelve gateando por INT, ver
  // handleTouch).
  Wire.setTimeOut(50);

  // CRITICO: encender la alimentacion del panel (BLDO1=OLED VDD 3.3V) ANTES de
  // inicializar el display. Si el PMU se reseteo (drenaje total), este rail
  // queda OFF y la pantalla se ve negra aunque el resto de la placa funcione.
  pmuEnablePanel();

  // QSPI a 80MHz (por defecto 40): el flush del framebuffer es el cuello de
  // botella del fps (~56ms a 40MHz). Si el panel mostrara basura, bajar a 40M.
  if (!gfx->begin(80000000)) Serial.println("gfx->begin() fallo");
  // Keep the panel dark with a cleared black framebuffer until loop() renders
  // the first real frame. PSRAM holds uninitialized data on boot, and setup()
  // still has touch retries, SD mount, and RTC catch-up to run; lighting the
  // panel here flashes garbage before the first frame appears. Setting
  // gBrightNow = 0 ensures updateBrightness() applies the player's saved level
  // right after the first frame is flushed.
  gfx->fillScreen(RGB565_BLACK);
  gfx->flush();
  gBrightNow = 0;
  panel->setBrightness(0);
  gFirstFrame = true;

  touch.setPins(TP_RESET, TP_INT);
  bool touchOk = false;
  for (int i = 0; i < 3 && !touchOk; i++) {  // a veces falla al primer intento
    touchOk = touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
    if (!touchOk) delay(150);
  }
  if (!touchOk) Serial.println("CST9217 no detectado");
  // begin() deja el chip en modo comando (lee la identidad y no sale);
  // hace falta un reset por hardware para que vuelva a reportar toques
  touch.reset();
  touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
  touch.setMirrorXY(true, true);  // el panel esta montado girado 180 grados
  // INT activo-bajo: salta cuando hay datos. Gatea las lecturas I2C (ver loop)
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), touchIsr, FALLING);

  party.begin();
  if (!party.box.data()) {
    Serial.println("save: box allocation failed; gameplay blocked. Check OPI PSRAM configuration.");
    while (true) delay(1000);
  }
  bag.begin();
  pet.begin();
  // AFTER pet.begin(), because it compares against the pet checkpoint's loaded
  // generation, and BEFORE syncClock() below, so offline progression is applied
  // to whichever creature the swap actually settled on.
  focusRecover();
  sdBegin();
  thumbs.load();

  // reloj real: aplica el tiempo que estuvo apagado
  rtcBegin();
  batBegin();
  pwrSetup();
  imuBegin();
  imuSetEnabled(gStepCounterEnabled);
  uint32_t e = rtcEpoch();
  if (e == 0) {
    // CLOCK_EPOCH_FLOOR, not a literal: the same number is what clockVerdict()
    // treats as the earliest real time, and two copies of it would let the seed
    // drift below the floor that judges it.
    rtcSetEpoch(CLOCK_EPOCH_FLOOR);  // RTC virgen: semilla (la hora absoluta da
    e = rtcEpoch();                  // igual, solo importan las diferencias)
    Serial.println("RTC sin hora: sembrado, sin progresion offline esta vez");
  }
  if (!focusBlocked) pet.syncClock(e);

  audioBegin();  // ES8311 + I2S + amplificador (suena un jingle de arranque)

  // After everything has loaded, so the figure reflects a real save rather than
  // an empty one. A full partition is erased WHOLE by the core on the next boot,
  // so this number trending down over a soak test is the only warning there is.
  nvsReport("boot");
  gNvsLow = nvsLowOnSpace();

  // Covers the SOFTWARE restarts -- ESP.restart() after an IMPORT, and the one
  // the serial console does. It does NOT cover the AXP2101's 4-second hardware
  // power-off, which tells the firmware nothing at all; that is what the
  // long-press flush in loop() is for.
  esp_register_shutdown_handler([]() { flushBeforePowerLoss("shutdown"); });

  lastInteract = millis();
}

// A sprite pack is streaming in over USB. The game stands aside -- no game frames,
// no taps, no per-file sprite reload or region rescan -- so the board spends its
// time on the card. Never over a fight or a minigame: their render paths pump the
// link and apply training, so those keep the old, slower behaviour.
bool transferMode() {
  return sdTransferAt && millis() - sdTransferAt < SD_TRANSFER_IDLE_MS &&
         !battleOpen && !lan.live() && !gameOpen && !sackOpen && !spdOpen && !memoOpen;
}

// carga/descarga el sprite de SD cuando cambia la especie
void ensureMon() {
  bool sdReload = sdDirty && !transferMode();
  if (pet.speciesId == monFor && monShinyFor == pet.shiny && !sdReload) return;
  sdDirty = false;
  // A NEW CREATURE IS ON THE PANEL: let it speak. This one hook covers hatching,
  // evolving and swapping the focused creature, because all three arrive here --
  // which is better than three call sites that could drift apart.
  //
  // monFor starts at -2, the "nothing loaded yet" sentinel, and that case is
  // deliberately silent: a cry on every boot is a device that shouts when you turn
  // it on. -1 is an egg, which has no voice either.
  if (monFor != -2 && pet.speciesId >= 1 && pet.speciesId != monFor)
    audioCry(pet.speciesId);
  monFor = pet.speciesId;
  monShinyFor = pet.shiny;
  mon.unload();
  pmd.unload();
  beh.x = beh.targetX = 233;
  beh.mode = 0;
  beh.until = 0;
  if (pet.speciesId >= 1 && pet.speciesId <= DEX_COUNT) {
    pmd.load(pet.speciesId, pet.shiny);          // principal: PMD
    if (!pmd.loaded) mon.load(pet.speciesId, pet.shiny);  // respaldo: B/N
  }
}

void loop() {
  if (focusBlocked) {
    focusRecover();
    render();
    gFirstFrame = false;
    updateBrightness(millis());
    delay(20);
    return;
  }
  uint32_t now = millis();
  pet.update(now);

  // The link is pumped here rather than from the LAN screen, because it has to
  // keep running through the battle too: linkNowPoll() drains what the radio
  // parked on the WiFi task, and tick() is what resends a lost packet and gives
  // up on a peer that has gone quiet.
  if (lan.live()) {
    linkNowPoll();
    lan.tick(now);
  }

  // avisa con un sonido cuando el bicho pasa a estar listo para evolucionar
  // (incluye el caso de cumplir al despertar). canEvolveNow es false durmiendo.
  static bool wasEvoReady = false;
  bool evoReady = pet.wantEvolveButton();
  if (evoReady && !wasEvoReady) sfxPlay(SFX_MEDAL);
  wasEvoReady = evoReady;
  // aviso sombrio cuando el bicho esta a punto de escaparse por abandono
  static bool wasRunReady = false;
  bool runReady = pet.canRunawayNow();
  if (runReady && !wasRunReady) sfxPlay(SFX_DENY);
  wasRunReady = runReady;

  handleTouch();
  handleSerial();
  ensureMon();

  // A sprite pack can arrive long after the card was mounted: the web installer
  // streams it over PUT into the FIRMWARE THAT IS ALREADY RUNNING. gRegionArt was
  // computed once in sdBegin(), so without this the region a player just spent ten
  // minutes downloading stayed greyed out reading NEEDS PACK until they rebooted --
  // indistinguishable, from the player's side, from the download having failed.
  // Quietly, because the host is still parsing this serial stream -- and once per
  // transfer rather than after every file, which cost 27 lookups in /mons each time.
  if (sdArtDirty && !transferMode()) {
    sdArtDirty = false;
    sdScanRegionArt(false);
  }
  // The same, for the gallery: thumbs.bin is read in setup(), so a copy that arrived
  // over PUT drew nothing new until a restart.
  if (sdThumbsDirty && !transferMode()) {
    sdThumbsDirty = false;
    thumbs.load();
  }

  // A farewell or release just finished: the creature is waiting for a slot.
  // With room it simply joins; with a full party the player is taken straight
  // to the party screen to choose who it replaces, or to let it go.
  if (pet.endedKind != CER_NONE && !partyPick && party.writable()) {
    // party first, then the box; only a full box makes it your choice
    if (!handoverStored) {
      if (party.firstFree() >= 0) handoverStored = party.add(pet.endedMon);
      else if (party.boxFirstFree() >= 0) handoverStored = party.boxAdd(pet.endedMon);
    }
    if (handoverStored) {
      PartyMon ended = pet.endedMon;
      if (clearHandover()) {
        snprintf(partyBannerName, sizeof(partyBannerName), "%s",
                 ended.nick[0] ? ended.nick : DEX_TBL[ended.dex].name);
        partyBannerUntil = now + 3500;
        sfxPlay(SFX_MEDAL);
      }
    } else if (party.saveHealthy() && party.writable() &&
               party.isFull() && party.boxFirstFree() < 0) {
      partyPick = true;
      partyOpen = true;
      menuOpen = false;
    }
  }

  // El PMU late sus interrupciones: se leen TODAS de una vez (pwrPoll) y los
  // accesores de abajo consumen lo que vio. Ver rtcbat.cpp: dos sondeos
  // independientes se borrarian los bits el uno al otro.
  static uint32_t lastPwr = 0;
  if (now - lastPwr > 250) {
    lastPwr = now;
    pwrPoll();
    if (pwrShortPressed()) {
      screenOff = !screenOff;
      pet.setScreenOff(screenOff);   // asleep only if it is also night
      if (!screenOff) lastInteract = now;
    }
    // The hold is on its way to the AXP2101's own power-off, and the firmware
    // gets no second warning. Flush NOW, ignoring the dim/off gate below: that
    // gate exists so a ~1 s flash write is never visible mid-animation, and a
    // visible stutter is a much smaller price than the last few minutes of a
    // creature's life. Same for the battery's low warning.
    if (pwrLongPressed()) flushBeforePowerLoss("pwr long press");
    if (batLowWarning()) flushBeforePowerLoss("battery low");
  }

  static StepDetector stepper;
  if (gStepCounterEnabled) {
    float ax, ay, az;
    if (imuReadAccel(ax, ay, az)) pet.addSteps(stepper.feed(ax, ay, az, now));
    uint8_t stepMs = pet.claimSteps();
    if (stepMs && !screenOff && !pet.sleeping)
      sfxPlay(stepMs >= STEP_MS_COUNT ? SFX_MEDAL : SFX_HEART);
  } else {
    stepper.reset();
  }

  // On the first pass after boot, flush the first real frame while the panel
  // is still dark so updateBrightness() lights straight onto rendered content.
  if (gFirstFrame) {
    gFirstFrame = false;
    lastRender = now;
    render();
  }

  updateBrightness(now);

  // Stop any jukebox loop when the screen goes dark or the companion sleeps,
  // and mirror audible playback onto Pet so it dances and gains listening joy.
  if (audioCurrentTrack() != 0xFF && (screenOff || pet.sleeping)) audioMusic(MUS_NONE);
  pet.musicOn = jukeAudible();

  // Battery at the safe limit (no USB): save the pet and power the PMU off before the cell
  // drops further; the RTC keeps the time. See batcurve.h.
  if (batShouldShutdown() && !usbPresentNow()) {
    flushBeforePowerLoss("battery cutoff");
    panel->setBrightness(0);
    batPowerOff();
  }

  // vuelca el autoguardado periodico SOLO con la pantalla atenuada/apagada o
  // durmiendo: la escritura a NVS congela ~1s ambos cores (caché de flash off),
  // y aqui no hay animacion que se corte ni dedo esperando respuesta. Con 90s
  // de inactividad la pantalla ya atenua, asi que se vuelca enseguida; el uso
  // activo persiste igual por los guardados de cada accion (comer/jugar/...).
  if (pet.savePending() && (screenOff || dimStage >= 1 || pet.sleeping)) {
    pet.flushSave();
  }

  // anota la hora real cada 30 s (se persiste en cada save del juego)
  static uint32_t lastClock = 0;
  if (now - lastClock > 30000) {
    lastClock = now;
    uint32_t e = rtcEpoch();
    if (e) pet.lastSeenEpoch = e;
  }

  // latido de salud cada 5 min (para el soak test; se descarta si no hay monitor)
  static uint32_t lastHealth = 0;
  if (now - lastHealth > 300000) {
    lastHealth = now;
    Serial.printf("HEALTH up=%lus heap=%u min=%u save=%s collection=%s pmu_err=%u\n",
                  (unsigned long)(now / 1000), ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), pet.saveHealthy() ? "ok" : "BROKEN",
                  party.saveHealthy() ? "ok" : "BROKEN",
                  (unsigned)pmuReadErrors());
    // NVS headroom rides the same heartbeat as the heap, and for the same
    // reason: the value on its own says little, the trend over a soak says
    // everything. See nvsinfo.cpp for what running out costs.
    nvsReport("health");
    gNvsLow = nvsLowOnSpace();
  }

  // 85 ms en juego/saco: margen seguro para que el redibujado no pise el envio
  // DMA del frame anterior (a 40-65 ms solapaba y causaba flashes negros; con
  // sprites grandes el dibujo tarda mas, asi que se deja colchon)
  //
  // WITH THE PANEL DARK THE CADENCE STRETCHES; IT DOES NOT STOP. Skipping
  // render() outright is the obvious optimisation and it is wrong, because this
  // sketch advances real state inside its render paths: renderBattle() pumps the
  // LAN turn (btlLinkPoll), renderGame/renderSack/renderSpeed are what APPLY a
  // finished session's training and close it, renderGallery consumes
  // galleryDirty, and four modal timers -- the food menu, the release confirm,
  // the choice dialog and the party banner -- only expire in render(). Returning
  // early would freeze a linked fight, silently discard a training session, and
  // strand a dialog open until something else redrew.
  //
  // So the frame still happens, just far less often, and anything with motion or
  // a peer waiting on it keeps its full rate.
  PowerCtx pc = {};
  pc.idleMs = idleSince(now, lastInteract);
  pc.screenOff = screenOff;
  pc.sleeping = pet.sleeping;
  pc.gameOpen = gameOpen || sackOpen || spdOpen || memoOpen || battleOpen;
  pc.usb = usbPresent();
  pc.charging = batCharging();
  pc.batPct = batPercent();
  pc.stepActive = gStepCounterEnabled && imuPresent();
  uint32_t period = frameIntervalMs(pc);
  if (screenOff && !battleOpen) period = 500;   // dark panel: the frame stretches, it does not stop (see above)
  if (period == 0) period = 500;
  // The one pause, and transferMode() already excludes the fights and minigames above.
  if (transferMode()) period = 1000;
  batSetPollMs(powerCacheMs(pc));
  if (now - lastRender >= period) {
    lastRender = now;
    render();
  } else if (!transferMode() && !battleOpen && lightSleepAllowed(pc) && !usbPresentNow()) {
    // Battery only, screen dark or nearly: sleep the CPU between frames. A touch (TP_INT)
    // or the timer wakes it. Never on USB (the serial port would vanish) or mid-transfer.
    cpuLightSleep(lightSleepMs(pc));
  } else {
    delay(idleDelayMs(pc));
  }
}

// brillo segun sueno + inactividad (proteccion del AMOLED)
void updateBrightness(uint32_t now) {
  // los eventos visibles despiertan la pantalla solos
  if (pet.evolving() || pet.ceremony || pet.eating() || pet.showHeart()) {
    lastInteract = now;
  }
  // `now` was captured at the top of loop(), before handleTouch() stamped
  // lastInteract with a fresh millis() after the CST9217 I2C read. Unsigned
  // subtraction wrapped a few negative ms into ~4.29e9 -> dimStage 2
  // (brightness 8) on every press and release. Clamp negative deltas to 0.
  uint32_t idle = (int32_t)(now - lastInteract) > 0 ? (now - lastInteract) : 0;
  dimStage = (idle > 300000) ? 2 : (idle > 90000) ? 1 : 0;
  const uint8_t base = brightBase(gBrightLevel, usbPresent());
  uint8_t target = pet.sleeping ? 25 : base;
  if (dimStage == 1) target = pet.sleeping ? 10 : 60;
  else if (dimStage == 2) target = 8;
  // The setting is a CEILING. Without this a low level would still be overridden
  // upwards by the dim ladder's own numbers -- at level 1 the base is ~20 and
  // dimStage 1's flat 60 would make an idle screen BRIGHTER than an attended one.
  if (target > base) target = base;
  {
    PowerCtx bc = {};
    bc.usb = usbPresent(); bc.charging = batCharging(); bc.batPct = batPercent();
    if (batteryCritical(bc) && target > 60) target = 60;
    else if (batteryLow(bc) && target > 100) target = 100;
  }
  if (screenOff) target = 0;
  if (target != gBrightNow) {
    gBrightNow = target;
    panel->setBrightness(target);
  }
}

// ---------- consola serie (provision de SD + depuracion) ----------

// The values are what dex.h stores in `DexEntry::biome`, plus BIOME_GRAVEYARD
// for Ghost-type species (from crenberries428/TamaPoke, MIT).
enum Biome : uint8_t {
  BIOME_MEADOW, BIOME_BEACH, BIOME_FOREST, BIOME_VOLCANO, BIOME_MOUNTAIN, BIOME_SNOW, BIOME_GRAVEYARD,
  BIOME_COUNT
};

// Debug override set by the BIOME console command; -1 = follow the species.
int8_t gBiomeOverride = -1;

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  if (sdSerialCommand(line)) return;

  if (line == "HATCH") {
    pet.eggTap(); pet.eggTap(); pet.eggTap();
    Serial.println("DONE");
  } else if (line.startsWith("SPEC ")) {
    int n = line.substring(5).toInt();
    if (n >= 1 && n <= DEX_COUNT) {
      pet.prevSpeciesId = pet.speciesId;
      pet.speciesId = n;
      Serial.printf("especie #%d %s\n", n, DEX_TBL[n].name);
    }
    Serial.println("DONE");
  } else if (line.startsWith("BIOME ")) {
    int n = line.substring(6).toInt();
    if (n >= 0 && n < BIOME_COUNT) {
      gBiomeOverride = (int8_t)n;
      Serial.printf("biome %d\n", n);
    }
    Serial.println("DONE");
  } else if (line.startsWith("LVL ")) {
    // level() is 1 + age/rate, so the age for level N is (N-1) rates -- this
    // used to set N and hand back N+1, which is a poor thing for a command
    // named LVL to do when it is what every balance check is anchored on.
    long want = line.substring(4).toInt();
    if (want < 1) want = 1;
    if (want > MAX_LEVEL) want = MAX_LEVEL;
    pet.ageMinutes = (uint32_t)(want - 1) * MINUTES_PER_LEVEL;
    pet.saveNow();
    Serial.printf("lvl=%u\n", pet.level());
  } else if (line == "STEPS ON") {
    setStepCounter(true);
    Serial.println("steps_enabled=1");
  } else if (line == "STEPS OFF") {
    setStepCounter(false);
    Serial.println("steps_enabled=0");
  } else if (line.startsWith("STEPS ")) {
    long want = line.substring(6).toInt();
    if (want < 0) want = 0;
    if (want > STEP_MAX) want = STEP_MAX;
    pet.setStepsToday((uint32_t)want);
    pet.saveNow();
    Serial.printf("steps=%lu\n", (unsigned long)pet.stepsToday());
  } else if (line == "STEPS") {
    Serial.printf("steps=%lu enabled=%u claim=%u\n", (unsigned long)pet.stepsToday(),
                  gStepCounterEnabled ? 1 : 0, (unsigned)pet.stepClaim);
  } else if (line == "MAX" && gEmuDebug) {
    // MAX: the fully-raised creature, in one command. LVL 100 + IV 31 x4 + TR at
    // the ceiling + no care mistakes, which is otherwise four commands in the
    // right order -- TR has to come after IV, because trMaxFor() is what caps it.
    //
    // EMULATOR ONLY, gated on gEmuDebug. Not because it is dangerous but because
    // it is not earned: this is a game about raising something slowly, and a
    // one-word cheat on a board would be a way to skip the whole point of it by
    // accident. The board still compiles this; see the flag's note.
    //
    // Deliberately BEFORE any startsWith("MA...") branch would go, and an exact
    // match, so it cannot shadow a future command.
    pet.ivAtk = pet.ivDef = pet.ivSpe = pet.ivHp = 31;
    pet.trAtk = pet.trMaxAtk();
    pet.trDef = pet.trMaxDef();
    pet.trSpe = pet.trMaxSpe();
    pet.ageMinutes = (uint32_t)(MAX_LEVEL - 1) * MINUTES_PER_LEVEL;
    pet.careMistakes = 0;
    pet.relearnFromLevel();          // a level 100 creature knows level 100 moves
    pet.saveNow();
    Serial.printf("lvl=%u iv=31/31/31/31 tr=%u/%u/%u\n", pet.level(),
                  pet.trAtk, pet.trDef, pet.trSpe);
    Serial.println("DONE");
  } else if (line.startsWith("MISS ")) {
    // MISS <n>: sets the care mistakes -- "descuidos", the desc= on STATS.
    // Each one pushes every evolution threshold up a level, so a creature that
    // was neglected early evolves late; MISS 0 forgives that. Its sibling
    // commands are IV, TR and LVL.
    long m = line.substring(5).toInt();
    if (m < 0) m = 0;
    if (m > 255) m = 255;
    pet.careMistakes = (uint8_t)m;
    pet.saveNow();
    Serial.printf("desc=%u\n", pet.careMistakes);
  } else if (line.startsWith("TR ")) {
    // TR <atk> <def> <spe>: sets the TRAINING (this game's EVs), for testing a
    // fully-raised creature without playing the minigames for an hour. Each is
    // clamped to trMaxFor(iv), the same IV-bound ceiling the games enforce, so
    // this cannot produce a creature the player could not have raised.
    int v[3] = { 0, 0, 0 };
    int n = sscanf(line.c_str() + 3, "%d %d %d", &v[0], &v[1], &v[2]);
    if (n >= 1) {
      int a = v[0], d = (n >= 2) ? v[1] : v[0], e = (n >= 3) ? v[2] : v[0];
      pet.trAtk = (uint8_t)(a < 0 ? 0 : (a > pet.trMaxAtk() ? pet.trMaxAtk() : a));
      pet.trDef = (uint8_t)(d < 0 ? 0 : (d > pet.trMaxDef() ? pet.trMaxDef() : d));
      pet.trSpe = (uint8_t)(e < 0 ? 0 : (e > pet.trMaxSpe() ? pet.trMaxSpe() : e));
      pet.saveNow();
    }
    Serial.printf("tr=%u/%u/%u topes=%u/%u/%u\n", pet.trAtk, pet.trDef, pet.trSpe,
                  pet.trMaxAtk(), pet.trMaxDef(), pet.trMaxSpe());
  } else if (line.startsWith("IV ")) {
    // IV <fue> <def> <vel> <vit>: fija los valores individuales (pruebas).
    // Con "IV 31 31 31 31" se ve el techo; con "IV 8 8 8 8" el suelo.
    int v[4] = { 16, 16, 16, 16 };
    int n = sscanf(line.c_str() + 3, "%d %d %d %d", &v[0], &v[1], &v[2], &v[3]);
    if (n >= 1) {
      for (int i = 0; i < 4; i++) v[i] = v[i] < 0 ? 0 : (v[i] > 31 ? 31 : v[i]);
      pet.ivAtk = v[0];
      pet.ivDef = (n >= 2) ? v[1] : v[0];
      pet.ivSpe = (n >= 3) ? v[2] : v[0];
      pet.ivHp = (n >= 4) ? v[3] : v[0];
      if (pet.trAtk > pet.trMaxAtk()) pet.trAtk = pet.trMaxAtk();
      if (pet.trDef > pet.trMaxDef()) pet.trDef = pet.trMaxDef();
      if (pet.trSpe > pet.trMaxSpe()) pet.trSpe = pet.trMaxSpe();
    }
    pet.saveNow();   // IV used to change RAM only and never write
    Serial.printf("iv=%u/%u/%u/%u topes=%u/%u/%u\n", pet.ivAtk, pet.ivDef,
                  pet.ivSpe, pet.ivHp, pet.trMaxAtk(), pet.trMaxDef(), pet.trMaxSpe());
    Serial.println("DONE");
  } else if (line.startsWith("TIME ")) {
    uint32_t e = (uint32_t)line.substring(5).toInt();
    rtcSetEpoch(e);
    pet.setClock(e);
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line.startsWith("RTCSET ")) {  // solo RTC (simular apagados en pruebas)
    rtcSetEpoch((uint32_t)line.substring(7).toInt());
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "TIME") {
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "GAL") {
    galleryOpen = !galleryOpen;
    galleryDetail = 0;
    galleryDirty = true;
    if (!galleryOpen) galleryPmd.unload();
    Serial.println("DONE");
  } else if (line == "EGGS") {
    // simula 20 tiradas de huevo (no cambia el estado del juego)
    for (int i = 0; i < 20; i++) {
      int16_t d = pet.pickEggSpecies();
      Serial.printf("%d:%s(r%u) ", d, DEX_TBL[d].name, DEX_TBL[d].rarity);
    }
    Serial.println();
    Serial.println("DONE");
  } else if (line == "BAG") {
    for (uint8_t i = 0; i < bag.distinctCount(); i++) {
      ItemKey k = bag.keyAt(i);
      Serial.printf("%u:%s x%u\n", k, itemEntry(k).name, bag.count(k));
    }
    Serial.println("DONE");
  } else if (line.startsWith("GIVE ")) {
    // GIVE <key> [n]: stock the bag, so catching can be exercised without
    // grinding wild wins first.
    int key = 0, n = 1;
    if (sscanf(line.c_str() + 5, "%d %d", &key, &n) >= 1 &&
        itemValid((ItemKey)key)) {
      uint8_t got = bag.add((ItemKey)key, (uint8_t)(n > 0 ? n : 1));
      Serial.printf("%s x%u (+%u)\n", ITEM_TBL[key].name,
                    bag.count((ItemKey)key), got);
    } else {
      Serial.println("bad item");
    }
    Serial.println("DONE");
  } else if (line == "WILD" || line == "WILD HARD") {
    bool hard = (line == "WILD HARD");
    if (startWildBattle(currentExploreRegion(), hard))
      Serial.printf("wild %s Lv.%u%s iv=%u/%u/%u/%u\n", DEX_TBL[wildDex].name,
                    wildLvl, wildShiny ? " *SHINY*" : "",
                    wildIv[0], wildIv[1], wildIv[2], wildIv[3]);
    else
      Serial.println("no encounter (egg, ceremony, or no sprite pack)");
    Serial.println("DONE");
  } else if (line.startsWith("FOCUS ")) {
    // FOCUS <slot>: swap the creature on the main screen with a party slot,
    // which is the console half of the MAKE ACTIVE button.
    int s = -1;
    if (sscanf(line.c_str() + 6, "%d", &s) == 1 && s >= 0 && s < PARTY_SLOTS &&
        !party.slots[s].empty()) {
      if (focusSwap((uint8_t)s))
        Serial.printf("raising %s Lv.%u\n", DEX_TBL[pet.speciesId].name, pet.level());
      else Serial.println("FOCUS SAVE FAIL");
    } else {
      Serial.println("empty or out of range");
    }
    Serial.println("DONE");
  } else if (line.startsWith("EGG ")) {
    // EGG <dex> [shiny]: hatch a chosen species right now. The legendary and
    // shiny IV guarantees only apply at hatch time, so this is the only way to
    // exercise them on hardware (SHINY below just toggles the flag afterwards).
    int dex = 0, sh = 0;
    int n = sscanf(line.c_str() + 4, "%d %d", &dex, &sh);
    if (n >= 1 && dex >= 1 && dex <= DEX_COUNT) {
      pet.dbgHatchAs(dex, sh != 0);
      Serial.printf("%s%s iv=%u/%u/%u/%u\n", DEX_TBL[dex].name,
                    pet.shiny ? " *SHINY*" : "", pet.ivAtk, pet.ivDef,
                    pet.ivSpe, pet.ivHp);
    }
    Serial.println("DONE");
  } else if (line.startsWith("BATTLE")) {
    // BATTLE <dex> [level] -- the only way in until the trainer roster exists
    int dex = 0, lvl = 0;
    int n = sscanf(line.c_str() + 6, "%d %d", &dex, &lvl);
    if (n >= 1 && dex >= 1 && dex <= DEX_COUNT) {
      startBattle(dex, lvl > 0 ? (uint8_t)lvl : pet.level());
      Serial.printf("battle vs %s Lv.%u\n", DEX_TBL[dex].name, btlFoe.level);
    } else {
      Serial.println("uso: BATTLE <dex> [nivel]");
    }
    Serial.println("DONE");
  } else if (line == "SHINY") {  // alterna shiny del actual (pruebas)
    pet.shiny = !pet.shiny;
    Serial.printf("shiny=%d\n", pet.shiny);
    Serial.println("DONE");
  } else if (line.startsWith("NICK ")) {
    pet.rename(line.substring(5).c_str());
    Serial.printf("nick=%s\n", pet.nick);
    Serial.println("DONE");
  } else if (line == "CAREDAY") {  // simula un dia nuevo cuidado (pruebas)
    pet.setClock(pet.lastSeenEpoch + 86400);
    pet.caress();
    Serial.printf("streak=%u bond=%u medals=0x%X\n", pet.streak, pet.bond, pet.medals);
    Serial.println("DONE");
  } else if (line == "BYE") {
    pet.startFarewell();
    Serial.println("DONE");
  } else if (line == "RUN") {
    pet.startRunaway();
    Serial.println("DONE");
  } else if (line == "BEEP") {
    sfxPlay(SFX_HATCH);  // prueba de audio
    Serial.println("DONE");
  } else if (line == "MUSIC") {
    for (uint8_t i = 0; i < audioTrackCount(); i++)
      Serial.printf("%u %s\n", i, audioTrackName(i));
    Serial.println("DONE");
  } else if (line == "MUSIC OFF") {
    audioMusic(MUS_NONE);
    Serial.println("DONE");
  } else if (line.startsWith("MUSIC ")) {
    const char *arg = line.c_str() + 6;
    int t = atoi(arg);
    if (*arg < '0' || *arg > '9' || t >= audioTrackCount()) { Serial.println("MUSIC ?"); return; }
    if (!audioEnabled()) Serial.println("sound is OFF in settings");
    audioPlayTrack((uint8_t)t);
    Serial.printf("playing %s\n", audioTrackName((uint8_t)t));
  } else if (line == "TUNES" || line == "TUNES ALL") {
    if (line == "TUNES ALL") { pet.tunes = (uint16_t)((1u << TUNE_COUNT) - 1); pet.saveNow(); }
    for (uint8_t t = 0; t < TUNE_COUNT; t++)
      Serial.printf("%u %s %s\n", t, audioTrackName(t), pet.tuneUnlocked(t) ? "OPEN" : "locked");
    Serial.println("DONE");
  } else if (line == "ABANDON") {
    pet.dbgRunawayReady();  // fuerza el estado "lista para escaparse" (test del boton)
    Serial.println("DONE");
  } else if (line == "EXPORT") {
    // Prints the whole save as a block of IMPORT commands. Pasting that block
    // back is the restore -- there is no separate format to get wrong, and no
    // single 2000-character line for a terminal to mangle.
    //
    // saveExport() reads the checkpoints only (see save.cpp) -- those are what
    // save() and persist() write on every change, so nothing needs forcing
    // fresh here first.
    uint8_t *buf = static_cast<uint8_t *>(saveAlloc(SAVE_TRANSFER_MAX));
    if (!buf) { Serial.println("EXPORT FAIL"); return; }
    size_t n = saveExport(buf, SAVE_TRANSFER_MAX);
    if (!n) { free(buf); Serial.println("EXPORT FAIL"); return; }
    Serial.printf("# TamaPoke save, %u bytes. Paste this whole block back.\n",
                  (unsigned)n);
    for (size_t i = 0; i < n; i += 48) {
      Serial.print("IMPORT ");
      for (size_t j = i; j < i + 48 && j < n; j++) Serial.printf("%02X", buf[j]);
      Serial.println();
    }
    Serial.println("IMPORT");        // the empty one commits
    free(buf);
  } else if (line.startsWith("IMPORT")) {
    // IMPORT <hex>   append a chunk
    // IMPORT         commit what has been appended
    static uint8_t *in = nullptr;
    static size_t inN = 0;
    auto resetImport = [&]() { free(in); in = nullptr; inN = 0; };
    String hex = line.substring(6);
    hex.trim();
    if (hex.length()) {
      if (hex.length() & 1) { Serial.println("IMPORT ODD"); resetImport(); return; }
      if (!in) in = static_cast<uint8_t *>(saveAlloc(SAVE_TRANSFER_MAX));
      if (!in) { Serial.println("IMPORT FULL"); resetImport(); return; }
      for (size_t i = 0; i + 1 < (size_t)hex.length(); i += 2) {
        if (inN >= SAVE_TRANSFER_MAX) { Serial.println("IMPORT FULL"); resetImport(); return; }
        auto nyb = [](char c) -> int {
          if (c >= '0' && c <= '9') return c - '0';
          if (c >= 'A' && c <= 'F') return c - 'A' + 10;
          if (c >= 'a' && c <= 'f') return c - 'a' + 10;
          return -1;
        };
        const char *hs = hex.c_str();
        int hi = nyb(hs[i]), lo = nyb(hs[i + 1]);
        if (hi < 0 || lo < 0) { Serial.println("IMPORT BAD"); resetImport(); return; }
        in[inN++] = (uint8_t)((hi << 4) | lo);
      }
      Serial.println("IMPORT MORE");
      return;                        // wait for the next chunk or commit
    }
    if (!inN) { resetImport(); Serial.println("IMPORT EMPTY"); return; }
    bool ok = saveImport(in, inN);
    Serial.println(ok ? "IMPORT OK" : "IMPORT REJECTED");
    resetImport();
    if (!ok && saveRestorePending()) {
      saveInhibited = true;
      delay(100);
      ESP.restart();
      return;
    }
    if (ok) { Serial.println("DONE"); delay(100); ESP.restart(); }
  } else if (line == "WIPE") {
    // Set BEFORE the restart, because the restart runs the shutdown handler, and
    // that handler's whole job is to flush a pending save. Without this it would
    // write the creature that was just deleted straight back into the empty NVS.
    saveInhibited = true;
    if (!pet.factoryReset()) {
      Serial.println("WIPE FAIL");
      if (resetArmed()) { delay(100); ESP.restart(); }
      else saveInhibited = false;
      return;
    }
    Serial.println("DONE");
    delay(100);
    ESP.restart();
  } else if (line.startsWith("PARTY")) {
    // PARTY          list the party
    // PARTY <dex>    bank a level-50 specimen (fills slots up for testing)
    // PARTY CLEAR    empty it
    String arg = line.substring(5);
    arg.trim();
    if (arg == "CLEAR") {
      for (int i = 0; i < PARTY_SLOTS; i++)
        if (!party.releaseAt(i)) { Serial.println("PARTY SAVE FAIL"); return; }
    } else if (arg.length()) {
      int d = arg.toInt();
      if (d >= 1 && d <= DEX_COUNT) {
        PartyMon m;
        m.dex = d;
        m.level = 50;
        m.ivAtk = m.ivDef = m.ivSpe = m.ivHp = 20;
        m.trAtk = m.trDef = m.trSpe = 50;
        Serial.println(party.add(m) ? "added" :
                       (party.saveHealthy() && party.writable() ? "party full" : "PARTY SAVE FAIL"));
      }
    }
    Serial.printf("party %u/%u:", party.count(), PARTY_SLOTS);
    for (int i = 0; i < PARTY_SLOTS; i++) {
      const PartyMon &m = party.slots[i];
      if (m.empty()) Serial.print(" -");
      else Serial.printf(" %s%s(nv%u)", DEX_TBL[m.dex].name, m.shiny ? "*" : "", m.level);
    }
    Serial.println();
    Serial.println("DONE");
  } else if (line == "REG") {
    Serial.printf("pokedex %u/%u:", pet.registeredCount(), DEX_COUNT);
    for (int i = 1; i <= DEX_COUNT; i++)
      if (pet.isRegistered(i)) Serial.printf(" %d", i);
    Serial.println();
    Serial.println("DONE");
  } else if (line == "HEALTH") {
    Serial.printf("up=%lus heap=%u min=%u sd=%d mon=%d\n",
                  (unsigned long)(millis() / 1000), ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), sdReady, pmd.loaded || mon.loaded);
    // PSRAM is where the sprites and the framebuffer live, so a memory problem
    // shows up here long before it shows up in the heap figure above.
    Serial.printf("psram=%u screen=%s btlspr=%d/%d\n", (unsigned)ESP.getFreePsram(),
                  SCREEN_NAME[uiCurrentScreen() % SCR_COUNT],
                  btlPmd[0].loaded ? 1 : 0, btlPmd[1].loaded ? 1 : 0);
    // The same two lines the periodic heartbeat prints. Typed on a board is when
    // you actually want them, and a console HEALTH that reported LESS than the
    // automatic one is § "A rule enforced in one path but not in its twin".
    Serial.printf("save=%s collection=%s pending=%d pmu_err=%u\n",
                  pet.saveHealthy() ? "ok" : "BROKEN",
                  party.saveHealthy() ? "ok" : "BROKEN",
                  pet.savePending() ? 1 : 0,
                  (unsigned)pmuReadErrors());
    nvsReport("health");
    gNvsLow = nvsLowOnSpace();
    Serial.println("DONE");
  } else if (line == "STATS") {
    // A stable per-board label, for the web installer's backup history so two
    // devices' saves can be told apart. The efuse MAC is fixed in silicon and
    // needs no storage. It is NOT a secret and NOTHING is authorised by it --
    // which is why the installer keeps its history in the browser rather than
    // on a server keyed by this.
    Serial.printf("board=%012llX\n", ESP.getEfuseMac());
    // Its own line: the installer matches board= as a whole line, and older pages must still.
    Serial.printf("fw=%s\n", FW_VERSION);
    Serial.printf("spec=%d nv=%u com=%u fel=%u ene=%u lim=%u desc=%u sd=%d mon=%d bat=%d usb=%d rtc=%u\n",
                  pet.speciesId, pet.level(), pet.fullness, pet.joy, pet.energy,
                  pet.hygiene, pet.careMistakes, sdReady, mon.loaded,
                  batPercent(), usbPresent(), rtcEpoch());
    Serial.printf("peso=%u fue=%u def=%u vel=%u vit=%u baya=%d\n",
                  pet.weight, pet.atkStat(), pet.defStat(), pet.speStat(),
                  pet.vitStat(), pet.berryKnown);
    Serial.printf("iv=%u/%u/%u/%u tr=%u/%u/%u topes=%u/%u/%u\n",
                  pet.ivAtk, pet.ivDef, pet.ivSpe, pet.ivHp,
                  pet.trAtk, pet.trDef, pet.trSpe,
                  pet.trMaxAtk(), pet.trMaxDef(), pet.trMaxSpe());
    Serial.printf("shiny=%d streak=%u/%u bond=%u medals=0x%X(%u) nick=%s\n",
                  pet.shiny, pet.streak, pet.bestStreak, pet.bond, pet.medals,
                  pet.totalMedals, pet.nick);
    Serial.println("DONE");
  }
}

// ---------- entrada tactil ----------

bool inPetZone(int16_t x, int16_t y) {
  return x > 110 && x < 356 && y > 95 && y < 310;
}

// el toque se resuelve al LEVANTAR el dedo para distinguir tap de deslizar
void handleTouch() {
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 20) return;  // 50 Hz le sobra a un dedo
  lastPoll = millis();
  // solo tocamos el bus si el chip aviso por INT o si el dedo sigue abajo (hay
  // que detectar el levantamiento). Leer el CST9217 dormido se colgaba ~1s y
  // congelaba el loop entero; SensorLib no respeta el timeout de Wire.
  if (!gTouchIrq && !wasPressed) return;
  gTouchIrq = false;
  int16_t x, y;
  bool pressed = touch.getPoint(&x, &y, 1) > 0;

  // saco de entrenamiento: cada toque cuenta al instante (aporrear rapido)
  if (sackOpen) {
    if (pressed && !wasPressed) {
      lastInteract = millis();
      if (y < 72) leaveSack();       // tocar arriba = salir, conservando lo ganado
      else sackTap();
    }
    wasPressed = pressed;
    return;
  }

  if (pressed && !wasPressed) {  // empieza el gesto
    tX0 = tXl = x;
    tY0 = tYl = y;
    tStart = millis();
    holdFired = false;
    // A drag that STARTS on the rim is a scroll, not a swipe. Deciding it at
    // touch-down rather than on release is what keeps it unambiguous: the
    // finger never has to be told afterwards what it meant.
    rimActive = uiOnRim(x, y) && uiRimPages() > 1;
    rimA0 = uiRimAngle(x, y);
    rimSteps = 0;
    rimMoved = false;
    swallowGesture = (dimStage > 0) || screenOff || transferMode();  // waking or mid-transfer: no action
    if (screenOff) pet.setScreenOff(false);        // waking the screen wakes it
    screenOff = false;
    lastInteract = millis();
  } else if (pressed) {  // sigue apoyado
    tXl = x;
    tYl = y;
    if (rimActive && !swallowGesture) {
      float d = uiRimAngle(x, y) - rimA0;
      while (d > 180.0f) d -= 360.0f;
      while (d < -180.0f) d += 360.0f;
      if (d > 6.0f || d < -6.0f) rimMoved = true;
      int want = (int)(d / RIM_STEP);
      while (rimSteps < want) { onRim(1); rimSteps++; }
      while (rimSteps > want) { onRim(-1); rimSteps--; }
    }
    // pulsacion larga sin moverse sobre el bicho -> dialogo de soltar
    //
    // Gated on the MAIN screen, not on a hand-maintained list of screens to
    // exclude. That list had gallery/card/keyboard/clock on it and nothing
    // else, so the hold still fired on the party, box, gym, battle, player and
    // menu screens -- every one of which draws something inside inPetZone.
    // On the party screen it was genuinely dangerous: the grid overlaps the
    // zone, so holding a party slot opened "release the live pet?", and that
    // dialog's YES box sits on top of party slot 4. Hold a slot, tap where you
    // think a creature is, lose the creature you are actually raising.
    // One question with one answer, so a new screen cannot be forgotten.
    if (!holdFired && !swallowGesture && !rimActive && millis() - tStart >= 1000 &&
        abs(tXl - tX0) < 30 && abs(tYl - tY0) < 30 && boxHubHold(tX0, tY0))
      holdFired = true;
    // There used to be a 3 s hold on the creature here that opened the release
    // dialog. It is gone (issue #24): holding a pet you are petting is too easy
    // to do by accident, and the same action lives in MENU -> RETIRE.
  } else if (wasPressed) {  // levanta el dedo: resolver gesto
    lastInteract = millis();
    int dx = tXl - tX0, dy = tYl - tY0;
    uint32_t dt = millis() - tStart;
    if (rimMoved) { rimActive = false; wasPressed = pressed; return; }   // it was a scroll
    if (!holdFired && !swallowGesture) {
      if (abs(dx) > 80 && abs(dy) < 70 && dt < 800) onSwipe(dx > 0 ? 1 : -1);
      else if (abs(dy) > 80 && abs(dx) < 70 && dt < 800) onSwipeV(dy > 0 ? 1 : -1);
      else if (dt < 1500 && abs(dx) < 40 && abs(dy) < 40) onTap(tX0, tY0);
    }
    rimActive = false;
  }
  wasPressed = pressed;
}

// deslizar vertical: DOWN is back, UP is deeper.
//
// One meaning per direction, on every screen. It used to be a per-screen
// grab-bag -- vertical closed some screens, changed region on others, and on
// the main screen down opened the player card while up opened the creature's.
// The player card is a tile now, so down is free to mean the one thing a round
// UI most needs and never had: a back gesture that works everywhere.

void openClock();  // prototipo

void onSwipeV(int dir) {
  if (pet.awaitingStarter()) return;  // bloqueado durante la eleccion de inicial
  if (menuOpen) { menuOpen = false; return; }   // any swipe closes the menu
  if (battleOpen) return;   // no swiping out of a fight
  const bool back = dir > 0;

  // Modals and sheets: down backs out, up has nowhere deeper to go.
  if (pickOpen)     { if (back) pickOpen = false; return; }
  if (lanOpen)      { if (back) { lanLeave(); lanOpen = false; } return; }
  if (trainOpen)    { if (back) trainOpen = false; return; }
  if (bagOpen)      { if (back) bagOpen = false; return; }
  if (evoPickOpen)  { if (back) evoPickOpen = false; return; }
  if (movePickOpen) { if (back) movePickOpen = false; return; }
  if (boxOpen) {
    if (back) {
      if (boxJumpOpen) boxJumpOpen = false;
      else if (boxDetail) { boxDetail = 0; releaseConfirm = false; }
      else { boxOpen = false; boxSel = 0; }
    }
    return;
  }
  // Either minigame exits on a swipe, keeping what was earned. A swipe cannot
  // be confused with a ball hit -- the gesture resolver separates them -- which
  // the header tap no longer could once the ball became hittable up there.
  if (gameOpen) { leaveGame(); return; }
  if (sackOpen) { leaveSack(); return; }
  if (spdOpen)  { leaveSpeed(); return; }
  if (memoOpen) { leaveMemo(); return; }
  if (jukeOpen) { if (back) jukeOpen = false; return; }
  if (kbOpen || pet.ceremony) return;
  if (clockOpen) {
    // The confirm is a level of depth: down dismisses it rather than leaving the
    // screen out from under it. A swipe must never be able to CONFIRM a wipe,
    // only decline one.
    if (resetConfirm) { if (back) resetConfirm = false; return; }
    if (back) clockOpen = false;
    return;
  }

  if (exploreOpen) { if (back) uiTileGo(TILE_PET); return; }

  // The two multi-region tiles are two levels deep: chooser, then ladder/grid.
  // DOWN walks back out of them one step at a time; UP keeps the region
  // shortcut, which is now a convenience rather than the only way in.
  if (gymOpen) {
    if (gymPick) { if (back) uiTileGo(TILE_PET); return; }
    if (back) { gymPick = true; rpickPage = 0; sfxPlay(SFX_TAP); return; }
    gymRegion = (uint8_t)((gymRegion + 1) % GYM_REGIONS);
    gymPage = 0;
    sfxPlay(SFX_TAP);
    return;
  }
  if (galleryOpen) {
    if (galleryDetail) {
      if (back) { galleryDetail = 0; galleryPmd.unload(); galleryDirty = true; }
      return;
    }
    if (galleryPick) { if (back) uiTileGo(TILE_PET); return; }
    if (back) {
      galleryPick = true; rpickPage = 0;
      galleryPmd.unload();
      galleryDirty = true;
      sfxPlay(SFX_TAP);
      return;
    }
    galleryRegion = (uint8_t)((galleryRegion + 1) % GAL_REGIONS);
    galleryPage = 0;
    galleryClampPage();
    galleryDirty = true;
    sfxPlay(SFX_TAP);
    return;
  }
  if (playerOpen) { if (back) uiTileGo(TILE_PET); return; }
  if (medalsOpen) { if (back) uiTileGo(TILE_PET); return; }
  if (partyOpen) {
    if (partyDetail) { if (back) partyDetail = 0; return; }
    if (back) {
      if (partyPick) {
        if (!clearHandover()) { sfxPlay(SFX_DENY); return; }
        partyPick = false;
      }
      uiTileGo(TILE_PET);
    }
    return;
  }
  if (cardOpen) { if (back) cardOpen = false; return; }

  // The pet tile itself. UP is its detail sheet; DOWN is already home.
  if (!back && !pet.isEgg() && !confirmUntil && !feedMenuUntil) {
    cardOpen = true;
    cardPage = 0;
  }
}

// party screen: pick a slot (when a newcomer is waiting) or just leave
// A banked creature's sheet: its moves above all, since typing alone does not
// tell you whether that Lapras still has ICE BEAM -- and in hard mode that is
// what decides the fight.
// The detail sheet, shared by the party and the box so the two cannot drift.
// `fromBox` picks which action the LEFT button offers; the right one is always
// RELEASE, which is irreversible and therefore always asks first.
void renderMonSheet(const PartyMon &m, bool fromBox) {
  const DexEntry &d = DEX_TBL[m.dex];
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  char head[36];
  snprintf(head, sizeof(head), "%s%s Lv.%u", m.shiny ? "*" : "",
           m.nick[0] ? m.nick : d.name, (unsigned)m.level);
  gfx->setTextColor(d.accent);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(head) * 6, 40);
  gfx->print(head);
  char ty[24];
  if (d.type2 == T_NONE) snprintf(ty, sizeof(ty), "%s", typeName(d.type1));
  else snprintf(ty, sizeof(ty), "%s/%s", typeName(d.type1), typeName(d.type2));
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(ty) * 3, 64);
  gfx->print(ty);

  for (int i = 0; i < MOVE_SLOTS; i++)
    drawMoveRow(78 + i * 52, m.moves[i], false, m.dex);

  char st[40];
  snprintf(st, sizeof(st), "ATK %u  DEF %u  SPD %u  HP %u",
           party.atkOf(m), party.defOf(m), party.speOf(m), party.vitOf(m));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(st) * 3, 300);
  gfx->print(st);

  // Which creature you are RAISING is now a choice you can make at any time.
  // It is a true exchange -- the one on the main screen takes the slot this one
  // vacates -- so unlike the old frozen BRING BACK it needs no free slot and no
  // "only while an egg waits" rule. Both keep their full care state, so nothing
  // is reset by swapping and swapping back.
  //
  // A box creature still goes to the party first: one rule per screen, and the
  // swap is then offered from the party sheet like any other member.
  bool leftOk = fromBox || (pet.ceremony == CER_NONE && !pet.awaitingStarter());
  const char *leftLbl = fromBox ? T(S_BOX_TAKE) : T(S_FOCUS);
  const char *rightLbl = fromBox ? T(S_RELEASE_BTN) : T(S_BOX_PUT);
  gfx->fillRoundRect(PDET_L_X, PDET_BTN_Y, PDET_L_W, PDET_BTN_H, 10,
                     leftOk ? UI_BAR_OK : UI_TRACK);
  gfx->drawRoundRect(PDET_L_X, PDET_BTN_Y, PDET_L_W, PDET_BTN_H, 10, UI_INK);
  gfx->setTextColor(leftOk ? UI_BG_DAY : 0x8410);
  int leftSize = strlen(leftLbl) * 12 <= PDET_L_W - 8 ? 2 : 1;
  gfx->setTextSize(leftSize);
  gfx->setCursor(PDET_L_X + PDET_L_W / 2 - (int)strlen(leftLbl) * 3 * leftSize,
                 PDET_BTN_Y + PDET_BTN_H / 2 - 4 * leftSize);
  gfx->print(leftLbl);

  gfx->fillRoundRect(PDET_R_X, PDET_BTN_Y, PDET_R_W, PDET_BTN_H, 10, UI_TRACK);
  gfx->drawRoundRect(PDET_R_X, PDET_BTN_Y, PDET_R_W, PDET_BTN_H, 10,
                     fromBox ? UI_BAR_BAD : UI_INK);
  gfx->setTextColor(UI_INK);
  int rightSize = strlen(rightLbl) * 12 <= PDET_R_W - 8 ? 2 : 1;
  gfx->setTextSize(rightSize);
  gfx->setCursor(PDET_R_X + PDET_R_W / 2 - (int)strlen(rightLbl) * 3 * rightSize,
                 PDET_BTN_Y + PDET_BTN_H / 2 - 4 * rightSize);
  gfx->print(rightLbl);

  if (!leftOk) {
    const char *why = fromBox ? T(S_PARTY_FULL) : T(S_FOCUS_NOW);
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)strlen(why) * 3, PDET_BTN_Y - 14);
    gfx->print(why);
  }
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 404);
  gfx->print(T(S_BACK));

  // Asked before it happens, because nothing gets this creature back: it is not
  // a farewell, it does not join anything, and there is no undo.
  if (releaseConfirm) {
    char q[40];
    snprintf(q, sizeof(q), T(S_RELEASE_FMT), m.nick[0] ? m.nick : d.name);
    drawConfirmPanel(q, T(S_RELEASE_GONE), nullptr, UI_BAR_BAD,
                     T(S_RELEASE_BTN), UI_BAR_BAD, UI_WHITE,
                     T(S_NO), UI_TRACK, UI_INK);
  }
  gfx->flush();
}

void renderPartyDetail() {
  // An empty slot means the creature was just let go. Fall through to the grid
  // rather than returning: a bare return draws nothing AND never flushes, which
  // leaves the previous frame frozen on the panel -- the exact failure
  // flush_test exists to catch, and invisible to any screenshot.
  if (party.slots[partyDetail - 1].empty()) {
    partyDetail = 0;
    releaseConfirm = false;
    renderParty();
    return;
  }
  renderMonSheet(party.slots[partyDetail - 1], false);
}

void renderBoxDetail() {
  if (!party.box.data() || boxDetail > BOX_SLOTS || party.box[boxDetail - 1].empty()) {
    boxDetail = 0;
    releaseConfirm = false;
    renderBox();
    return;
  }
  renderMonSheet(party.box[boxDetail - 1], true);
}

// The two buttons' hit areas, so a test can prove they do not overlap without
// copying the geometry -- a test that restates the numbers drifts from them.
// Every primary button's height, so a test can hold them all to UI_TAP_MIN
// instead of waiting for somebody to report the next one by hand.
void uiButtonHeights(int *out, int max, int *n) {
  // What a control is worth is what you can HIT, not what it draws: the home
  // icons are a 72 px graphic inside a 94 px disc.
  const int h[] = { PSLOT_HUB_R * 2, PSLOT_R * 2, PARTYCLOSE_H, LANBTN_H,
                    BTL_CELL_H + BTL_HIT_PAD * 2,
                    EXPLORE_BTN_H + EXPLORE_BTN_PAD * 2, BTN_HIT * 2 };
  int c = (int)(sizeof(h) / sizeof(h[0]));
  if (c > max) c = max;
  for (int i = 0; i < c; i++) out[i] = h[i];
  if (n) *n = c;
}

// FULL-WIDTH LIST ROWS, which are a different class from the discs and pills
// above and have to be judged differently.
//
// A row spans the whole panel, so at 44 px tall it is still a ~276 x 44 target --
// far easier to hit than a 44 px button, and nothing about it is ambiguous
// because there is nothing beside it to hit instead. They also CANNOT reach
// UI_TAP_FINGER where they live: five settings rows at 94 px would need 520 px on
// a 466 px panel, and four training rows would need 416 before the header. So
// they are held to the hard 44 px floor and not to the finger floor.
//
// Published so that floor is enforced rather than assumed -- the training menu's
// pitch was just retuned to fit a fourth row, which is exactly the change that
// could have quietly dropped it under.
void uiRowHeights(int *out, int max, int *n) {
  // The type quiz's three answers are rows too: 240 px wide, stacked, nothing
  // beside them to hit instead.
  const int h[] = { MENU_ROW_H, TRAIN_ROW_H, SET_ROW_H, GOAL_ROW_H, GYM_ROW_H, QUIZ_OPT_H, JUKE_ROW_H };
  int c = (int)(sizeof(h) / sizeof(h[0]));
  if (c > max) c = max;
  for (int i = 0; i < c; i++) out[i] = h[i];
  if (n) *n = c;
}

// The gym list's difficulty pill against its first leader row. Enlarging the
// pill to a real tap target once pushed it straight over that row -- the third
// overlap of this kind, after BOX/CLOSE and the battle grid against BACK.
// Which home icon is the only one live while the pet sleeps, and where it is.
// Exposed so a test can prove it is the LIGHT: removing an icon once shifted
// every index and quietly made the BATH button the wake-up button.
// Asleep, the LIGHT is the only live icon -- it is what wakes the pet. Both the
// draw path and the tap path ask THIS, so a greyed button can never still be
// tappable and the greying can never point at the wrong icon.
bool uiButtonDisabled(int i) { return pet.sleeping && i != BTN_LIGHT; }

int uiSleepButton(int *cx, int *cy) {
  if (cx) *cx = buttons[BTN_LIGHT].cx;
  if (cy) *cy = buttons[BTN_LIGHT].cy;
  return BTN_LIGHT;
}

void uiButtonAt(int i, int *cx, int *cy, int *half) {
  if (i < 0 || i >= BTN_COUNT) return;
  if (cx) *cx = buttons[i].cx;
  if (cy) *cy = buttons[i].cy;
  if (half) *half = BTN_HALF;
}

void gymHeaderRects(int *pillTop, int *pillBot, int *rowTop) {
  if (pillTop) *pillTop = GYMDIF_Y;
  if (pillBot) *pillBot = GYMDIF_Y + GYMDIF_H;
  if (rowTop) *rowTop = GYM_ROW_Y(0);
}

void partyButtonRects(int *boxTop, int *boxBot, int *closeTop, int *closeBot) {
  if (boxTop) *boxTop = PSLOT_CY - PSLOT_HUB_R;
  if (boxBot) *boxBot = PSLOT_CY + PSLOT_HUB_R;
  if (closeTop) *closeTop = PARTYCLOSE_Y;
  if (closeBot) *closeBot = PARTYCLOSE_Y + PARTYCLOSE_H;
}

// Which of the detail sheet's two buttons a tap landed on. One answer for the
// party and the box, off the same PDET_* constants the sheet is DRAWN with, so
// the graphic and the hit area cannot drift apart.
bool monSheetBtn(int16_t x, int16_t y, bool left) {
  if (y < PDET_BTN_Y || y > PDET_BTN_Y + PDET_BTN_H) return false;
  int x0 = left ? PDET_L_X : PDET_R_X;
  int w = left ? PDET_L_W : PDET_R_W;
  return x >= x0 && x <= x0 + w;
}

// YES / NO on the release confirm. Returns true when the tap was consumed. The
// caller swallows everything else while it is up: a modal that leaks a miss
// through to what is drawn underneath is precisely how an irreversible action
// gets triggered by a fumbled tap, which is the shape CLAUDE.md section 4 keeps
// warning about.
bool monSheetConfirmTap(int16_t x, int16_t y, bool fromBox) {
  int c1t, c1b, c2t, c2b;
  uiConfirmRects(&c1t, &c1b, &c2t, &c2b);
  if (x < CONFIRM_BTN_X || x > CONFIRM_BTN_X + CONFIRM_BTN_W) return false;
  if (y >= c1t && y <= c1b) {            // YES -- and it does not come back
    if (!(fromBox ? party.boxReleaseAt(boxDetail - 1) :
                   party.releaseAt(partyDetail - 1))) { sfxPlay(SFX_DENY); return true; }
    if (fromBox) boxDetail = 0;
    else partyDetail = 0;
    // A half-finished swap named a slot that may now be empty, so disarm both
    // sides rather than leaving one pointing at a creature that is gone.
    boxSwapFrom = 0;
    boxSel = 0;
    releaseConfirm = false;
    sfxPlay(SFX_BYE);
    return true;
  }
  if (y >= c2t && y <= c2b) {            // NO
    releaseConfirm = false;
    sfxPlay(SFX_TAP);
    return true;
  }
  return false;
}

// The six party/box slots on a RING rather than a 2x3 grid; the geometry is
// declared with the other party layout constants above.
void partySlotPos(int i, int *cx, int *cy) {
  float a = (-120.0f + 60.0f * (float)i) * 0.01745329f;   // clockwise from top-left
  if (cx) *cx = CX + (int)(cosf(a) * PSLOT_RING);
  if (cy) *cy = PSLOT_CY + (int)(sinf(a) * PSLOT_RING);
}

// Which slot a tap landed in, or -1. The draw path and both tap paths ask this
// one function; the grid they replace had its rectangle written out in three
// places, which is how the party screen's controls drifted apart before.
int partySlotAt(int16_t x, int16_t y) {
  for (int i = 0; i < PARTY_SLOTS; i++) {
    int sx, sy;
    partySlotPos(i, &sx, &sy);
    int dx = x - sx, dy = y - sy;
    if (dx * dx + dy * dy <= PSLOT_R * PSLOT_R) return i;
  }
  return -1;
}

bool partyHubAt(int16_t x, int16_t y) {
  int dx = x - CX, dy = y - PSLOT_CY;
  return dx * dx + dy * dy <= PSLOT_HUB_R * PSLOT_HUB_R;
}

// Exposed so hit_test can hold every control to the finger floor without
// keeping its own copy of the number it is enforcing.
int uiTapFinger() { return UI_TAP_FINGER; }

// The home icons' TAP radius, which is larger than the graphic they draw.
int uiButtonHitR() { return BTN_HIT; }

bool clearHandover() {
  const PartyMon ended = pet.endedMon;
  const uint8_t kind = pet.endedKind;
  const uint32_t gen = pet.petGeneration();
  pet.clearEnded();
  if (kind != CER_NONE && pet.petGeneration() == gen) {
    pet.endedMon = ended;
    pet.endedKind = kind;
    return false;
  }
  handoverStored = false;
  return true;
}

void partyTap(int16_t x, int16_t y) {
  if (boxOpen) { boxTap(x, y); return; }
  // The SHEET IS CHECKED FIRST, and that is not cosmetic ordering. It covers the
  // whole screen, and its action row overlaps the BOX button's hit area below.
  // BOX button's hit area (x138..328, y312..372) below -- so with BOX tested
  // first, tapping RELEASE opened the box instead. Nothing behind a full-screen
  // sheet may answer a tap.
  if (partyDetail) {
    // The confirm is modal: while it is up nothing else on the sheet responds,
    // or a miss on YES would fall through to the move rows underneath it.
    if (releaseConfirm) { monSheetConfirmTap(x, y, false); return; }
    if (monSheetBtn(x, y, true)) {           // MAKE ACTIVE
      if (pet.ceremony != CER_NONE || pet.awaitingStarter()) { sfxPlay(SFX_DENY); return; }
      if (!focusSwap((uint8_t)(partyDetail - 1))) { sfxPlay(SFX_DENY); return; }
      partyDetail = 0;
      boxSwapFrom = 0;
      partyOpen = false;
      sfxPlay(SFX_HATCH);
      return;
    }
    if (monSheetBtn(x, y, false)) {          // TO BOX
      partyDetail = 0;
      boxOpen = true;
      boxPage = 0;
      sfxPlay(SFX_TAP);
      return;
    }
    for (int i = 0; i < MOVE_SLOTS; i++) {   // tap a move to change it
      int ry = 78 + i * 52;
      if (x < 70 || x > 396 || y < ry || y > ry + 50) continue;
      movePickParty = partyDetail;
      movePickSlot = i;
      movePickPage = 0;
      movePickOpen = true;
      sfxPlay(SFX_TAP);
      return;
    }
    if (y > 380) {                           // only the BACK band at the bottom
      partyDetail = 0;
      releaseConfirm = false;
      sfxPlay(SFX_TAP);
    }
    return;
  }
  if (!partyPick && partyHubAt(x, y)) {
    boxOpen = true;                  // open the box, nothing picked yet
    boxPage = 0;
    boxSwapFrom = 0;
    sfxPlay(SFX_TAP);
    return;
  }
  // exit button only (no unmarked top-edge exit trap: issue #24)
  if (y >= PARTYCLOSE_Y && y <= PARTYCLOSE_Y + PARTYCLOSE_H &&
      x >= PARTYCLOSE_X && x <= PARTYCLOSE_X + PARTYCLOSE_W) {
    if (partyPick) {                 // declined the swap: the pet is let go
      if (!clearHandover()) { sfxPlay(SFX_DENY); return; }
      partyPick = false;
    }
    partyOpen = false;
    sfxPlay(SFX_TAP);
    return;
  }
  for (int i = 0; i < PARTY_SLOTS; i++) {
    if (partySlotAt(x, y) != i) continue;
    if (boxSel) {                    // a box creature is waiting for a slot
      if (!party.swapPartyBox(i, boxSel - 1)) { sfxPlay(SFX_DENY); return; }
      boxSel = 0;
      sfxPlay(SFX_MEDAL);
      return;
    }
    if (!partyPick) {
      if (boxSwapFrom == i + 1) {    // tapped again: take it to the box
        boxOpen = true;
        boxPage = 0;
        sfxPlay(SFX_TAP);
        return;
      }
      if (party.slots[i].empty()) { boxSwapFrom = i + 1; sfxPlay(SFX_TAP); return; }
      partyDetail = i + 1;
      boxSwapFrom = i + 1;           // armed, in case the box is opened next
      sfxPlay(SFX_TAP);
      return;
    }
    if (!handoverStored) {
      if (!party.replaceAt(i, pet.endedMon)) { sfxPlay(SFX_DENY); return; }
      handoverStored = true;
    }
    const PartyMon ended = pet.endedMon;
    if (!clearHandover()) { sfxPlay(SFX_DENY); return; }
    snprintf(partyBannerName, sizeof(partyBannerName), "%s",
             ended.nick[0] ? ended.nick : DEX_TBL[ended.dex].name);
    partyBannerUntil = millis() + 3500;
    partyPick = false;
    partyOpen = false;
    sfxPlay(SFX_MEDAL);
    return;
  }
}

// deslizar: dir +1 = hacia la derecha
//
// HORIZONTAL IS THE TILE AXIS, and nothing else. Every screen used to keep its
// own opinion in here -- some paged, some closed, and several paged and THEN
// closed off the end, which is one gesture meaning two things and is exactly
// how the same paging bug shipped four times. Paging moved to the rim
// (onRim), which is what frees this axis to mean one thing.
void onSwipe(int dir) {
  // First boot has no axis yet -- nothing but the region chooser exists -- so
  // horizontal still pages it there. The rim is not a gesture anyone has been
  // taught on the first screen they ever see.
  if (pet.awaitingStarter()) { rpickSwipe(dir); return; }
  if (menuOpen) { menuOpen = false; return; }   // any swipe closes the menu
  if (battleOpen) return;   // no swiping out of a fight

  int t = uiTileIndex();
  if (t >= 0) {
    // A sheet drawn on top of a tile closes first; the axis is underneath it.
    if (galleryDetail) { galleryDetail = 0; galleryPmd.unload(); galleryDirty = true; return; }
    if (partyDetail) { partyDetail = 0; return; }
    if (partyPick) {
      if (!clearHandover()) { sfxPlay(SFX_DENY); return; }
      partyPick = false;
    }
    if (pet.ceremony || confirmUntil) return;
    int n = t - dir;   // the content follows the finger
    // IT BUMPS. This is the whole point of the axis: a horizontal swipe can no
    // longer close anything, so it cannot be confused with paging.
    if (n < 0 || n >= TILE_COUNT) { sfxPlay(SFX_DENY); return; }
    uiTileGo(n);
    return;
  }

  // Off the axis are modals -- pickers, sheets, minigames. None of them page
  // any more, so here horizontal simply backs out.
  if (pickOpen) { pickOpen = false; return; }
  if (trainOpen) { trainOpen = false; return; }
  if (bagOpen) { bagOpen = false; return; }
  if (evoPickOpen) return;
  if (movePickOpen) { movePickOpen = false; return; }
  if (boxOpen) {
    if (boxJumpOpen) { boxJumpOpen = false; return; }
    boxOpen = false; boxSel = 0; return;
  }
  if (gameOpen) { leaveGame(); return; }   // swipe out, keeping what you earned
  if (spdOpen) { leaveSpeed(); return; }
  if (memoOpen) { leaveMemo(); return; }
  if (jukeOpen) { jukeSwipe(dir); return; }
  if (kbOpen) return;
  if (clockOpen) {
    if (resetConfirm) return;
    if (dir < 0 && clockPage + 1 < CLOCK_PAGES) { clockPage++; sfxPlay(SFX_TAP); }
    else if (dir > 0 && clockPage > 0) { clockPage--; sfxPlay(SFX_TAP); }
    else sfxPlay(SFX_DENY);
    return;
  }
  if (cardOpen) { cardOpen = false; return; }
}

void onTap(int16_t x, int16_t y) {
  if (pet.awaitingStarter()) {  // primera partida: region y luego inicial
    if (!starterRegionDone) {
      if (x >= LANBTN_X && x <= LANBTN_X + LANBTN_W &&
          y >= LANBTN_Y && y <= LANBTN_Y + LANBTN_H) {      // random, any region
        pet.setRegion(REGION_ALL);   // first: setRegion re-rolls a waiting egg
        pet.chooseStarter(randomStarter());
        starterRegionDone = true;
        sfxPlay(SFX_TAP);
        return;
      }
      int r = regionPickTap(x, y, RPICK_FOR_START);
      if (r >= 0) {
        pet.setRegion((uint8_t)r);   // the region picked here is where eggs come from too
        starterRegionDone = true;
        sfxPlay(SFX_TAP);
      }
      return;
    }
    if (x >= STARTER_RAND_X && x <= STARTER_RAND_X + STARTER_RAND_W &&
        y >= STARTER_RAND_Y && y <= STARTER_RAND_Y + STARTER_RAND_H) {
      pet.chooseStarter(randomStarterIn(pet.region));
      sfxPlay(SFX_TAP);
      return;
    }
    for (int i = 0; i < starterCountShown(pet.region); i++) {
      int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
      if (x >= 70 && x <= 396 && y >= ry && y <= ry + STARTER_ROW_H) {
        pet.chooseStarter(starterOf(pet.region, (uint8_t)i));
        sfxPlay(SFX_TAP);
        break;
      }
    }
    return;
  }
  if (evoPickOpen) {
    evoPickTap(x, y);
    return;
  }
  if (battleOpen) {
    battleTap(x, y);
    return;
  }
  if (exploreOpen) {
    int8_t regionStep = exploreRegionHit(x, y);
    if (regionStep) {
      exploreStepRegion(regionStep);
      sfxPlay(SFX_TAP);
      return;
    }
    if (y >= EXPLORE_DIF_Y && y <= EXPLORE_DIF_Y + EXPLORE_DIF_H) {
      exploreHard = !exploreHard;
      sfxPlay(SFX_TAP);
      return;
    }
    if (x >= EXPLORE_BTN_X - EXPLORE_BTN_PAD &&
      x <= EXPLORE_BTN_X + EXPLORE_BTN_W + EXPLORE_BTN_PAD &&
      y >= EXPLORE_BTN_Y - EXPLORE_BTN_PAD &&
      y <= EXPLORE_BTN_Y + EXPLORE_BTN_H + EXPLORE_BTN_PAD) {
      if (!startWildBattle(currentExploreRegion(), exploreHard)) { sfxPlay(SFX_DENY); return; }
      exploreOpen = false;
      sfxPlay(SFX_TAP);
    }
    return;
  }
  if (bagOpen) {
    bagTap(x, y);
    return;
  }
  if (playerOpen) {
    if (playerPage == 0 && y >= 32 && y < 68) {   // the name: rename yourself
      openKeyboardFor(KB_TRAINER);
      sfxPlay(SFX_TAP);
      return;
    }
    // only the bottom BACK band closes; a miss near the avatar/name stays put (#24)
    if (playerPage == 0 && x > CX - 40 && x < CX + 40 && y > 70 && y < 146) {
      pet.avatar = (uint8_t)((pet.avatar + 1) % AVATAR_COUNT);
      pet.saveNow();
      sfxPlay(SFX_TAP);
      return;
    }
    if (y > 380) playerOpen = false;
    return;
  }
  if (medalsOpen) {   // nothing on it is live: a tap backs out, as on the card
    medalsOpen = false;
    return;
  }
  if (pickOpen) {
    pickTap(x, y);
    return;
  }
  if (lanOpen) {
    lanTap(x, y);
    return;
  }
  if (gymOpen && gymPick) {
    int r = regionPickTap(x, y, RPICK_FOR_GYMS);
    if (r >= 0) {
      gymRegion = (uint8_t)r;
      gymPage = 0;
      gymPick = false;
      sfxPlay(SFX_TAP);
      return;
    }
    if (y >= LANBTN_Y && y <= LANBTN_Y + LANBTN_H &&
        x >= LANBTN_X && x <= LANBTN_X + LANBTN_W) {   // LAN battle
      gymOpen = false; gymPick = false;
      lan.state = LINK_OFF;
      lanOpen = true;
      sfxPlay(SFX_TAP);
      return;
    }
    if (y > 380) { gymOpen = false; gymPick = false; }
    return;
  }
  if (gymOpen) {
    if (y >= GYMDIF_Y && y <= GYMDIF_Y + GYMDIF_H) {   // the difficulty pill
      gymHard = !gymHard;
      sfxPlay(SFX_TAP);
      return;
    }
    if (x >= GYMBTN_X && x <= GYMBTN_X + GYMBTN_W &&
        y >= GYMBTN_Y && y <= GYMBTN_Y + GYMBTN_H) {
      gymOpen = false;
      lan.state = LINK_OFF;
      lanOpen = true;
      sfxPlay(SFX_TAP);
      return;
    }
    for (int i = 0; i < GYM_ROWS; i++) {
      uint8_t idx = gymPage * GYM_ROWS + i;
      if (idx >= TRAINER_COUNT) break;
      int rx, ry, rw, rh;
      gymRowRect(i, &rx, &ry, &rw, &rh);
      if (x < rx || x > rx + rw || y < ry || y > ry + rh) continue;
      if (!gymUnlocked(idx, gymHard)) { sfxPlay(SFX_DENY); return; }
      sfxPlay(SFX_TAP);
      gymOpen = false;
      pickTrainer = idx;
      pickHard = gymHard;
      pickRegion = gymRegion;     // BEFORE squadCap: it reads pickRegion now
      pickPage = 0;
      pickDefault(squadCap(idx, gymHard));
      pickOpen = true;
      return;
    }
    if (y > 380) gymOpen = false;   // only the bottom BACK band, not a missed row (#24)
    return;
  }
  if (pet.hasLearnOffer()) {
    for (int i = 0; i < MOVE_SLOTS; i++) {
      int ry = LEARN_ROW_Y(i);
      if (x < 70 || x > 396 || y < ry || y > ry + 50) continue;
      sfxPlay(SFX_TAP);
      pet.acceptLearn(i);
      return;
    }
    if (x >= 70 && x <= 396 && y >= LEARN_SKIP_Y && y <= LEARN_SKIP_Y + 44) {
      sfxPlay(SFX_TAP);
      pet.declineLearn();
    }
    return;   // modal: nothing else on screen responds until it is answered
  }
  if (trainOpen) {
    bool inPanel = (x >= TRAIN_X && x <= TRAIN_X + TRAIN_W &&
                    y >= TRAIN_Y && y <= TRAIN_Y + TRAIN_H);
    if (!inPanel) { trainOpen = false; return; }   // tap outside = back to the pet
    for (int i = 0; i < TRAIN_ROWS; i++) {
      int ry = TRAIN_ROW_Y(i);
      if (x < TRAIN_X + 18 || x > TRAIN_X + TRAIN_W - 18) continue;
      if (y < ry || y > ry + TRAIN_ROW_H) continue;
      sfxPlay(SFX_TAP);
      trainOpen = false;
      if (i == TRAIN_ROW_ATK) startSack();
      else if (i == TRAIN_ROW_SPE) startSpeedGame();
      else if (i == TRAIN_ROW_DEF) startGame();   // the ball game trains DEF
      else startMemoGame();                       // and this one trains neither
      return;
    }
    return;
  }
  // The menu is modal and has three independent ways out: the CLOSE row, a tap
  // anywhere on the dimmed area outside the panel, and any swipe (see onSwipe).
  // Deliberately no timeout: a menu that vanishes while you read it is worse
  // than one that lingers.
  if (jukeOpen) { jukeTap(x, y); return; }
  if (menuOpen) {
    bool inPanel = (x >= MENU_X && x <= MENU_X + MENU_W &&
                    y >= MENU_Y && y <= MENU_Y + MENU_H);
    if (!inPanel) { menuOpen = false; return; }   // tap outside = back to the pet
    for (int i = 0; i < MENU_ROWS; i++) {
      int ry = MENU_ROW_Y(i);
      if (x < MENU_X + 12 || x > MENU_X + MENU_W - 12) continue;
      if (y < ry || y > ry + MENU_ROW_H) continue;
      sfxPlay(SFX_TAP);
      menuOpen = false;
      if (i == MENU_STATS) { cardOpen = true; cardPage = 1; }   // straight to the stats page
      else if (i == MENU_BAG) { bagOpen = true; bagPage = 0; }
      else if (i == MENU_DEX) { galleryOpen = true; galleryPick = true; galleryPage = 0; rpickPage = 0; galleryDetail = 0; galleryDirty = true; }
      else if (i == MENU_JUKE) { openJukebox(); }
      else if (i == MENU_SETTINGS) { openClock(); }
      else if (i == MENU_RETIRE) {
        if (!pet.canRetireNow()) { sfxPlay(SFX_DENY); return; }
        choiceKind = 3; choiceUntil = millis() + 12000;
      }
      return;                                     // MENU_CLOSE: just shut
    }
    return;
  }
  if (movePickOpen) {
    uint8_t all[MOVE_COUNT];
    uint8_t n = learnableList(all, sizeof(all));
    for (uint8_t i = 0; i < MOVE_PICK_PER_PAGE; i++) {
      uint8_t idx = movePickPage * MOVE_PICK_PER_PAGE + i;
      if (idx >= n) break;
      int ry = MOVE_PICK_Y(i);
      if (x < 70 || x > 396 || y < ry || y > ry + 50) continue;
      sfxPlay(SFX_TAP);
      // Swapping for a move already in another slot would silently duplicate
      // it, so trade the two slots instead of overwriting.
      if (movePickParty && !party.writable()) { sfxPlay(SFX_DENY); return; }
      uint8_t *tgt = pickTargetMoves();
      uint8_t before[MOVE_SLOTS];
      memcpy(before, tgt, sizeof(before));
      for (int s = 0; s < MOVE_SLOTS; s++)
        if (tgt[s] == all[idx] && s != movePickSlot) tgt[s] = tgt[movePickSlot];
      tgt[movePickSlot] = all[idx];
      if (movePickParty) {
        if (!party.save()) {
          memcpy(tgt, before, sizeof(before));
          sfxPlay(SFX_DENY);
          return;
        }
      } else pet.saveNow();
      movePickOpen = false;
      return;
    }
    if (y > 380) movePickOpen = false;   // only the bottom BACK band (#24)
    return;
  }
  if (partyOpen) {
    partyTap(x, y);
    return;
  }
  if (galleryOpen) {
    if (galleryPick) {
      int r = regionPickTap(x, y, RPICK_FOR_DEX);
      if (r >= 0) {
        galleryRegion = (uint8_t)r;
        galleryPage = 0;
        galleryClampPage();
        galleryDetail = 0;
        galleryDirty = true;
        galleryPick = false;
        sfxPlay(SFX_TAP);
      } else if (y > 380) {
        galleryOpen = false;
      }
      return;
    }
    galleryTap(x, y);
    return;
  }
  if (kbOpen) {
    keyboardTap(x, y);
    return;
  }
  if (clockOpen) {
    clockTap(x, y);
    return;
  }
  if (pet.ceremony) return;  // durante la despedida no hay botones
  if (cardOpen) {
    if (cardPage == 0 && y < 84) openKeyboard();  // tocar el nombre = renombrar
    else if (cardPage == 2) {
      for (int i = 0; i < MOVE_SLOTS; i++) {   // tap a slot to change it
        int ry = MOVE_ROW_Y(i);
        if (x < 70 || x > 396 || y < ry || y > ry + 50) continue;
        sfxPlay(SFX_TAP);
        movePickParty = 0;      // the live pet
        movePickSlot = i;
        movePickPage = 0;
        movePickOpen = true;
        return;
      }
      if (y > 380) cardOpen = false;   // only the bottom BACK band (#24)
    } else if (y > 380) {
      cardOpen = false;
    }
    return;
  }
  if (spdOpen) {
    spdTap(x, y);
    return;
  }
  if (memoOpen) {
    memoTap(x, y);
    return;
  }
  if (gameOpen) {
    gameTap(x, y);
    return;
  }
  if (choiceKind) {          // dialogo de decision: boton accion (arriba) / mantener (abajo)
    int c1t, c1b, c2t, c2b;
    uiConfirmRects(&c1t, &c1b, &c2t, &c2b);
    const bool inX = (x >= CONFIRM_BTN_X && x <= CONFIRM_BTN_X + CONFIRM_BTN_W);
    bool b1 = inX && y >= c1t && y <= c1b;   // accion
    bool b2 = inX && y >= c2t && y <= c2b;   // mantener / quedaros
    if (choiceKind == 1) {                 // evolucion
      if (b1) {
        int16_t options[EVO_BRANCH_MAX];
        if (pet.evoOptions(pet.speciesId, options) > 1) {
          evoPickOpen = true;
          evoPickPage = 0;
        } else {
          int16_t old = pet.speciesId;
          pet.evolve();
          evoPmd.load(old, pet.shiny);
        }
      }
      else if (b2) pet.declineEvolve();
    } else if (choiceKind == 3) {          // retirada a peticion
      if (b1) pet.startRetire();
      // b2 is simply "no": nothing to decline, the row is always there
    } else if (choiceKind == 2) {          // despedida
      if (b1) pet.startFarewell();
      else if (b2) pet.declineFarewell();
    }
    choiceKind = 0;
    return;
  }
  if (confirmUntil) {        // dialogo "soltar?": SI / NO
    if (millis() < confirmUntil && x >= 118 && x <= 218 && y >= 252 && y <= 304) {
      pet.release();
    }
    confirmUntil = 0;
    return;
  }
  if (feedMenuUntil) {       // selector de comida
    if (millis() < feedMenuUntil && y >= FEED_MENU_Y && y <= FEED_MENU_Y + FEED_MENU_H &&
        x >= FEED_MENU_X && x <= FEED_MENU_X + FEED_MENU_W) {
      int item = (x - FEED_MENU_X) / (FEED_MENU_W / 4);
      if (item > 3) item = 3;
      eatItem = (uint8_t)item;
      if (item == 3) pet.feedCandy();
      else pet.feedBerry(item);
      sfxPlay(SFX_EAT);
    }
    feedMenuUntil = 0;
    return;
  }
  if (pet.isEgg()) {
    // the region pill first, or choosing a region would also crack the egg --
    // and a near miss is swallowed rather than counted, since three taps hatch
    if (eggRegionTap(x, y)) return;
    pet.eggTap();
    sfxPlay(SFX_TAP);
    return;
  }
  // boton de evolucion: abre el dialogo evolucionar/mantener
  if (pet.wantEvolveButton() && x >= EVO_BTN_X && x <= EVO_BTN_X + EVO_BTN_W &&
      y >= EVO_BTN_Y && y <= EVO_BTN_Y + EVO_BTN_H) {
    choiceKind = 1; choiceUntil = millis() + 12000;
    return;
  }
  // botones de final (mismo recuadro): escapada directa; despedida abre dialogo.
  //
  // The runaway does NOT ask, deliberately. A pet you have to authorise to
  // leave is not really at stake, and neglect having teeth is the whole premise.
  // What was actually wrong is that a night's sleep could reach this state at
  // all -- fixed where it belongs, in the drain, by having the creature put
  // itself to bed (Pet::tick, autoSleep).
  if (x >= FAR_BTN_X && x <= FAR_BTN_X + FAR_BTN_W &&
      y >= FAR_BTN_Y && y <= FAR_BTN_Y + FAR_BTN_H) {
    if (pet.canRunawayNow()) { pet.startRunaway(); return; }
    if (pet.wantFarewellButton()) { choiceKind = 2; choiceUntil = millis() + 12000; return; }
  }
  for (int i = 0; i < BTN_COUNT; i++) {
    int dx = x - buttons[i].cx, dy = y - buttons[i].cy;
    if (dx * dx + dy * dy <= BTN_HIT * BTN_HIT) {
      if (uiButtonDisabled(i)) { sfxPlay(SFX_DENY); return; }
      sfxPlay(SFX_TAP);
      if (i == BTN_FOOD) feedMenuUntil = millis() + 6000;
      else if (i == BTN_LIGHT) pet.toggleLight();
      else if (i == BTN_BATH) startBath();
      else trainOpen = true;
      return;
    }
  }
  // tapping the name/status band opens the menu. This band was inert before,
  // and it sits clear of inPetZone (which starts at y 95).
  if (y >= 28 && y < 94) {
    menuOpen = true;
    sfxPlay(SFX_TAP);
    return;
  }
  // tocar al bicho = caricia
  if (inPetZone(x, y)) {
    pet.caress();
    if (!pet.sleeping) sfxPlay(SFX_HEART);
  }
}

// ---------- render ----------

bool gNight = false;  // noche real (por hora) o durmiendo: lo fija render()
uint16_t inkColor() { return gNight ? UI_INK_NIGHT : UI_INK; }

// ---------- escena de fondo: bioma del tipo + hora real del RTC ----------

#define C565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define HORIZON 232  // linea donde el cielo se encuentra con el suelo

uint16_t lerp565(uint16_t a, uint16_t b, int i, int n) {
  if (n <= 0) return a;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)((((ar + (br - ar) * i / n) << 11)) |
                    (((ag + (bg - ag) * i / n) << 5)) | (ab + (bb - ab) * i / n));
}

void drawPmdActZ(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil,
                 uint8_t maxS, uint16_t zoom100, uint16_t maxPx);

static uint8_t speciesBiome(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return BIOME_MEADOW;
  const DexEntry &d = DEX_TBL[dex];
  if (d.type1 == T_GHOST || d.type2 == T_GHOST) return BIOME_GRAVEYARD;
  return d.biome;
}

static uint8_t sceneBiome() {
  if (gBiomeOverride >= 0) return (uint8_t)gBiomeOverride;
  return pet.isEgg() ? BIOME_MEADOW : speciesBiome(pet.speciesId);
}

// hora del dia 0-23 (de la hora real cacheada cada 30s; 13 si no hay reloj)
int sceneHour() {
  uint32_t e = pet.lastSeenEpoch;
  return e ? (int)((e / 3600) % 24) : 13;
}

// ground of each biome by day (at night it blends toward the night blue)
static const uint16_t BIOME_SOIL[BIOME_COUNT] = {
  C565(0x7e, 0xc0, 0x7f),  // BIOME_MEADOW
  C565(0xdc, 0xca, 0x94),  // BIOME_BEACH (sand)
  C565(0x3f, 0x78, 0x4c),  // BIOME_FOREST
  C565(0x5e, 0x40, 0x3c),  // BIOME_VOLCANO (dark basalt)
  C565(0xa8, 0x90, 0x6a),  // BIOME_MOUNTAIN
  C565(0xe6, 0xee, 0xf5),  // BIOME_SNOW
  C565(0x5e, 0x5a, 0x6e),  // BIOME_GRAVEYARD (cold violet-grey earth)
};

// One puffy cloud: a flat-bottomed body of overlapping circles, a soft shadow
// band under it and a highlight on the top puffs (from crenberries428/TamaPoke, MIT).
static void drawCloud(int cx, int cy, int s, uint16_t col, uint16_t shade) {
  auto px = [&](int v) { return v * s / 100; };
  int r1 = px(17), r2 = px(13), r3 = px(11), r4 = px(9);
  gfx->fillRoundRect(cx - px(30), cy + px(2), px(60), px(14), px(7), shade);
  gfx->fillCircle(cx + px(20), cy + px(5), r3, shade);
  gfx->fillCircle(cx - px(20), cy + px(6), r4, shade);
  gfx->fillRoundRect(cx - px(30), cy - px(2), px(60), px(14), px(7), col);
  gfx->fillCircle(cx - px(2), cy - px(6), r1, col);
  gfx->fillCircle(cx + px(18), cy + px(1), r2, col);
  gfx->fillCircle(cx - px(19), cy + px(3), r3, col);
  gfx->fillCircle(cx + px(30), cy + px(6), r4, col);
  gfx->fillCircle(cx - px(30), cy + px(7), r4, col);
}

void drawClouds(uint32_t now, uint16_t col, uint16_t shade) {
  static const int Y[3] = { 62, 108, 170 };
  static const int S[3] = { 120, 80, 100 };
  static const int OFS[3] = { 0, 190, 360 };
  static const int DIV[3] = { 70, 110, 90 };
  for (int k = 0; k < 3; k++) {
    int cx = (int)((now / DIV[k] + OFS[k]) % 600) - 70;
    drawCloud(cx, Y[k], S[k], col, shade);
  }
}

static void drawSun(int cx, int cy, int r, uint16_t core, uint16_t sky, uint32_t now) {
  for (int i = 0; i < 4; i++)
    gfx->fillCircle(cx, cy, r + 30 - i * 8, lerp565(sky, core, i + 1, 9));
  float a0 = (now % 24000) * (2.0f * PI / 24000.0f);
  for (int k = 0; k < 8; k++) {
    float a = a0 + k * (PI / 4.0f);
    float ca = cosf(a), sa = sinf(a), cb = cosf(a + 0.16f), sb = sinf(a + 0.16f);
    int r0 = r + 4, r1 = r + (k & 1 ? 20 : 28);
    gfx->fillTriangle(cx + (int)(ca * r0), cy + (int)(sa * r0),
                      cx + (int)(cb * r0), cy + (int)(sb * r0),
                      cx + (int)(ca * r1 + cb * r1) / 2, cy + (int)(sa * r1 + sb * r1) / 2,
                      lerp565(sky, core, 6, 9));
  }
  gfx->fillCircle(cx, cy, r, core);
  gfx->fillCircle(cx, cy, r - 5, lerp565(core, C565(0xff, 0xff, 0xff), 1, 3));
  gfx->fillCircle(cx - r / 3, cy - r / 3, r / 4, C565(0xff, 0xff, 0xff));
}

static void drawStars(uint32_t now, uint16_t top, uint16_t bot, int yMax) {
  static struct { int16_t x, y; uint16_t period, phase; uint8_t size; } st[STAR_COUNT];
  static bool rolled = false;
  if (!rolled) {
    for (int i = 0; i < STAR_COUNT; i++) {
      int x, y;
      do {
        x = 20 + random(426); y = 24 + random(yMax - 24);
      } while ((x - 233) * (x - 233) + (y - 233) * (y - 233) > 215 * 215);
      st[i] = { (int16_t)x, (int16_t)y, (uint16_t)(1400 + random(2600)),
                (uint16_t)random(4000), (uint8_t)(random(4) == 0 ? 2 : 1) };
    }
    rolled = true;
  }
  for (int i = 0; i < STAR_COUNT; i++) {
    int t = (int)((now + st[i].phase) % st[i].period) * 16 / st[i].period;
    int b = t < 8 ? t : 16 - t;
    int sy = st[i].y;
    uint16_t sky = lerp565(top, bot, sy, HORIZON);
    uint16_t c = lerp565(sky, C565(0xff, 0xff, 0xf0), 3 + b, 11);
    int sz = st[i].size * 2;
    gfx->fillRect(st[i].x, sy, sz, sz, c);
    if (st[i].size == 2 && b >= 5) {
      uint16_t d = lerp565(sky, c, 1, 2);
      gfx->fillRect(st[i].x - 3, sy + 1, 10, 2, d);
      gfx->fillRect(st[i].x + 1, sy - 3, 2, 10, d);
    }
  }
}

static void drawMoon(int cx, int cy, uint16_t sky) {
  const int R = 26, CR = 22, cutX = cx + 12;
  for (int i = 0; i < 4; i++)
    gfx->fillCircle(cx, cy, 50 - i * 6, lerp565(sky, C565(0xb8, 0xc4, 0xe0), i + 1, 12));
  uint16_t lit = C565(0xf2, 0xf0, 0xdc);
  for (int dy = -R; dy <= R; dy++) {
    int mw = (int)sqrtf((float)(R * R - dy * dy));
    int x0 = cx - mw, x1 = cx + mw;
    if (abs(dy) >= CR) { gfx->fillRect(x0, cy + dy, x1 - x0 + 1, 1, lit); continue; }
    int cw = (int)sqrtf((float)(CR * CR - dy * dy));
    int c0 = cutX - cw;
    if (c0 > x0) gfx->fillRect(x0, cy + dy, min(c0, x1 + 1) - x0, 1, lit);
    if (cutX + cw < x1) gfx->fillRect(cutX + cw + 1, cy + dy, x1 - cutX - cw, 1, lit);
  }
  uint16_t cr = C565(0xd6, 0xd6, 0xc2);
  gfx->fillCircle(cx - 18, cy + 8, 3, cr);
  gfx->fillCircle(cx - 20, cy - 8, 2, cr);
  gfx->fillCircle(cx - 12, cy + 18, 2, cr);
}

// Volcano biome, in the forest's style: layered tones lit from the sun side, and
// hand-built shapes rather than flat triangles. Back to front: a jagged distant
// range with a small second cone, a smoke plume lit from below, the main cone
// (craggy scanline-built edges, four rock tones, eroded gullies, a rim warmed by
// the crater), lava streams, an occasional eruption burst and lava bombs, glowing
// ground cracks, a small SLUGMA crawling far back, faceted boulders and a charred
// dead tree. The cone sits left of centre so its plume stays clear of the sun and
// moon (upper right). Keep the ground's lower left clear: poops land there (see
// drawPoops). The step counter covers the far left, the HUD everything below ~310.

// Cheap deterministic noise 0..255: fixed per (a, seed), so edges never shimmer.
static int volNoise(int a, int seed) {
  uint32_t v = (uint32_t)a * 374761393u + (uint32_t)seed * 668265263u;
  v = (v ^ (v >> 13)) * 1274126177u;
  return (int)((v >> 16) & 0xff);
}

// A boulder: a fan of facets around a noisy outline, tone chosen by the facet's
// facing (lit toward the upper right), with a lava rim-light on the lower left.
static void volBoulder(int cx, int cy, int r, int seed, bool night, uint16_t lava) {
  uint16_t t[4];
  if (night) { t[0] = C565(0x0c, 0x08, 0x0e); t[1] = C565(0x18, 0x12, 0x18); t[2] = C565(0x24, 0x1c, 0x24); t[3] = C565(0x34, 0x28, 0x30); }
  else       { t[0] = C565(0x2a, 0x1e, 0x22); t[1] = C565(0x42, 0x30, 0x34); t[2] = C565(0x5e, 0x46, 0x46); t[3] = C565(0x80, 0x60, 0x58); }
  const int N = 8;
  int vx[N + 1], vy[N + 1];
  for (int i = 0; i < N; i++) {
    float an = i * 6.2832f / N;
    int rr = r * (78 + volNoise(i, seed) % 30) / 100;
    vx[i] = cx + (int)(cosf(an) * rr * 1.25f);                // wider than tall: it sits on the ground
    vy[i] = cy + (int)(sinf(an) * rr * 0.8f);
  }
  vx[N] = vx[0]; vy[N] = vy[0];
  for (int i = 0; i < N; i++) {
    float an = (i + 0.5f) * 6.2832f / N;                       // facet facing; y is down, so up-right is negative sin
    float lit = cosf(an) * 0.6f - sinf(an) * 0.8f;             // dot with the sun direction
    uint16_t c = lit > 0.75f ? t[3] : (lit > 0.2f ? t[2] : (lit > -0.4f ? t[1] : t[0]));
    gfx->fillTriangle(cx, cy - r / 6, vx[i], vy[i], vx[i + 1], vy[i + 1], c);
  }
  uint16_t rim = lerp565(t[0], lava, 1, 2);                    // lava glow along the lower-left edge
  for (int i = 3; i <= 5; i++) gfx->drawLine(vx[i], vy[i], vx[i + 1], vy[i + 1], rim);
}

// Charred dead tree: forked bare limbs, a lit edge, and ember cracks that pulse.
// Thickness steps down limb by limb; the tips sway.
static void volDeadTree(int cx, int footY, uint32_t now, float sway, bool night, uint16_t lava) {
  uint16_t ch  = night ? C565(0x14, 0x0e, 0x10) : C565(0x2e, 0x22, 0x22);
  uint16_t chL = night ? C565(0x24, 0x1a, 0x1c) : C565(0x58, 0x40, 0x3a);
  static const int16_t LB[12][5] = {                          // x0 y0 x1 y1 width
    { 0, 0, -3, -50, 4 }, { -3, -50, 2, -96, 3 },
    { -3, -50, -26, -74, 2 }, { -26, -74, -38, -98, 2 },
    { -3, -56, 22, -72, 2 }, { 22, -72, 34, -100, 2 },
    { 2, -96, -12, -122, 2 }, { 2, -96, 16, -118, 2 },
    { -38, -98, -47, -110, 1 }, { 34, -100, 45, -111, 1 },
    { -12, -122, -18, -136, 1 }, { 16, -118, 25, -132, 1 } };
  for (int i = 0; i < 4; i++)                                // root flare
    gfx->fillTriangle(cx + (i < 2 ? -14 : 14), footY + 2, cx + (i < 2 ? -4 : 4), footY - 14, cx + (i < 2 ? -4 : 4), footY + 2, ch);
  for (int i = 0; i < 12; i++) {
    int x0 = cx + LB[i][0] + (int)(sway * -LB[i][1] / 100.0f), y0 = footY + LB[i][1];
    int x1 = cx + LB[i][2] + (int)(sway * -LB[i][3] / 100.0f), y1 = footY + LB[i][3];
    for (int w = 0; w < LB[i][4]; w++) gfx->drawLine(x0 + w - LB[i][4] / 2, y0, x1 + w - LB[i][4] / 2, y1, ch);
    gfx->drawLine(x0 + LB[i][4] / 2, y0, x1 + LB[i][4] / 2, y1, chL);            // lit right edge
  }
  float pulse = (sinf(now / 600.0f) + 1.0f) * 0.5f;
  uint16_t ember = lerp565(lava, C565(0xff, 0xd0, 0x58), (int)(pulse * 4), 8);
  static const int8_t EM[5][3] = { { -1, -14, 8 }, { 0, -30, 6 }, { -3, -52, 6 }, { 1, -74, 7 }, { -27, -80, 5 } };
  for (int i = 0; i < 5; i++)                                // glowing cracks in the char
    gfx->fillRect(cx + EM[i][0] + (int)(sway * -EM[i][1] / 100.0f), footY + EM[i][1], 1, EM[i][2], ember);
}

// A small SLUGMA creeping along the ground far behind the pet. Loaded once on
// entering the volcano (~140 KB of PSRAM) and freed when the biome changes.
static PmdMon gVolcanoMon;
static bool gVolcanoTried = false;

static void drawVolcano(uint32_t now, bool night, uint16_t top, uint16_t bot) {
  const int VX = 170, VT = HORIZON - 104, B = HORIZON + 2;   // crater centre, cone top, base
  float pulse = (sinf(now / 420.0f) + 1.0f) * 0.5f;          // 0..1
  uint16_t rockD = night ? C565(0x0e, 0x0a, 0x10) : C565(0x34, 0x24, 0x28);
  uint16_t rock  = night ? C565(0x1c, 0x16, 0x1c) : C565(0x56, 0x3e, 0x40);
  uint16_t rockM = night ? C565(0x2a, 0x20, 0x28) : C565(0x74, 0x52, 0x4c);
  uint16_t rockL = night ? C565(0x3a, 0x2e, 0x36) : C565(0x98, 0x6c, 0x60);
  uint16_t lava  = lerp565(C565(0xe8, 0x4a, 0x14), C565(0xff, 0x9a, 0x2a), (int)(pulse * 8), 8);
  uint16_t hot   = C565(0xff, 0xd0, 0x58);
  auto skyAt = [&](int y) { return lerp565(top, bot, y < 0 ? 0 : y, HORIZON); };
  auto hw = [&](int y) { return 26 + (y - VT) * 100 / (B - VT); };   // cone half-width at y
  auto seg = [&](int x0, int x1, int y, uint16_t c) { if (x1 > x0) gfx->fillRect(x0, y, x1 - x0, 2, c); };

  // heat glow along the horizon, then a jagged distant range and a small far cone
  gfx->fillRect(0, HORIZON - 12, 466, 14, lerp565(skyAt(HORIZON), lava, 1, 7));
  uint16_t far = lerp565(rock, skyAt(HORIZON - 40), 1, 2);
  for (int x = -10; x < 480; x += 14) {                      // ridge profile interpolated between noisy peaks
    int h1 = 14 + volNoise(x / 14, 11) % 30, h2 = 14 + volNoise(x / 14 + 1, 11) % 30;
    gfx->fillTriangle(x, B, x + 14, B, x, B - h1, far);
    gfx->fillTriangle(x + 14, B, x + 14, B - h2, x, B - h1, far);
  }
  gfx->fillTriangle(330, B, 480, B, 400, HORIZON - 54, far);                              // far second cone
  gfx->fillRect(393, HORIZON - 56, 14, 3, lerp565(far, lava, 1, 3 + (int)(pulse * 1)));  // its faint crater glow

  // smoke: puffs rise, grow and fade; each is a dark body with a lighter disc
  // low on it, so the young ones read as lit orange from the crater below
  uint16_t smoke = night ? C565(0x34, 0x2c, 0x3c) : C565(0x5c, 0x4a, 0x50);
  for (int k = 0; k < 7; k++) {
    int t = (int)((now / 36 + k * 31) % 217);               // 0..216 life of one puff
    int y = VT - 6 - t;
    int x = VX + 6 + t / 3 + (int)(sinf((t + k * 20) / 22.0f) * 8);
    int r = 10 + t / 8;
    uint16_t c = lerp565(smoke, skyAt(y), t, 217);
    uint16_t glowC = t < 90 ? lerp565(c, lava, 90 - t, 260) : lerp565(c, C565(0xff, 0xff, 0xff), 1, night ? 14 : 8);
    gfx->fillCircle(x, y, r, c);
    gfx->fillCircle(x + 1, y + r / 4, r * 65 / 100, glowC);
  }

  // lightning flickering inside the plume every few seconds
  if (now % 5200 < 140) {
    int lx = VX + 30, ly = VT - 120;
    uint16_t bolt = C565(0xff, 0xf4, 0xc8);
    static const int8_t Z[5][2] = { { 0, 0 }, { -9, 14 }, { 6, 28 }, { -8, 44 }, { 2, 62 } };
    for (int i = 0; i < 4; i++) {
      gfx->drawLine(lx + Z[i][0], ly + Z[i][1], lx + Z[i + 1][0], ly + Z[i + 1][1], bolt);
      gfx->drawLine(lx + Z[i][0] + 1, ly + Z[i][1], lx + Z[i + 1][0] + 1, ly + Z[i + 1][1], bolt);
    }
  }

  // main cone, one 2 px scanline at a time: noisy edges, four rock bands whose
  // borders wobble, lava warmth near the rim, and rock/light specks
  for (int y = VT; y <= B; y += 2) {
    int w = hw(y);
    int xl = VX - w + volNoise(y / 4, 1) % 5 - 2, xr = VX + w + volNoise(y / 4, 2) % 5 - 2, W = xr - xl;   // noise per 8 px: craggy, not stair-stepped
    int wob = (int)(sinf(y / 9.0f) * 4.0f);
    int b1 = xl + W * 30 / 100 + wob + volNoise(y, 3) % 7 - 3;
    int b2 = xl + W * 55 / 100 - wob + volNoise(y, 4) % 7 - 3;
    int b3 = xl + W * 76 / 100 + wob + volNoise(y, 5) % 7 - 3;
    int k = y < VT + 30 ? (30 - (y - VT)) * (50 + (int)(pulse * 14)) / 30 : 0;   // lava warmth, 0..~64
    seg(xl, b1, y, k ? lerp565(rockD, lava, k, 160) : rockD);
    seg(b1, b2, y, k ? lerp565(rock, lava, k, 160) : rock);
    seg(b2, b3, y, k ? lerp565(rockM, lava, k, 160) : rockM);
    seg(b3, xr, y, k ? lerp565(rockL, lava, k, 160) : rockL);
    if (volNoise(y, 6) < 70 && W > 4) gfx->fillRect(xl + volNoise(y, 7) % W, y, 3, 2, rockD);
    if (volNoise(y, 8) < 60 && xr > b3 + 3) gfx->fillRect(b3 + volNoise(y, 9) % (xr - b3 - 2), y, 2, 2, lerp565(rockL, C565(0xff, 0xff, 0xff), 1, 6));
  }
  for (int g = 0; g < 6; g++)                                // eroded gullies fanning from the rim
    for (int y = VT + 12; y < B - 4; y += 3) {
      int x = VX + (g * 46 - 115) * (y - VT) / (B - VT) + volNoise(y + g * 31, 12) % 5 - 2;
      gfx->fillRect(x, y, 2, 3, rockD);
    }
  volBoulder(VX - 98, B + 6, 8, 5, night, lava);              // a few boulders at the foot
  volBoulder(VX + 58, B + 8, 9, 6, night, lava);
  volBoulder(VX + 108, B + 4, 6, 7, night, lava);

  // crater: a rocky lip, a dark throat, a lava lake with a hot centre and bubbles
  gfx->fillRoundRect(VX - 34, VT - 9, 68, 17, 8, rockM);
  gfx->fillRoundRect(VX - 34, VT - 9, 68, 5, 3, rockL);                       // lit lip
  gfx->fillRoundRect(VX - 28, VT - 6, 56, 11, 5, rockD);
  gfx->fillRoundRect(VX - 25, VT - 5, 50, 9, 4, lava);
  gfx->fillRoundRect(VX - 13, VT - 3, 26, 4, 2, hot);
  for (int i = 0; i < 3; i++)                                                  // bubbles popping
    if ((now / 260 + i * 2) % 5 < 2) gfx->fillCircle(VX - 16 + i * 16 + (int)(sinf(now / 300.0f + i) * 3), VT - 3, 2, hot);

  // lava streams, wiggling, white-hot at the top, with a bright core and a glow at the foot
  const int SX[3] = { VX - 10, VX + 8, VX + 18 };
  const int SD[3] = { -34, -4, 40 };                         // total drift of each stream
  const int SL[3] = { 96, 70, 100 };                         // length
  for (int i = 0; i < 3; i++) {
    for (int y = 0; y < SL[i]; y += 2) {
      int x = SX[i] + SD[i] * y / SL[i] + (int)(sinf(y / 9.0f + i * 2) * 3);
      gfx->fillRect(x - 3, VT + 4 + y, 7, 3, lerp565(lava, rockD, 1, 5));   // dark crust edge
      gfx->fillRect(x - 2, VT + 4 + y, 5, 3, y < 10 ? hot : lava);
      gfx->fillRect(x - 1, VT + 4 + y, 2, 3, lerp565(lava, hot, 1, 2));     // core
      if (y > 20 && volNoise(y + i * 17, 16) < 50) gfx->fillRect(x - 2, VT + 4 + y, 5, 3, lerp565(lava, rockD, 2, 3));   // cooled crust patches
    }
    int fx = SX[i] + SD[i];
    if (VT + 4 + SL[i] > B - 14) gfx->fillCircle(fx, B - 2, 6 + (int)(pulse * 2), lerp565(rock, lava, 1, 2));
  }

  // every ~9 s the crater erupts for a second: a fountain of sparks and a flare
  uint32_t ep = now % 9000;
  if (ep < 1100) {
    int et = (int)(ep * 100 / 1100);                         // 0..99
    gfx->fillCircle(VX, VT - 2, 16 * (100 - et) / 100 + 4, lerp565(lava, hot, 1, 2));
    for (int i = 0; i < 16; i++) {
      int dx = (volNoise(i, 17) % 121) - 60, vy = 70 + volNoise(i, 18) % 70;
      int x = VX + dx * et / 100, y = VT - vy * et / 100 + 90 * et * et / 10000;
      if (y > 14 && y < B - 4) gfx->fillRect(x, y, 3, 3, i & 1 ? hot : lava);
    }
  }

  // lava bombs: thrown from the crater on arcs, trailing sparks
  static const int8_t BVX[4] = { -92, -40, 56, 104 };         // horizontal reach
  static const uint8_t BH[4] = { 130, 104, 120, 90 };         // arc height
  for (int k = 0; k < 4; k++) {
    for (int tr = 0; tr < 3; tr++) {                         // head, then two fading trail dots
      int ph = (int)((now / 12 + k * 50) % 210) - tr * 5;     // 0..209, tr steps back in time
      if (ph < 0 || ph >= 150) continue;
      int x = VX + BVX[k] * ph / 150;
      int y = VT - 4 * BH[k] * ph * (150 - ph) / (150 * 150);
      uint16_t c = tr == 0 ? hot : (tr == 1 ? lava : lerp565(skyAt(y), lava, 1, 2));
      int sz = tr == 0 ? 5 : (tr == 1 ? 4 : 3);
      if (y > 14 && y < B - 4) gfx->fillRect(x - sz / 2, y - sz / 2, sz, sz, c);
    }
  }

  // cracked ground: thin jagged fissures with dim lava showing through, kept to
  // the edges and well under the stream brightness so the pet stays the focus
  static const int16_t CK[5][8][2] = {                        // x,y points, x < 0 ends the line
    { { 332, 236 }, { 348, 248 }, { 338, 262 }, { 362, 274 }, { 354, 290 }, { 378, 304 }, { -1, 0 } },
    { { 348, 248 }, { 372, 254 }, { 390, 246 }, { -1, 0 } },
    { { 414, 240 }, { 426, 256 }, { 416, 270 }, { 442, 284 }, { -1, 0 } },
    { { 112, 238 }, { 126, 250 }, { 116, 262 }, { 138, 272 }, { -1, 0 } },
    { { 262, 246 }, { 274, 260 }, { 266, 274 }, { 286, 286 }, { -1, 0 } } };
  uint16_t crack = lerp565(lerp565(rockD, lava, 5, 8), lava, (int)(pulse * 3), 8);
  for (int c = 0; c < 5; c++)
    for (int i = 0; CK[c][i + 1][0] >= 0; i++) {
      int x0 = CK[c][i][0], y0 = CK[c][i][1], x1 = CK[c][i + 1][0], y1 = CK[c][i + 1][1];
      gfx->drawLine(x0 - 1, y0, x1 - 1, y1, rockD);           // dark lip either side
      gfx->drawLine(x0 + 1, y0, x1 + 1, y1, rockD);
      gfx->drawLine(x0, y0, x1, y1, crack);
    }

  // a SLUGMA creeping along behind the pet, slowly
  if (!gVolcanoTried) { gVolcanoTried = true; gVolcanoMon.load(218, false); }
  if (gVolcanoMon.loaded) {
    const uint32_t P = 34000;
    uint32_t u = now % (2 * P);
    bool right = u < P;
    uint32_t tri = right ? u : 2 * P - u;
    int x = 250 + (int)(100ULL * tri / P);
    uint8_t act = right ? PMD_WALKR : PMD_WALKL;
    if (!gVolcanoMon.has(act)) act = PMD_IDLE;
    drawPmdActZ(gVolcanoMon, act, x, 262, now, true, false, 2, 70, 220);
  }

  // faceted boulders in front, on the right and the middle ground
  volBoulder(412, 296, 17, 1, night, lava);
  volBoulder(380, 302, 10, 2, night, lava);
  volBoulder(306, 292, 11, 3, night, lava);
  volBoulder(452, 300, 12, 4, night, lava);
  // the charred tree last, so it stands in front
  volDeadTree(436, 274, now, sinf(now / 1700.0f) * 2.0f, night, lava);

  // ash drifting down across the sky, embers rising from the crater
  for (int a = 0; a < 14; a++) {
    int x = (a * 67 + (int)(now / 70) + a * a * 5) % 466;
    int y = (a * 41 + (int)(now / 34)) % (HORIZON - 30) + 24;
    gfx->fillRect(x, y, 2, 2, lerp565(skyAt(y), rockD, 1, 2));
  }
  for (int e = 0; e < 8; e++) {
    int life = (int)((now / (14 + e * 2) + e * 53) % 190);
    int x0 = VX - 20 + e * 6;
    int y0 = VT;
    int x = x0 + (int)(sinf((life + e * 17) / 14.0f) * 10);
    int y = y0 - life;
    if (y < 20) continue;
    gfx->fillRect(x, y, life < 120 ? 3 : 2, life < 120 ? 3 : 2, life < 120 ? hot : lava);
  }
}

// Beach biome: a hazy island and a sailboat on a banded sea whose crests drift
// at depth-scaled speeds, a glitter path under the sun or moon, gulls by day, a
// tide that washes foam in and out over wet sand, an EXEGGUTOR pacing the
// shore, a WINGULL overhead and shells.
// The sea horizon is higher than HORIZON so there is room for water above the
// pet; the lower left stays bare (poops land there, see drawPoops).
#define SEA_TOP 176   // sea horizon y

// A small EXEGGUTOR strolls the shore far behind the pet and a WINGULL flies
// over. Each PMD sprite (~140 KB of PSRAM) is loaded once on entering the beach
// and freed when the biome changes.
static PmdMon gBeachMon;
static PmdMon gBeachBird;          // WINGULL, wheeling across the sky by day
static bool gBeachBirdTried = false;
static bool gBeachTried = false;   // one load attempt per visit: no SD must not retry every frame

static void drawBeach(uint32_t now, bool night, int h, uint16_t top, uint16_t bot, uint16_t soil) {
  bool sunset = !night && h >= 18, sunrise = !night && h < 8;
  auto skyAt = [&](int y) { return lerp565(top, bot, y < 0 ? 0 : y, HORIZON); };

  uint16_t farC, nearC;
  if (night)  { farC = C565(0x1e, 0x38, 0x58); nearC = C565(0x0c, 0x1c, 0x34); }
  else        { farC = C565(0x86, 0xcc, 0xdc); nearC = C565(0x2a, 0x82, 0xb0); }
  if (sunset || sunrise) {                                  // tint the water by the sky
    farC = lerp565(farC, bot, 1, 3);
    nearC = lerp565(nearC, bot, 1, 5);
  }
  uint16_t crest = night ? lerp565(nearC, C565(0x8a, 0xa8, 0xd0), 1, 3) : lerp565(farC, C565(0xff, 0xff, 0xff), 2, 3);

  // distant island and sailboat sit on the horizon, hazed toward the sky
  uint16_t isl = lerp565(night ? C565(0x10, 0x20, 0x1c) : C565(0x3e, 0x72, 0x56), skyAt(SEA_TOP), 1, 3);
  gfx->fillCircle(408, SEA_TOP + 2, 24, isl);
  gfx->fillCircle(380, SEA_TOP + 4, 15, isl);
  if (!night) {
    int bob = (int)(sinf(now / 700.0f) * 1.5f);
    int bx = 172;
    gfx->fillTriangle(bx - 1, SEA_TOP - 22 + bob, bx - 1, SEA_TOP - 4 + bob, bx + 11, SEA_TOP - 4 + bob, C565(0xff, 0xff, 0xff));
    gfx->fillTriangle(bx - 3, SEA_TOP - 17 + bob, bx - 3, SEA_TOP - 4 + bob, bx - 13, SEA_TOP - 4 + bob, lerp565(C565(0xff, 0xff, 0xff), skyAt(SEA_TOP), 1, 4));
    gfx->fillRect(bx - 12, SEA_TOP - 3 + bob, 26, 3, C565(0x7a, 0x4a, 0x30));
  }

  // sea: depth bands, haze at the horizon
  const int ROWS = 6, RH = 11;
  for (int r = 0; r < ROWS; r++)
    gfx->fillRect(0, SEA_TOP + r * RH, 466, RH, lerp565(farC, nearC, r, ROWS - 1));
  gfx->fillRect(0, SEA_TOP, 466, 3, lerp565(skyAt(SEA_TOP), farC, 1, 2));

  // drifting crests: farther rows are slower and shorter (parallax)
  for (int r = 0; r < ROWS; r++) {
    int y0 = SEA_TOP + r * RH;
    for (int k = 0; k < 7; k++) {
      int x = ((k * 83 + r * 37) + (int)(now / (52 - r * 6))) % 540 - 40;
      int yy = y0 + 5 + (int)(sinf(now / 520.0f + k + r) * 2);
      int w = 10 + r * 5;
      gfx->fillRect(x, yy, w, 2, crest);
      gfx->fillRect(x + w / 3, yy + 2, w / 3, 1, lerp565(lerp565(farC, nearC, r, ROWS - 1), crest, 1, 2));
    }
  }

  // glitter path under the sun (or moon): flickering dashes that widen toward us
  int gx = sunset ? 233 : 360;
  uint16_t gl = night ? C565(0xd8, 0xe0, 0xf0) : C565(0xff, 0xf4, 0xd0);
  for (int r = 0; r < ROWS; r++)
    for (int k = 0; k < 4; k++) {
      if ((now / 140 + k * 2 + r * 3) % 4 == 0) continue;     // flicker
      int dx = (int)(sinf(now / 420.0f + k * 1.9f + r) * (5 + r * 5));
      int w = 4 + r * 2;
      gfx->fillRect(gx + dx - w / 2, SEA_TOP + r * RH + 2 + k * 2, w, 1, gl);
    }

  // sand, then the tide: per column the water reaches ye(x), wet sand just beyond
  gfx->fillRect(0, SEA_TOP + ROWS * RH, 466, 466 - (SEA_TOP + ROWS * RH), soil);
  uint16_t wet = lerp565(soil, night ? C565(0x08, 0x0c, 0x18) : C565(0x70, 0x5a, 0x3c), 1, 4);
  for (int x = 0; x < 466; x += 4) {
    int ye = 252 + (int)(sinf(now / 1100.0f + x / 46.0f) * 6);
    gfx->fillRect(x, SEA_TOP + ROWS * RH, 4, ye - (SEA_TOP + ROWS * RH), nearC);
    gfx->fillRect(x, ye, 4, 14, wet);
    gfx->fillRect(x, ye - 1, 4, 3, night ? C565(0xa8, 0xc0, 0xe0) : C565(0xff, 0xff, 0xff));      // foam line
    if (((x / 4) + (int)(now / 300)) % 3) gfx->fillRect(x, ye + 5, 4, 1, lerp565(wet, crest, 1, 2));  // thin trail
  }

  // a WINGULL crossing the sky by day, turning round at each edge: the same
  // triangle-wave beat as the walkers, with a slow bob. Its walk cycle is the
  // wingbeat, so the walk action doubles as flying.
  if (!night && !gBeachBirdTried) { gBeachBirdTried = true; gBeachBird.load(278, false); }
  if (!night && gBeachBird.loaded) {
    const uint32_t P = 17000;
    uint32_t u = (now + 5000) % (2 * P);
    bool right = u < P;
    uint32_t tri = right ? u : 2 * P - u;
    int x = -40 + (int)(540ULL * tri / P);
    int y = 156 + (int)(sinf(now / 900.0f) * 9.0f);
    uint8_t act = right ? PMD_WALKR : PMD_WALKL;
    if (!gBeachBird.has(act)) act = PMD_IDLE;
    drawPmdActZ(gBeachBird, act, x, y, now, true, false, 1, 140, 220);
  }

  // an EXEGGUTOR pacing the waterline, small and far, turning at each end of its
  // beat. Behind the pet, in front of the sea.
  if (!gBeachTried) { gBeachTried = true; gBeachMon.load(103, false); }
  if (gBeachMon.loaded) {
    static const struct { int16_t y; uint16_t zoom; uint32_t period; uint32_t phase; int16_t lo, hi; } WK[1] = {
      { 262, 125, 30000, 0, 50, 420 } };
    for (int i = 0; i < 1; i++) {
      uint32_t u = (now + WK[i].phase) % (2 * WK[i].period);
      bool right = u < WK[i].period;
      uint32_t tri = right ? u : 2 * WK[i].period - u;
      int x = WK[i].lo + (int)((WK[i].hi - WK[i].lo) * (uint64_t)tri / WK[i].period);
      uint8_t act = right ? PMD_WALKR : PMD_WALKL;
      if (!gBeachMon.has(act)) act = PMD_IDLE;
      drawPmdActZ(gBeachMon, act, x, WK[i].y, now, true, false, 1, WK[i].zoom, 220);
    }
  }

  // shells and a starfish on the dry sand, on the right where the pet is not
  gfx->fillCircle(366, 296, 5, night ? C565(0x70, 0x58, 0x60) : C565(0xf0, 0xb8, 0xb0));
  gfx->fillCircle(366, 296, 2, lerp565(soil, C565(0xff, 0xff, 0xff), 1, 3));
  uint16_t star = night ? C565(0x80, 0x48, 0x30) : C565(0xf0, 0x7a, 0x3a);
  for (int a = 0; a < 5; a++) {
    float an = a * 1.2566f - 1.5708f;
    gfx->fillTriangle(424, 292, 424 + (int)(cosf(an) * 11), 292 + (int)(sinf(an) * 11),
                      424 + (int)(cosf(an + 0.6f) * 4), 292 + (int)(sinf(an + 0.6f) * 4), star);
  }

  // gulls drifting across the sky by day, clear of the header text above y 120
  if (!night)
    for (int g = 0; g < 2; g++) {
      int x = (int)(now / 30 + g * 260) % 540 - 40;
      int y = 130 + g * 18 + (int)(sinf(now / 600.0f + g) * 4);
      int up = ((now / 200 + g) & 1) ? -4 : 2;
      uint16_t gc = lerp565(C565(0x30, 0x38, 0x48), skyAt(y), 1, 4);
      gfx->drawLine(x - 7, y + up, x, y, gc);
      gfx->drawLine(x, y, x + 7, y + up, gc);
    }
}

// Forest biome, back to front: two hazy tree lines, drifting mist, a small
// BEEDRILL and CATERPIE, two big trees on the right that sway, ferns and mushrooms on the ground, leaves
// drifting down by day and fireflies by night. The big trees stay right of the
// pet and below the sun and moon; the lower left is bare (poops land there, see
// drawPoops) and the step counter covers the far left.
// Tree-line layer: a mix of spiky pines and rounded leafy crowns, the kind and
// height fixed per x so nothing flickers. Crowns are two overlapping circles on
// a filled base; pines are single triangles.
static void forestRidge(int baseY, int step, int w, int hmin, int hmax, int seed, int x0, uint16_t col) {
  for (int x = x0; x < 480; x += step) {
    int hsh = (x * 37 + seed * 91) & 0x7fff;
    int ht = hmin + hsh % (hmax - hmin + 1);
    if ((hsh / 7) % 3 == 0) {                                // pine
      gfx->fillTriangle(x, baseY, x + w, baseY, x + w / 2, baseY - ht, col);
    } else {                                                 // leafy crown
      int r = ht * 2 / 5 + 4, cy = baseY - ht + r;
      gfx->fillCircle(x + w / 2 - r / 2, cy + r / 4, r, col);
      gfx->fillCircle(x + w / 2 + r / 2, cy, r - 1, col);
      gfx->fillRect(x + w / 2 - r, cy, 2 * r, baseY - cy, col);
    }
  }
}

// Leaf canopy tones, dark to bright, by day or night.
static void forestTones(bool night, uint16_t t[4]) {
  if (night) { t[0] = C565(0x08, 0x18, 0x12); t[1] = C565(0x0e, 0x26, 0x1a); t[2] = C565(0x16, 0x36, 0x24); t[3] = C565(0x20, 0x46, 0x30); }
  else       { t[0] = C565(0x24, 0x62, 0x38); t[1] = C565(0x38, 0x80, 0x44); t[2] = C565(0x5c, 0xa6, 0x4a); t[3] = C565(0x92, 0xcc, 0x58); }
}

// A leafy clump lit from the upper right: dark body, then mid, light and
// highlight discs stepping toward the sun, with a few leaf specks.
static void forestClump(int cx, int cy, int r, int seed, bool night) {
  uint16_t t[4]; forestTones(night, t);
  gfx->fillCircle(cx, cy, r, t[0]);
  gfx->fillCircle(cx + r / 8, cy - r / 8, r * 85 / 100, t[1]);
  gfx->fillCircle(cx + r / 4, cy - r / 4, r * 58 / 100, t[2]);
  gfx->fillCircle(cx + r * 3 / 8, cy - r * 3 / 8, r * 30 / 100, t[3]);
  for (int i = 0; i < 5; i++) {                              // specks of light and shade
    int hsh = (seed * 53 + i * 29) & 0xff;
    int dx = (hsh % (r + 1)) - r / 3, dy = ((hsh * 7) % (r + 1)) - r * 2 / 3;
    if (dx * dx + dy * dy < r * r * 7 / 10) gfx->fillRect(cx + dx, cy + dy, 2, 2, i & 1 ? t[3] : t[0]);
  }
}

// Broadleaf tree: flared roots, a barked trunk that forks, and a crown of
// clumps, with Oran berries in it. The whole crown sways; each clump also
// flutters a little on its own.
static void forestOak(int cx, int footY, uint32_t now, float sway, bool night) {
  uint16_t bark  = night ? C565(0x1c, 0x14, 0x12) : C565(0x5a, 0x3c, 0x28);
  uint16_t barkD = night ? C565(0x10, 0x0c, 0x0a) : C565(0x3c, 0x26, 0x1a);
  uint16_t barkL = night ? C565(0x2a, 0x1e, 0x1a) : C565(0x84, 0x5c, 0x3a);
  const int FORK = footY - 74;
  gfx->fillTriangle(cx - 16, footY + 2, cx - 6, footY - 16, cx - 6, footY + 2, bark);   // roots
  gfx->fillTriangle(cx + 16, footY + 2, cx + 6, footY - 16, cx + 6, footY + 2, bark);
  gfx->fillRect(cx - 7, FORK, 14, footY - FORK + 2, bark);
  gfx->fillRect(cx + 3, FORK, 4, footY - FORK + 2, barkL);                              // lit edge
  gfx->fillRect(cx - 7, FORK, 3, footY - FORK + 2, barkD);                              // shaded edge
  for (int i = 0; i < 5; i++)                                                           // bark grooves
    gfx->fillRect(cx - 2 + (i % 2) * 3, FORK + 8 + i * 13, 1, 9, barkD);
  int csx = (int)sway, ctop = FORK - 38;                                                // crown centre
  for (int k = 0; k < 2; k++) {                                                         // the two limbs
    int ex = cx + (k ? 24 : -22) + csx, ey = FORK - 26;
    for (int w = -1; w <= 1; w++) gfx->drawLine(cx + w, FORK + 2, ex + w, ey, bark);
  }
  static const int8_t CL[8][3] = { { -34, 26, 20 }, { 32, 28, 21 }, { -16, 6, 26 }, { 22, 4, 24 },
                                   { 0, -18, 24 }, { -40, 4, 16 }, { 38, 2, 16 }, { 0, 30, 20 } };
  for (int i = 0; i < 8; i++)                                                           // back to front, lower first
    forestClump(cx + CL[i][0] + csx + (int)(sinf(now / 1100.0f + i) * 1.5f),
                ctop + 28 + CL[i][1], CL[i][2], i + 3, night);
  uint16_t berry = night ? C565(0x20, 0x30, 0x70) : C565(0x4a, 0x78, 0xe0);
  static const int8_t BERRY[4][2] = { { -18, 18 }, { 14, 30 }, { 30, 14 }, { -4, 2 } };
  for (int i = 0; i < 4; i++) {
    int bx = cx + BERRY[i][0] + csx, by = ctop + 28 + BERRY[i][1];
    gfx->fillCircle(bx, by, 3, berry);
    gfx->fillRect(bx - 1, by - 1, 1, 1, night ? berry : C565(0xc8, 0xe0, 0xff));
  }
}

// A ragged pine: trunk plus four tiers with a jagged hem, three tones, the top
// tiers swaying most. The sun-facing (right) side is lit by day.
static void forestPine(int cx, int footY, int h, int w, float sway, bool night) {
  uint16_t trunk = night ? C565(0x20, 0x18, 0x14) : C565(0x5a, 0x3c, 0x28);
  uint16_t dark  = night ? C565(0x08, 0x14, 0x0e) : C565(0x1a, 0x4c, 0x32);
  uint16_t mid   = night ? C565(0x0c, 0x1e, 0x16) : C565(0x2a, 0x6c, 0x40);
  uint16_t lit   = night ? C565(0x12, 0x2c, 0x20) : C565(0x44, 0x92, 0x4c);
  int top = footY - 22 - h, tierH = h * 45 / 100, step = (h - tierH) / 3;
  gfx->fillRect(cx - 4, footY - 26, 8, 28, trunk);
  gfx->fillRect(cx + 1, footY - 26, 3, 28, night ? C565(0x2a, 0x1e, 0x1a) : C565(0x84, 0x5c, 0x3a));
  for (int i = 0; i < 4; i++) {
    int ay = top + i * step, by = ay + tierH;
    int hw = w * (45 + 18 * i) / 200;
    int sx = (int)(sway * (4 - i) / 4.0f);
    gfx->fillTriangle(cx + sx, ay, cx - hw, by, cx + hw, by, dark);
    gfx->fillTriangle(cx + sx, ay, cx + hw, by, cx - hw / 4, by, mid);
    gfx->fillTriangle(cx + sx, ay, cx + hw, by, cx + hw / 2, by, lit);
    for (int x = cx - hw; x < cx + hw; x += 7) {             // jagged hem: drooping boughs
      int t = x - cx + hw;
      uint16_t c = t > hw * 3 / 2 ? lit : (t > hw * 3 / 4 ? mid : dark);
      gfx->fillTriangle(x, by - 1, x + 7, by - 1, x + 3, by + 5 + (x & 3), c);
    }
  }
}

// A low bush: three clumps.
static void forestBush(int x, int y, int r, bool night) {
  forestClump(x - r * 2 / 3, y, r * 3 / 4, 1, night);
  forestClump(x + r * 2 / 3, y + 1, r * 3 / 4, 2, night);
  forestClump(x, y - r / 3, r, 3, night);
}

// A small BEEDRILL hovers and a CATERPIE crawls far behind the pet by day; a
// ZUBAT flutters about at night, swapped in as the hour changes. Each
// PMD sprite (~140 KB of PSRAM) is loaded once on entering the forest and freed
// when the biome changes.
static PmdMon gForestBee, gForestBug, gForestBat;   // bat: ZUBAT, night only
static bool gForestTried = false, gForestBatTried = false;

static void drawForest(uint32_t now, bool night, int h, uint16_t top, uint16_t bot, uint16_t soil) {
  auto skyAt = [&](int y) { return lerp565(top, bot, y < 0 ? 0 : y, HORIZON); };
  uint16_t farC = lerp565(night ? C565(0x10, 0x24, 0x2c) : C565(0x4a, 0x8c, 0x6c), skyAt(HORIZON - 40), 1, 2);
  uint16_t midC = lerp565(night ? C565(0x0c, 0x1c, 0x1c) : C565(0x2e, 0x6c, 0x4a), skyAt(HORIZON - 40), 1, 5);
  float sway = sinf(now / 1500.0f) * 3.0f;

  forestRidge(HORIZON + 2, 20, 30, 28, 50, 1, -10, farC);

  // mist: long soft streaks drifting slowly along the tree line
  uint16_t mist = lerp565(skyAt(HORIZON - 8), C565(0xff, 0xff, 0xff), 1, night ? 12 : 5);
  for (int k = 0; k < 4; k++) {
    int x = (int)((k * 170 + now / 70) % 640) - 140;
    gfx->fillRoundRect(x, HORIZON - 22 + (k & 1) * 12, 130, 9, 4, mist);
  }

  forestRidge(HORIZON + 2, 26, 38, 40, 72, 2, -4, midC);

  // the BEEDRILL drifts on a slow lissajous across the mid air, facing the way it
  // is heading; the CATERPIE crawls back and forth on the ground behind the ferns
  if (night) {                                               // day critters out, bat in
    if (gForestBee.loaded || gForestBug.loaded) { gForestBee.unload(); gForestBug.unload(); }
    gForestTried = false;
    if (!gForestBatTried) { gForestBatTried = true; gForestBat.load(41, false); }
  } else {
    if (gForestBat.loaded) gForestBat.unload();
    gForestBatTried = false;
  }
  if (night && gForestBat.loaded) {                          // erratic: two beats of different speed
    float a = now / 3100.0f, b = now / 1300.0f;
    int x = 190 + (int)(120.0f * sinf(a) + 30.0f * sinf(b * 1.7f));
    int y = 178 + (int)(22.0f * sinf(b) + 8.0f * sinf(a * 3.0f));
    uint8_t act = cosf(a) > 0 ? PMD_WALKR : PMD_WALKL;
    if (!gForestBat.has(act)) act = PMD_IDLE;
    drawPmdActZ(gForestBat, act, x, y, now, true, false, 1, 100, 220);
  }
  if (!night && !gForestTried) { gForestTried = true; gForestBee.load(15, false); gForestBug.load(10, false); }
  if (!night && gForestBug.loaded) {
    const uint32_t P = 26000;
    uint32_t u = now % (2 * P);
    bool right = u < P;
    uint32_t tri = right ? u : 2 * P - u;
    int x = 230 + (int)(150ULL * tri / P);
    uint8_t act = right ? PMD_WALKR : PMD_WALKL;
    if (!gForestBug.has(act)) act = PMD_IDLE;
    drawPmdActZ(gForestBug, act, x, 258, now, true, false, 2, 70, 220);
  }
  if (!night && gForestBee.loaded) {
    float ph = now / 5200.0f;
    int x = 190 + (int)(130.0f * sinf(ph));
    int y = 176 + (int)(16.0f * sinf(now / 1700.0f));
    uint8_t act = cosf(ph) > 0 ? PMD_WALKR : PMD_WALKL;
    if (!gForestBee.has(act)) act = PMD_IDLE;
    drawPmdActZ(gForestBee, act, x, y, now, true, false, 1, 110, 220);
  }

  // ferns and mushrooms, to the right of the pet
  uint16_t fern = night ? C565(0x16, 0x3a, 0x28) : C565(0x4c, 0xa0, 0x58);
  static const int16_t FN[3][2] = { { 306, 266 }, { 322, 288 }, { 338, 262 } };
  for (int f = 0; f < 3; f++)
    for (int b = -2; b <= 2; b++) {
      int ex = FN[f][0] + b * 7, ey = FN[f][1] - 16 + abs(b) * 4 + (int)(sway * 0.3f);
      gfx->drawLine(FN[f][0], FN[f][1], ex, ey, fern);
      gfx->drawLine(FN[f][0] + 1, FN[f][1], ex + 1, ey, fern);
    }
  uint16_t cap = night ? C565(0x70, 0x20, 0x28) : C565(0xe0, 0x44, 0x40);
  uint16_t stem = night ? C565(0x80, 0x78, 0x70) : C565(0xf4, 0xec, 0xdc);
  static const int16_t MU[3][3] = { { 360, 298, 7 }, { 376, 304, 5 }, { 346, 306, 4 } };
  for (int m = 0; m < 3; m++) {
    int x = MU[m][0], y = MU[m][1], r = MU[m][2];
    gfx->fillRect(x - r / 3, y - 2, r * 2 / 3 + 1, r, stem);
    gfx->fillRoundRect(x - r, y - r, r * 2, r + 1, r / 2 + 1, cap);
    gfx->fillRect(x - r / 2, y - r + 2, 2, 2, stem);
    gfx->fillRect(x + r / 3, y - r + 3, 2, 2, stem);
  }

  // the two big pines last, so they stand in front of everything else
  forestPine(384, 262, 66, 44, sway * 0.7f, night);
  forestOak(440, 272, now, sway, night);
  forestBush(404, 296, 13, night);

  if (!night) {  // leaves drifting down on a slow slant
    static const uint16_t LC[3] = { C565(0x6c, 0xb8, 0x4c), C565(0xd8, 0xc0, 0x40), C565(0xd8, 0x80, 0x38) };
    for (int i = 0; i < 6; i++) {
      int x = (i * 83 + (int)(now / 55) + (int)(sinf(now / 600.0f + i * 2.0f) * 12)) % 520 - 30;
      int y = (i * 47 + (int)(now / 30)) % 270 + 36;
      gfx->fillRect(x, y, 4, 2, LC[i % 3]);
      gfx->fillRect(x + 1, y + 2, 2, 1, LC[i % 3]);
    }
  } else {       // fireflies: bob about, each pulsing on its own beat
    uint16_t glow = C565(0xd8, 0xff, 0x70);
    for (int i = 0; i < 10; i++) {
      float pulse = (sinf(now / 350.0f + i * 2.0f) + 1.0f) * 0.5f;
      if (pulse < 0.25f) continue;
      int x = 40 + (i * 97) % 400 + (int)(sinf(now / 700.0f + i) * 14);
      int y = 170 + (i * 53) % 120 + (int)(cosf(now / 900.0f + i * 1.3f) * 10);
      gfx->fillRect(x - 1, y, 5, 3, lerp565(soil, glow, (int)(pulse * 4), 16));   // halo
      gfx->fillRect(x, y - 1, 3, 5, lerp565(soil, glow, (int)(pulse * 4), 16));
      gfx->fillRect(x, y, 3, 3, lerp565(soil, glow, (int)(pulse * 16), 16));
    }
  }
}

// Snow biome, drawn as the POLAR ICE: a flat pale sheet under a wide cold sky,
// a few faceted icebergs standing on it, one open lead, soft pressure ridges and
// cracks, and a thin wind-blown snowfall. At night a full aurora: swaying
// curtains of rays, bright at the base and fading green to cyan to violet,
// mirrored faintly in the ice. Deliberately sparse. Critters: a PIPLUP waddling
// slowly.
// Keep the lower left bare (poops land there, see drawPoops) and everything below
// the sun and moon.

// scanline ellipse (the emulator's GFX has no fillEllipse, and 1 px rows are cheap here)
static void snowEll(int cx, int cy, int rx, int ry, uint16_t c) {
  for (int dy = -ry; dy <= ry; dy++) {
    int w = (int)(rx * sqrtf(1.0f - (float)(dy * dy) / (float)(ry * ry)));
    if (w > 0) gfx->fillRect(cx - w, cy + dy, 2 * w, 1, c);
  }
}

// An iceberg: a fan of seven facets around a centre point, dark on the left to
// bright on the lit right, a darker waterline band and a turquoise glow where it
// meets the ice. 'haze' (0..8) fades it toward the sky for the distant ones.
static void iceberg(int cx, int baseY, int w, int h, int haze, uint16_t sky, uint16_t soil, bool night) {
  static const int8_t P[8][2] = {                              // x, y as percent of w, h (y up)
    { -50, 0 }, { -34, 50 }, { -12, 68 }, { 8, 100 }, { 30, 62 }, { 50, 0 }, { 0, 0 }, { -5, 25 } };
  uint16_t tone[4];
  if (night) { tone[0] = C565(0x20, 0x30, 0x58); tone[1] = C565(0x30, 0x44, 0x74); tone[2] = C565(0x44, 0x5c, 0x90); tone[3] = C565(0x64, 0x80, 0xb4); }
  else       { tone[0] = C565(0x8c, 0xb0, 0xd4); tone[1] = C565(0xb4, 0xd0, 0xe8); tone[2] = C565(0xd8, 0xea, 0xf6); tone[3] = C565(0xff, 0xff, 0xff); }
  for (int i = 0; i < 4; i++) tone[i] = lerp565(tone[i], sky, haze, 10);
  auto X = [&](int i) { return cx + P[i][0] * w / 100; };
  auto Y = [&](int i) { return baseY - P[i][1] * h / 100; };
  static const uint8_t FACE[7][3] = { { 7, 0, 1 }, { 7, 1, 2 }, { 7, 2, 3 }, { 7, 3, 4 }, { 7, 4, 5 }, { 7, 5, 6 }, { 7, 6, 0 } };
  static const uint8_t TONE[7] = { 0, 1, 2, 3, 2, 1, 0 };
  snowEll(cx, baseY + 2, w * 55 / 100, 4, lerp565(soil, night ? C565(0x30, 0x60, 0x90) : C565(0x70, 0xc8, 0xe0), 1, 3 + haze / 2));   // glow in the ice
  for (int f = 0; f < 7; f++)
    gfx->fillTriangle(X(FACE[f][0]), Y(FACE[f][0]), X(FACE[f][1]), Y(FACE[f][1]), X(FACE[f][2]), Y(FACE[f][2]), tone[TONE[f]]);
  gfx->fillRect(cx - w / 2, baseY - 2, w, 3, lerp565(tone[0], night ? C565(0x10, 0x20, 0x50) : C565(0x40, 0x78, 0xb0), 1, 2));   // waterline band
}

// Aurora: swaying curtains of vertical rays. Each column has its own base height,
// ray length and brightness, all moving; rays are brightest at the base, fade
// upward, and shift green -> cyan -> violet. Two layers at different phases give
// depth. Kept clear of the moon by dimming near it. Returns nothing; also tints
// the ice faintly through 'glowAt' so the sky seems to light the ground.
static void drawAurora(uint32_t now, uint16_t top, uint16_t bot, int moonX, int moonY) {
  float t = now / 1000.0f;
  for (int layer = 0; layer < 2; layer++)
    for (int x = 12; x < 454; x += 4) {
      float ph = layer * 1.9f;
      int base = 118 + layer * 14 + (int)(20.0f * sinf(x / 71.0f + t * 0.35f + ph) + 9.0f * sinf(x / 23.0f - t * 0.9f + ph));
      int len = 34 + (int)(24.0f * sinf(x / 43.0f + t * 0.5f + ph) + 12.0f * sinf(x / 15.0f + t * 1.3f));
      float br = 0.55f + 0.45f * sinf(x / 19.0f + t * 1.1f + ph * 2.0f);          // flicker along the curtain
      int dx = x - moonX, dy = base - len / 2 - moonY;
      float d2 = (float)(dx * dx + dy * dy);
      if (d2 < 3600.0f) continue;                                                    // inside 60 px of the moon: leave it alone
      if (d2 < 10000.0f) br *= (d2 - 3600.0f) / 6400.0f;                             // fade in out to 100 px
      if (len < 8 || br < 0.12f) continue;
      for (int j = 0; j < len; j += 6) {
        float f = (float)j / len;                                                    // 0 at the base, 1 at the tip
        int a = (int)(br * (1.0f - f) * (1.0f - f * 0.3f) * 20.0f) + 1;              // brighter at the base
        uint16_t c = f < 0.5f ? lerp565(C565(0x40, 0xff, 0x98), C565(0x40, 0xd8, 0xff), (int)(f * 20), 10)
                              : lerp565(C565(0x40, 0xd8, 0xff), C565(0xb0, 0x60, 0xff), (int)((f - 0.5f) * 20), 10);
        int y = base - j - 6;
        if (y < 18) break;
        gfx->fillRect(x, y, 4, 6, lerp565(lerp565(top, bot, y, HORIZON), c, a, 28));
      }
    }
}

// small walker (PIPLUP, or SEEL without the Sinnoh pack). The sprite is resident
// only while the biome is on screen (~110 KB of PSRAM).
static PmdMon gSnowMon;
static int16_t gSnowMonDex = 0;

static void drawSnow(uint32_t now, bool night, int h, uint16_t top, uint16_t bot, uint16_t soil) {
  auto skyAt = [&](int y) { return lerp565(top, bot, y < 0 ? 0 : y, HORIZON); };
  const int B = HORIZON + 2;
  uint16_t sW = night ? C565(0x98, 0xaa, 0xd0) : C565(0xf6, 0xfb, 0xff);
  uint16_t haze = skyAt(HORIZON - 30);

  if (night) drawAurora(now, top, bot, 360, 122);

  // distant icebergs on the horizon, hazed toward the sky
  iceberg(92, B, 120, 62, 3, haze, soil, night);
  iceberg(300, B, 80, 40, 4, haze, soil, night);
  gfx->fillRect(0, B - 1, 466, 3, lerp565(soil, haze, 1, 3));            // soft horizon line

  // the ice sheet: faint aurora glow in it at night, soft ridges, cracks, one open lead
  if (night) {
    float t = now / 1000.0f;
    for (int x = 0; x < 466; x += 8) {
      int a = 1 + (int)((sinf(x / 40.0f + t * 0.5f) + 1.0f) * 2.5f);
      gfx->fillRect(x, B + 2, 8, 22, lerp565(soil, C565(0x40, 0xe0, 0x98), a, 40));
    }
  }
  uint16_t shade = night ? C565(0x30, 0x40, 0x70) : C565(0xc0, 0xd4, 0xea);
  uint16_t lite = night ? C565(0x78, 0x8c, 0xb8) : C565(0xff, 0xff, 0xff);
  snowEll(240, 256, 84, 4, shade);                                      // two long, low pressure ridges
  snowEll(244, 254, 80, 3, lite);
  snowEll(150, 284, 60, 3, shade);
  snowEll(153, 283, 56, 2, lite);
  uint16_t crack = night ? C565(0x28, 0x38, 0x68) : C565(0x88, 0xa8, 0xd0);
  static const int16_t CK[2][6][2] = { { { 296, 246 }, { 312, 258 }, { 306, 270 }, { 330, 282 }, { 326, 296 }, { -1, 0 } },
                                       { { 100, 248 }, { 118, 256 }, { 112, 266 }, { -1, 0 } } };
  for (int c = 0; c < 2; c++)
    for (int i = 0; CK[c][i + 1][0] >= 0; i++)
      gfx->drawLine(CK[c][i][0], CK[c][i][1], CK[c][i + 1][0], CK[c][i + 1][1], crack);
  uint16_t water = night ? C565(0x10, 0x20, 0x4c) : C565(0x3c, 0x6c, 0x9c);   // the open lead, ice-rimmed
  snowEll(398, 272, 62, 6, night ? C565(0x70, 0x84, 0xb0) : C565(0xe8, 0xf2, 0xfa));
  snowEll(398, 272, 56, 4, water);
  snowEll(404, 271, 28, 1, lerp565(water, sW, 1, 3));                    // sky reflected in it
  iceberg(418, 276, 56, 42, 1, haze, soil, night);                       // one nearer berg beside it

  // sparkles on the ice: a few fixed spots, each twinkling on its own beat
  for (int i = 0; i < 8; i++) {
    int x = 130 + volNoise(i, 23) % 320, y = 244 + volNoise(i, 24) % 54;
    if (x < 200 && y > 262) continue;                        // poops land bottom left
    if ((now / 260 + i * 3) % 8 != 0) continue;
    gfx->fillRect(x, y, 2, 2, sW);
    gfx->fillRect(x - 2, y, 6, 1, lerp565(soil, sW, 1, 2));
    gfx->fillRect(x, y - 2, 1, 6, lerp565(soil, sW, 1, 2));
  }

  // the walker: a PIPLUP waddling slowly behind the pet at any hour (SEEL
  // instead if the Sinnoh pack is not on the card, so it is not left empty)
  if (!gSnowMonDex) {
    gSnowMonDex = 393;
    if (!gSnowMon.load(393, false)) { gSnowMonDex = 86; gSnowMon.load(86, false); }
  }
  if (gSnowMon.loaded) {
    const uint32_t P = 30000;
    uint32_t u = now % (2 * P);
    bool right = u < P;
    uint32_t tri = right ? u : 2 * P - u;
    int x = 250 + (int)(80ULL * tri / P);
    uint8_t act = right ? PMD_WALKR : PMD_WALKL;
    if (!gSnowMon.has(act)) act = PMD_IDLE;
    drawPmdActZ(gSnowMon, act, x, 262, now, true, false, 2, 70, 220);
  }

  // heavy snowfall in three depths, blown sideways by the wind: many small far
  // flakes, fewer big near ones, each with its own speed and a little wobble
  for (int d = 0; d < 3; d++) {
    int n = 46 - d * 12, sz = 2 + d, drift = 22 - d * 5, fall = 60 + d * 25;
    for (int i = 0; i < n; i++) {
      int x = (i * 71 + d * 37 + 466 + (int)(now / drift) + (int)(sinf(now / 700.0f + i) * 5.0f)) % 466;
      int y = (i * 53 + d * 29 + (int)(now / (38 - d * 9 - i % 3 * 3))) % (HORIZON + fall) - 10;
      if (night) { gfx->fillRect(x, y, sz, sz, d == 0 ? lerp565(skyAt(y), sW, 2, 3) : sW); continue; }
      uint16_t tint = C565(0xa8, 0xc4, 0xe4);                // by day white-on-pale is invisible: tint, plus a shadow
      if (d == 0) { gfx->fillRect(x, y, sz, sz, lerp565(skyAt(y), tint, 2, 3)); continue; }
      gfx->fillRect(x + 1, y + 1, sz, sz, tint);
      gfx->fillRect(x, y, sz, sz, C565(0xff, 0xff, 0xff));
    }
  }
}

// Meadow biome: an open flower meadow of tall grass in the wind. Layered rolling
// hills behind; in front, seven rows of long curved blades whose tips sway in a
// wave that travels across the field (brighter where it crests, stronger in
// gusts), with wildflowers standing above the grass and seed fluff drifting.
// Day: a small BUTTERFREE overhead and a RATTATA scurrying through the grass;
// night: fireflies and a VENOMOTH, everything darker. Kept calm. The lower left
// stays short and bare (poops land there, see drawPoops), the sun and moon clear.

// small residents: a flyer (BUTTERFREE by day, VENOMOTH at night) and a RATTATA
// by day, each loaded only while wanted and freed when the biome changes.
static PmdMon gMeadowFly, gMeadowWalk;
static int16_t gMeadowFlyDex = 0;
static bool gMeadowWalkTried = false;

static void drawMeadow(uint32_t now, bool night, int h, uint16_t top, uint16_t bot, uint16_t soil) {
  auto skyAt = [&](int y) { return lerp565(top, bot, y < 0 ? 0 : y, HORIZON); };
  const int B = HORIZON + 2;
  float gust = 1.0f + 0.6f * sinf(now / 3300.0f);             // 0.4..1.6: the wind comes and goes

  // layered hills: pale and hazy far away, richer green near, tiny bushes on the middle one
  uint16_t farH = lerp565(night ? C565(0x18, 0x34, 0x34) : C565(0x6c, 0xaa, 0x8c), skyAt(HORIZON - 30), 1, 3);
  uint16_t midH = night ? C565(0x14, 0x38, 0x2a) : C565(0x4c, 0x98, 0x58);
  snowEll(70, B, 150, 26, farH);
  snowEll(330, B, 170, 22, farH);
  snowEll(455, B, 100, 30, farH);
  snowEll(190, B + 2, 165, 18, midH);
  snowEll(425, B + 2, 125, 20, midH);
  forestBush(150, B - 14, 9, night);
  forestBush(322, B - 12, 8, night);
  forestBush(262, B - 8, 6, night);

  // residents: the flyer by day/night (the rattata is drawn mid-grass below)
  int16_t wantFly = night ? 49 : 12;
  if (gMeadowFlyDex != wantFly) { gMeadowFly.unload(); gMeadowFlyDex = wantFly; gMeadowFly.load(wantFly, false); }
  if (gMeadowFly.loaded) {
    float ph = now / 6200.0f;
    int x = 200 + (int)(120.0f * sinf(ph)), y = 188 + (int)(14.0f * sinf(now / 1500.0f));
    uint8_t act = cosf(ph) > 0 ? PMD_WALKR : PMD_WALKL;
    if (!gMeadowFly.has(act)) act = PMD_IDLE;
    drawPmdActZ(gMeadowFly, act, x, y, now, true, false, 1, 110, 220);
  }
  if (night) { if (gMeadowWalk.loaded) gMeadowWalk.unload(); gMeadowWalkTried = false; }
  else if (!gMeadowWalkTried) { gMeadowWalkTried = true; gMeadowWalk.load(19, false); }

  // grass tones, dark to bright, and the flower palette (day / night)
  uint16_t gT[4];
  if (night) { gT[0] = C565(0x0e, 0x24, 0x1a); gT[1] = C565(0x18, 0x38, 0x28); gT[2] = C565(0x26, 0x50, 0x36); gT[3] = C565(0x5a, 0x7c, 0x5a); }
  else       { gT[0] = C565(0x34, 0x7c, 0x3c); gT[1] = C565(0x4c, 0x9c, 0x46); gT[2] = C565(0x70, 0xbc, 0x54); gT[3] = C565(0xdc, 0xf2, 0x8c); }
  static const uint16_t FC[2][4] = {
    { C565(0xff, 0xff, 0xff), C565(0xe8, 0x40, 0x3c), C565(0x58, 0x88, 0xf0), C565(0xff, 0xd8, 0x3c) },
    { C565(0xa0, 0xa8, 0xc0), C565(0x80, 0x34, 0x40), C565(0x34, 0x4c, 0x90), C565(0xa0, 0x90, 0x40) } };
  uint16_t lav = night ? C565(0x58, 0x40, 0x80) : C565(0xa8, 0x70, 0xe0);

  // seven rows of grass, back to front, each with the flowers that stand in it
  for (int r = 0; r < 7; r++) {
    int y0 = 238 + r * 11, len = 10 + r * 4, sp = 9 + r / 2;
    float amp = 2.0f + r * 0.8f;
    gfx->fillRect(0, y0 - 2, 466, 8, lerp565(soil, gT[0], 1, 3));        // shadowed root band under the blades
    if (r == 3 && gMeadowWalk.loaded && !night) {                         // the rattata runs through the middle of the grass
      const uint32_t P = 22000;
      uint32_t u = now % (2 * P);
      bool right = u < P;
      uint32_t tri = right ? u : 2 * P - u;
      int rx = 236 + (int)(90ULL * tri / P);
      uint8_t act = right ? PMD_WALKR : PMD_WALKL;
      if (!gMeadowWalk.has(act)) act = PMD_IDLE;
      drawPmdActZ(gMeadowWalk, act, rx, 262, now, true, false, 2, 70, 220);
    }
    for (int i = -1; i * sp < 480; i++) {
      int x = i * sp + volNoise(i + r * 17, 45) % sp;
      if (x < 170 && y0 > 262) continue;                                  // keep the poop corner short and bare
      float wv = sinf(now / 560.0f + x / 48.0f + r * 0.5f);              // the travelling wave
      int off = (int)(wv * amp * gust) + volNoise(i + r * 7, 46) % 5 - 2;
      int bl = len - volNoise(i + r * 11, 47) % (len / 3 + 1);
      int mx = x + off * 2 / 5, my = y0 - bl * 55 / 100, tx = x + off, ty = y0 - bl;
      uint16_t c = gT[volNoise(i + r * 5, 48) % 3];
      uint16_t tip = (wv * gust > 0.8f) ? gT[3] : lerp565(c, gT[3], 1, 4); // crests glint
      gfx->drawLine(x, y0 + 4, mx, my, c);
      gfx->drawLine(mx, my, tx, ty, tip);
      if (r >= 3) { gfx->drawLine(x + 1, y0 + 4, mx + 1, my, c); gfx->drawLine(mx + 1, my, tx + 1, ty, tip); }
    }
    for (int f = 0; f < 44; f++) {                                       // wildflowers rooted in this row
      int fy = 240 + volNoise(f, 42) % 66;
      if (fy < y0 || fy >= y0 + 11) continue;
      int fx = 14 + volNoise(f, 43) % 440;
      if (fx < 170 && fy > 262) continue;
      float wv = sinf(now / 560.0f + fx / 48.0f + r * 0.5f);
      int sw = (int)(wv * (amp + 1.0f) * gust), fl = len + 8 + volNoise(f, 49) % 8;
      int hx = fx + sw, hy = fy - fl, kind = volNoise(f, 44) % 6;
      gfx->drawLine(fx, fy + 2, fx + sw / 2, fy - fl / 2, gT[1]);
      gfx->drawLine(fx + sw / 2, fy - fl / 2, hx, hy, gT[1]);
      if (kind == 5) {                                                    // lavender: a tall violet spike
        gfx->fillRect(hx - 1, hy - 7, 3, 9, lav);
        gfx->fillRect(hx - 2, hy - 4, 5, 2, lav);
      } else {
        uint16_t fc = FC[night][kind % 4];
        if (kind == 1) { gfx->fillRect(hx - 3, hy - 2, 7, 5, fc); gfx->fillRect(hx - 1, hy - 1, 3, 3, night ? C565(0x20, 0x10, 0x14) : C565(0x30, 0x20, 0x20)); }   // poppy
        else if (kind == 3) { gfx->fillRect(hx - 2, hy - 2, 5, 5, fc); gfx->fillRect(hx - 1, hy - 2, 2, 1, C565(0xff, 0xf4, 0xa0)); }                              // buttercup
        else { gfx->fillRect(hx - 3, hy - 1, 7, 3, fc); gfx->fillRect(hx - 1, hy - 3, 3, 7, fc); gfx->fillRect(hx - 1, hy - 1, 3, 3, night ? C565(0x80, 0x70, 0x30) : C565(0xff, 0xc0, 0x20)); }   // daisy / cornflower
      }
    }
  }

  if (!night) {  // seed fluff and pollen carried along on the wind
    for (int i = 0; i < 10; i++) {
      int x = (i * 53 + (int)(now / 14) + (int)(sinf(now / 600.0f + i) * 6.0f)) % 520 - 30;
      int y = 190 + (i * 11) % 90 + (int)(sinf(now / 450.0f + i * 1.7f) * 5.0f);
      gfx->fillRect(x, y, 2, 2, C565(0xff, 0xf6, 0xd0));
      gfx->fillRect(x + 2, y - 1, 1, 1, C565(0xff, 0xff, 0xff));
    }
  } else {       // fireflies: bob about, each pulsing on its own beat
    uint16_t glow = C565(0xd8, 0xff, 0x70);
    for (int i = 0; i < 10; i++) {
      float pulse = (sinf(now / 350.0f + i * 2.0f) + 1.0f) * 0.5f;
      if (pulse < 0.25f) continue;
      int x = 40 + (i * 97) % 400 + (int)(sinf(now / 700.0f + i) * 14);
      int y = 200 + (i * 53) % 100 + (int)(cosf(now / 900.0f + i * 1.3f) * 10);
      gfx->fillRect(x - 1, y, 5, 3, lerp565(soil, glow, (int)(pulse * 4), 16));
      gfx->fillRect(x, y - 1, 3, 5, lerp565(soil, glow, (int)(pulse * 4), 16));
      gfx->fillRect(x, y, 3, 3, lerp565(soil, glow, (int)(pulse * 16), 16));
    }
  }
}

// Mountain biome: high alpine country. Back to front: a hazy range, a mid range,
// one big near peak on the left (all faceted fans with snow caps and ragged snow
// hems, lit from the right), drifting fog banks, a rocky cliff on the right with
// a waterfall into a misty pool, one small pine, boulders and scree. Day: a
// FEAROW soaring and a GEODUDE trundling behind the pet; night: a GOLBAT and a
// CUBONE. The peaks stay below the sun and moon; the lower left is bare (poops
// land there, see drawPoops).

// A peak: a fan of eight facets around a centre point, dark on the left to bright
// on the lit right, then a snow cap (shaded left, lit right) with a ragged hem.
// 'haze' (0..10) fades it toward the sky for the far ones; snowPct is how far down
// the cap reaches.
static void mtnPeak(int cx, int baseY, int w, int H, int seed, int haze, uint16_t sky, int snowPct, bool night) {
  static const int8_t V[8][2] = { { -100, 0 }, { -52, 46 }, { -26, 70 }, { 0, 100 }, { 24, 72 }, { 50, 44 }, { 100, 0 }, { 12, 0 } };
  uint16_t tone[4], sW, sS;
  if (night) { tone[0] = C565(0x14, 0x18, 0x2c); tone[1] = C565(0x1e, 0x24, 0x40); tone[2] = C565(0x2c, 0x34, 0x54); tone[3] = C565(0x40, 0x4a, 0x70); sW = C565(0xa0, 0xb0, 0xd8); sS = C565(0x5c, 0x6c, 0x9c); }
  else       { tone[0] = C565(0x4a, 0x4e, 0x6a); tone[1] = C565(0x6a, 0x6e, 0x86); tone[2] = C565(0x8a, 0x8e, 0xa4); tone[3] = C565(0xb4, 0xb6, 0xc8); sW = C565(0xf8, 0xfc, 0xff); sS = C565(0xc0, 0xd0, 0xea); }
  for (int i = 0; i < 4; i++) tone[i] = lerp565(tone[i], sky, haze, 10);
  sW = lerp565(sW, sky, haze, 12); sS = lerp565(sS, sky, haze, 12);
  int px[8], py[8];
  for (int i = 0; i < 8; i++) {
    int jx = volNoise(i, seed) % 9 - 4, jy = (i == 0 || i == 6 || i == 7) ? 0 : volNoise(i + 9, seed) % 7 - 3;
    px[i] = cx + (V[i][0] + jx) * w / 100;
    py[i] = baseY - (V[i][1] + jy) * H / 100;
  }
  int ccx = cx, ccy = baseY - 34 * H / 100;
  static const uint8_t TN[8] = { 0, 1, 2, 3, 2, 1, 0, 0 };         // tone of the facet starting at vertex i
  for (int i = 0; i < 8; i++) {
    int j = (i + 1) & 7;
    gfx->fillTriangle(ccx, ccy, px[i], py[i], px[j], py[j], tone[TN[i]]);
  }
  // snow cap, a fraction of the way down each ridge from the summit
  auto lx = [&](int a, int b) { return px[3] + (px[a] - px[3]) * snowPct / 100; };
  auto ly = [&](int a, int b) { return py[3] + (py[a] - py[3]) * snowPct / 100; };
  int sx[5] = { lx(1, 0), lx(2, 0), px[3], lx(4, 0), lx(5, 0) };    // lower edge of the cap, left to right
  int sy[5] = { ly(1, 0), ly(2, 0), py[3] + (baseY - py[3]) * snowPct / 160, ly(4, 0), ly(5, 0) };
  gfx->fillTriangle(px[3], py[3], sx[0], sy[0], sx[1], sy[1], sS);
  gfx->fillTriangle(px[3], py[3], sx[1], sy[1], sx[2], sy[2], sS);
  gfx->fillTriangle(px[3], py[3], sx[2], sy[2], sx[3], sy[3], sW);
  gfx->fillTriangle(px[3], py[3], sx[3], sy[3], sx[4], sy[4], sW);
  for (int k = 0; k < 4; k++)                                        // ragged hem: teeth hanging off the lower edge
    for (int t = 0; t < 3; t++) {
      int ax = sx[k] + (sx[k + 1] - sx[k]) * t / 3, ay = sy[k] + (sy[k + 1] - sy[k]) * t / 3;
      int bx = sx[k] + (sx[k + 1] - sx[k]) * (t + 1) / 3, by = sy[k] + (sy[k + 1] - sy[k]) * (t + 1) / 3;
      gfx->fillTriangle(ax, ay, bx, by, (ax + bx) / 2, (ay + by) / 2 + 3 + volNoise(k * 3 + t, seed + 3) % 6, k < 2 ? sS : sW);
    }
}

// A grey boulder with a lit upper right, shaded lower left, and a tuft on top.
static void mtnBoulder(int cx, int cy, int r, int seed, bool night) {
  uint16_t t[4];
  if (night) { t[0] = C565(0x18, 0x1c, 0x2c); t[1] = C565(0x28, 0x2e, 0x44); t[2] = C565(0x3a, 0x42, 0x5c); t[3] = C565(0x50, 0x5a, 0x78); }
  else       { t[0] = C565(0x5c, 0x58, 0x5c); t[1] = C565(0x7c, 0x78, 0x78); t[2] = C565(0x9c, 0x96, 0x92); t[3] = C565(0xc0, 0xb8, 0xae); }
  const int N = 8;
  int vx[N + 1], vy[N + 1];
  for (int i = 0; i < N; i++) {
    float an = i * 6.2832f / N;
    int rr = r * (80 + volNoise(i, seed) % 26) / 100;
    vx[i] = cx + (int)(cosf(an) * rr * 1.25f);
    vy[i] = cy + (int)(sinf(an) * rr * 0.8f);
  }
  vx[N] = vx[0]; vy[N] = vy[0];
  for (int i = 0; i < N; i++) {
    float an = (i + 0.5f) * 6.2832f / N, lit = cosf(an) * 0.6f - sinf(an) * 0.8f;
    gfx->fillTriangle(cx, cy - r / 6, vx[i], vy[i], vx[i + 1], vy[i + 1], lit > 0.75f ? t[3] : (lit > 0.2f ? t[2] : (lit > -0.4f ? t[1] : t[0])));
  }
  uint16_t g = night ? C565(0x1c, 0x3a, 0x2c) : C565(0x58, 0x9c, 0x4c);                // moss on top
  gfx->fillRect(cx - r / 3, cy - r * 4 / 5, r * 2 / 3, 2, g);
  gfx->fillRect(cx - r / 4, cy - r * 4 / 5 - 2, r / 2, 2, g);
}

static PmdMon gMtnFly, gMtnWalk;
static int16_t gMtnDay = -1;   // which set is loaded: 1 day, 0 night, -1 none

static void drawMountain(uint32_t now, bool night, int h, uint16_t top, uint16_t bot, uint16_t soil) {
  auto skyAt = [&](int y) { return lerp565(top, bot, y < 0 ? 0 : y, HORIZON); };
  const int B = HORIZON + 2;
  float sway = sinf(now / 1500.0f) * 3.0f;
  uint16_t sW = night ? C565(0xa0, 0xb0, 0xd8) : C565(0xf8, 0xfc, 0xff);

  // three ranges, back to front: hazy and low, mid, then the big near peak on the left
  uint16_t hz = skyAt(HORIZON - 50);
  for (int i = 0; i < 6; i++)
    mtnPeak(10 + i * 88, B, 56 + volNoise(i, 61) % 30, 46 + volNoise(i, 62) % 34, 61 + i, 7, hz, 42, night);
  mtnPeak(60, B, 110, 78, 71, 4, hz, 40, night);
  mtnPeak(300, B, 90, 52, 72, 4, hz, 44, night);                 // below the sun and moon
  mtnPeak(430, B, 80, 48, 73, 4, hz, 44, night);
  mtnPeak(172, B, 124, 118, 81, 0, hz, 44, night);                // the big one, lit from the right

  // fog banks drifting across the ranges
  uint16_t fog = lerp565(skyAt(HORIZON - 20), C565(0xff, 0xff, 0xff), 1, night ? 9 : 4);
  for (int k = 0; k < 3; k++) {
    int x = (int)((k * 190 + now / 90) % 640) - 150;
    snowEll(x, HORIZON - 18 + k * 9, 100 + k * 14, 4, fog);
    snowEll(x + 30, HORIZON - 21 + k * 9, 60, 3, fog);
  }

  // the cliff on the right: ragged-edged strata, shaded left edge, lit right side, grass on top
  uint16_t rc[4];
  if (night) { rc[0] = C565(0x14, 0x18, 0x28); rc[1] = C565(0x20, 0x26, 0x3c); rc[2] = C565(0x2e, 0x36, 0x50); rc[3] = C565(0x42, 0x4c, 0x6c); }
  else       { rc[0] = C565(0x4c, 0x44, 0x48); rc[1] = C565(0x6c, 0x62, 0x60); rc[2] = C565(0x88, 0x7c, 0x74); rc[3] = C565(0xa8, 0x98, 0x88); }
  const int CT = 178, CB = 266;                                     // cliff top and foot
  for (int y = CT; y < CB; y += 2) {
    int xl = 376 + volNoise(y / 4, 63) % 9 - (y - CT) / 14;          // ragged, flaring out toward the foot
    int band = (y / 12 + volNoise(y / 12, 64) % 2) % 3;               // strata
    gfx->fillRect(xl, y, 12, 2, rc[0]);
    gfx->fillRect(xl + 12, y, 466 - xl - 12, 2, rc[band == 2 ? 2 : 1]);
    gfx->fillRect(430, y, 40, 2, rc[band == 0 ? 2 : 3]);             // the lit side
    if (volNoise(y, 65) < 50) gfx->fillRect(xl + 14 + volNoise(y, 66) % 40, y, 3, 2, rc[0]);   // cracks and pits
  }
  uint16_t gTop = night ? C565(0x1c, 0x3c, 0x2a) : C565(0x58, 0xa0, 0x4c);
  uint16_t gTopL = night ? C565(0x2a, 0x54, 0x38) : C565(0x86, 0xcc, 0x58);
  gfx->fillRect(372, CT - 5, 100, 6, gTop);
  gfx->fillRect(420, CT - 5, 50, 3, gTopL);
  for (int x = 374; x < 466; x += 6)                                 // grass hanging over the edge
    gfx->fillTriangle(x, CT, x + 6, CT, x + 3, CT + 4 + volNoise(x, 67) % 5, gTop);

  // the waterfall: a pale ribbon from a notch in the lip to a misty pool, with bright
  // streaks racing down it and spray rising at the foot
  const int WX = 414;
  uint16_t wt = night ? C565(0x70, 0x90, 0xc8) : C565(0xb8, 0xdc, 0xf4);
  gfx->fillRect(WX - 5, CT - 3, 10, 6, lerp565(rc[0], wt, 1, 2));    // the notch
  for (int y = CT + 2; y < CB + 6; y += 2) {
    int wob = (int)(sinf(y / 14.0f + now / 500.0f) * 1.0f);
    gfx->fillRect(WX - 4 + wob, y, 8, 2, wt);
    gfx->fillRect(WX + 2 + wob, y, 2, 2, lerp565(wt, C565(0x30, 0x58, 0x88), 1, 4));   // shaded edge
  }
  int wlen = CB - CT;
  for (int k = 0; k < 5; k++) {
    int yy = CT + 4 + (int)((now / 9 + k * 37) % wlen);
    gfx->fillRect(WX - 3 + (k * 3) % 5, yy, 2, 8, C565(0xff, 0xff, 0xff));
  }
  snowEll(WX + 4, 270, 46, 8, night ? C565(0x3a, 0x4a, 0x70) : C565(0x78, 0x9c, 0xc0));            // pool rim
  snowEll(WX + 4, 270, 42, 6, night ? C565(0x1a, 0x2c, 0x58) : C565(0x4c, 0x80, 0xb4));            // water
  for (int k = 0; k < 3; k++) {                                                                      // ripples spreading out from the splash
    int r = (int)((now / 40 + k * 14) % 42);
    gfx->fillRect(WX + 4 - r, 270, 1, 1, lerp565(C565(0x4c, 0x80, 0xb4), sW, 2, 3));
    gfx->fillRect(WX + 4 + r, 270, 1, 1, lerp565(C565(0x4c, 0x80, 0xb4), sW, 2, 3));
  }
  for (int k = 0; k < 6; k++) {                                                                      // spray: puffs rising and thinning
    int life = (int)((now / 26 + k * 40) % 120);
    int px = WX + (int)(sinf(k * 2.1f + now / 400.0f) * (8 + life / 8)), py = 266 - life / 3;
    gfx->fillCircle(px, py, 3 + life / 24, lerp565(skyAt(py), C565(0xff, 0xff, 0xff), 8 - life / 18, 14));
  }

  // an alpine pine at the cliff foot, boulders around it; tufts and scree on the ground
  forestPine(380, 270, 52, 34, sway * 0.6f, night);
  mtnBoulder(352, 292, 12, 1, night);
  mtnBoulder(300, 300, 10, 2, night);
  mtnBoulder(460, 296, 11, 3, night);
  uint16_t gD = night ? C565(0x14, 0x30, 0x24) : C565(0x4c, 0x86, 0x44);
  for (int i = 0; i < 16; i++) {
    int x = 190 + volNoise(i, 68) % 220, y = 250 + volNoise(i, 69) % 50;
    if (x > 372 && y < 270) continue;
    int sw = (int)(sinf(now / 600.0f + x / 40.0f) * 1.5f);
    gfx->drawLine(x, y, x - 3 + sw, y - 6, gD);
    gfx->drawLine(x + 1, y, x + 1 + sw, y - 8, gD);
    gfx->drawLine(x + 2, y, x + 5 + sw, y - 5, gD);
  }
  for (int i = 0; i < 26; i++) {                                    // scree: scattered pebbles
    int x = 140 + volNoise(i, 70) % 320, y = 240 + volNoise(i, 71) % 62;
    if ((x < 200 && y > 262) || (x > 366 && y < 274)) continue;
    gfx->fillRect(x, y, 2 + i % 2, 2, night ? C565(0x2a, 0x30, 0x48) : (i & 1 ? C565(0x84, 0x7c, 0x78) : C565(0xb8, 0xae, 0xa0)));
  }
  static const int16_t EW[5][2] = { { 268, 262 }, { 318, 278 }, { 232, 296 }, { 340, 258 }, { 280, 292 } };   // edelweiss
  for (int i = 0; i < 5; i++) {
    int x = EW[i][0], y = EW[i][1];
    gfx->drawLine(x, y + 2, x, y - 4, gD);
    gfx->fillRect(x - 2, y - 6, 5, 3, night ? C565(0x90, 0xa0, 0xc0) : C565(0xff, 0xff, 0xf0));
    gfx->fillRect(x, y - 5, 1, 1, C565(0xe0, 0xc0, 0x40));
  }

  // residents: FEAROW soars by day / GOLBAT at night, a GEODUDE or CUBONE trundles behind the pet
  int16_t day = night ? 0 : 1;
  if (gMtnDay != day) {
    gMtnFly.unload(); gMtnWalk.unload(); gMtnDay = day;
    gMtnFly.load(day ? 22 : 42, false);
    gMtnWalk.load(day ? 74 : 104, false);
  }
  if (gMtnFly.loaded) {                                            // a long slow glide, turning at each end, clear of the moon
    const uint32_t P = 20000;
    uint32_t u = now % (2 * P);
    bool right = u < P;
    uint32_t tri = right ? u : 2 * P - u;
    int x = 60 + (int)(250ULL * tri / P), y = 168 + (int)(sinf(now / 1400.0f) * 8.0f);
    uint8_t act = right ? PMD_WALKR : PMD_WALKL;
    if (!gMtnFly.has(act)) act = PMD_IDLE;
    drawPmdActZ(gMtnFly, act, x, y, now, true, false, 1, 100, 220);
  }
  if (gMtnWalk.loaded) {
    const uint32_t P = 26000;
    uint32_t u = now % (2 * P);
    bool right = u < P;
    uint32_t tri = right ? u : 2 * P - u;
    int x = 240 + (int)(80ULL * tri / P);
    uint8_t act = right ? PMD_WALKR : PMD_WALKL;
    if (!gMtnWalk.has(act)) act = PMD_IDLE;
    drawPmdActZ(gMtnWalk, act, x, 262, now, true, false, 2, 70, 220);
  }
}

// Graveyard biome for ghost types: always dusky, never cheerful. Back to front:
// bare gnarled trees on the horizon, a haunted house with flickering windows on a
// far hill, drifting fog, an iron fence, weathered gravestones (round, cross,
// obelisk, slab) with moss and carved lines, scattered bones, more fog, and a few
// will-o'-wisps. A small GASTLY (HAUNTER at night) drifts about; at night two bats
// cross the sky. The lower left is bare (poops land there, see
// drawPoops), and the sun and moon stay clear.

// One gravestone, lit cold from the upper right: a stone body per 'kind', a lit
// right edge, a shadowed left, moss at the foot, a hairline crack, carved lines.
// kind: 0 rounded, 1 cross, 2 obelisk, 3 slab (leaning by 'lean' px).
static void graveStone(int cx, int footY, int kind, int w, int h, int lean, int seed, bool night) {
  uint16_t stL = night ? C565(0x5c, 0x64, 0x88) : C565(0xa8, 0xa2, 0xb8);
  uint16_t stM = night ? C565(0x40, 0x48, 0x68) : C565(0x84, 0x7e, 0x98);
  uint16_t stD = night ? C565(0x26, 0x2c, 0x48) : C565(0x5a, 0x56, 0x6c);
  uint16_t moss = night ? C565(0x1e, 0x3a, 0x34) : C565(0x5c, 0x8c, 0x58);
  uint16_t earth = night ? C565(0x1c, 0x1a, 0x2a) : C565(0x44, 0x40, 0x52);
  snowEll(cx, footY + 1, w * 70 / 100, 4, earth);                              // the mound it stands in
  int x0 = cx - w / 2, top = footY - h;
  if (kind == 0) {                                                            // rounded headstone
    gfx->fillRect(x0, top + w / 2, w, h - w / 2, stM);
    gfx->fillCircle(cx, top + w / 2, w / 2, stM);
    gfx->fillRect(x0 + w - 3, top + w / 2, 3, h - w / 2, stL);
    gfx->fillRect(x0, top + w / 2, 3, h - w / 2, stD);
    gfx->fillRect(cx - 2, top + 4, 4, 5, stD);                                // a small carved cross
    gfx->fillRect(cx - 1, top + 2, 2, 9, stD);
  } else if (kind == 1) {                                                     // a cross
    int bw = w / 3;
    gfx->fillRect(cx - bw / 2, top, bw, h, stM);
    gfx->fillRect(x0, top + h / 4, w, bw, stM);
    gfx->fillRect(cx + bw / 2 - 2, top, 2, h, stL);
    gfx->fillRect(x0 + w - 2, top + h / 4, 2, bw, stL);
    gfx->fillRect(cx - bw / 2, top + h / 4 + bw, 2, h - h / 4 - bw, stD);
  } else if (kind == 2) {                                                     // an obelisk on a plinth
    int pw = w, ph = h / 5;
    gfx->fillRect(cx - pw / 2, footY - ph, pw, ph, stM);
    gfx->fillRect(cx - pw / 2, footY - ph, pw, 2, stL);
    int bw = w * 60 / 100;
    gfx->fillRect(cx - bw / 2, top + bw, bw, h - ph - bw, stM);
    gfx->fillTriangle(cx - bw / 2, top + bw, cx + bw / 2, top + bw, cx, top, stL);
    gfx->fillRect(cx + bw / 2 - 3, top + bw, 3, h - ph - bw, stL);
    gfx->fillRect(cx - bw / 2, top + bw, 3, h - ph - bw, stD);
  } else {                                                                    // a slab, leaning, a corner chipped off
    for (int y = 0; y < h; y += 2) {
      int xo = x0 + lean * (h - y) / h;
      gfx->fillRect(xo, top + y, w, 2, stM);
      gfx->fillRect(xo + w - 3, top + y, 3, 2, stL);
      gfx->fillRect(xo, top + y, 3, 2, stD);
    }
    gfx->fillTriangle(x0 + w - 5 + lean, top, x0 + w + lean, top, x0 + w + lean, top + 5, earth);
  }
  gfx->fillRect(cx - w / 2, footY - 4, w, 4, moss);                           // moss creeping up the foot
  for (int i = 0; i < 4; i++) gfx->fillRect(cx - w / 2 + volNoise(i, seed) % w, footY - 4 - volNoise(i + 5, seed) % 5, 2, 2, moss);
  gfx->drawLine(cx - 2, top + h / 2, cx + 3, top + h * 3 / 4, stD);           // a hairline crack
  if (kind != 1) for (int i = 0; i < 2; i++) gfx->fillRect(cx - w / 4, top + h / 2 + i * 4 - 2, w / 2, 1, stD);   // carved lines
}

// A will-o'-wisp: a flickering teardrop flame, violet outside, white-hot inside.
static void graveWisp(int x, int y, uint32_t now, int seed, bool night) {
  float fl = sinf(now / 90.0f + seed * 2.3f);
  int r = 3 + (fl > 0.3f);
  uint16_t out = night ? C565(0x90, 0x70, 0xe8) : C565(0xa8, 0x98, 0xd0);
  gfx->fillTriangle(x - r, y, x + r, y, x + (int)(fl * 2), y - r * 3, out);
  gfx->fillCircle(x, y + 1, r, out);
  gfx->fillTriangle(x - r / 2, y, x + r / 2, y, x + (int)(fl * 1.5f), y - r * 2, C565(0xe4, 0xd8, 0xff));
  gfx->fillRect(x - 1, y, 2, 2, C565(0xff, 0xff, 0xff));
}

// Bones for the graveyard floor. Ivory, lit on the upper right, shaded below.
static void graveBoneCols(bool night, uint16_t c[3]) {
  if (night) { c[0] = C565(0xb4, 0xbc, 0xd4); c[1] = C565(0x7c, 0x86, 0xa4); c[2] = C565(0x2a, 0x30, 0x4c); }
  else       { c[0] = C565(0xf0, 0xe8, 0xd0); c[1] = C565(0xbc, 0xb0, 0x94); c[2] = C565(0x60, 0x56, 0x4a); }
}

// A long bone (femur): a shaded shaft with a lit edge and a knobbed pair at each end.
static void graveFemur(int x0, int y0, int x1, int y1, bool night) {
  uint16_t c[3]; graveBoneCols(night, c);
  gfx->drawLine(x0, y0 + 1, x1, y1 + 1, c[1]);
  gfx->drawLine(x0, y0, x1, y1, c[0]);
  int dx = x1 - x0, dy = y1 - y0, len = (int)sqrtf((float)(dx * dx + dy * dy));
  if (len < 1) return;
  int px = -dy * 2 / len, py = dx * 2 / len;                                 // perpendicular, 2 px
  for (int e = 0; e < 2; e++) {
    int ex = e ? x1 : x0, ey = e ? y1 : y0;
    gfx->fillCircle(ex + px, ey + py, 2, c[0]);
    gfx->fillCircle(ex - px, ey - py, 2, c[1]);
  }
}

// A skull, optionally sunk into the ground up to the jaw.
static void graveSkull(int x, int y, int r, bool buried, bool night, uint16_t earth) {
  uint16_t c[3]; graveBoneCols(night, c);
  gfx->fillCircle(x, y, r, c[0]);
  gfx->fillCircle(x - r / 4, y + r / 4, r * 3 / 4, c[1]);                    // shaded lower left
  gfx->fillCircle(x + r / 5, y - r / 5, r * 3 / 4, c[0]);                    // lit upper right
  gfx->fillRect(x - r * 2 / 3, y + r / 2, r * 4 / 3, r / 2 + 1, c[1]);       // jaw
  for (int t = 0; t < 4; t++) gfx->fillRect(x - r / 2 + t * (r / 3 + 1), y + r / 2 + 1, 1, r / 3, c[2]);   // teeth
  gfx->fillRect(x - r / 2 - 1, y - r / 6, r * 2 / 5 + 2, r / 2 + 1, c[2]);   // eye sockets
  gfx->fillRect(x + r / 10, y - r / 6, r * 2 / 5 + 2, r / 2 + 1, c[2]);
  gfx->fillTriangle(x - 1, y + r / 3, x + 2, y + r / 3, x, y + r / 6, c[2]);   // nose
  if (buried) snowEll(x, y + r, r + 3, r / 2 + 1, earth);
}

// Half a ribcage: a short spine with curved ribs both sides, shrinking toward the bottom.
static void graveRibs(int x, int y, bool night) {
  uint16_t c[3]; graveBoneCols(night, c);
  gfx->drawLine(x, y, x, y + 20, c[1]);
  gfx->drawLine(x + 1, y, x + 1, y + 20, c[0]);
  for (int k = 0; k < 5; k++)
    for (int side = -1; side <= 1; side += 2) {
      int ry = y + 2 + k * 4, w = 11 - k;
      gfx->drawLine(x, ry, x + side * w, ry + 3, side > 0 ? c[0] : c[1]);
      gfx->drawLine(x + side * w, ry + 3, x + side * (w - 2), ry + 8, side > 0 ? c[0] : c[1]);
    }
}

// A row of vertebrae curving across the ground.
static void graveSpine(int x, int y, bool night) {
  uint16_t c[3]; graveBoneCols(night, c);
  for (int i = 0; i < 7; i++) {
    int vx = x + i * 5, vy = y + (int)(sinf(i * 0.7f) * 3.0f);
    gfx->fillRect(vx, vy, 4, 3, c[i & 1 ? 1 : 0]);
    gfx->fillRect(vx + 1, vy - 2, 2, 2, c[1]);                              // the spinous process
  }
}

static PmdMon gGhostMon;
static int16_t gGhostDex = 0;

static void drawGraveyard(uint32_t now, bool night, int h, uint16_t top, uint16_t bot, uint16_t soil) {
  auto skyAt = [&](int y) { return lerp565(top, bot, y < 0 ? 0 : y, HORIZON); };
  const int B = HORIZON + 2;
  uint16_t hz = skyAt(HORIZON - 30);
  uint16_t dark = night ? C565(0x0c, 0x0a, 0x1c) : C565(0x30, 0x2a, 0x40);

  // bare gnarled trees along the horizon, hazed toward the sky
  uint16_t farT = lerp565(dark, hz, 1, 3);
  for (int i = 0; i < 9; i++) {
    int x = 14 + i * 56 + volNoise(i, 81) % 20, ht = 34 + volNoise(i, 82) % 30;
    gfx->drawLine(x, B, x + volNoise(i, 83) % 5 - 2, B - ht, farT);
    gfx->drawLine(x + 1, B, x + 1 + volNoise(i, 83) % 5 - 2, B - ht, farT);
    for (int b = 0; b < 4; b++) {                                              // crooked branches, alternating sides
      int by = B - ht * (30 + b * 18) / 100, dir = (b & 1) ? 1 : -1, len = 8 + volNoise(i + b, 84) % 10;
      gfx->drawLine(x, by, x + dir * len, by - len / 2 - 3, farT);
      gfx->drawLine(x + dir * len, by - len / 2 - 3, x + dir * (len + 5), by - len - 4, farT);
    }
  }

  // the haunted house on a far hill: a main block, a roof with a gable, a tower, lit windows
  snowEll(280, B, 100, 12, lerp565(dark, hz, 1, 4));
  uint16_t hs = lerp565(dark, hz, 1, 5);
  gfx->fillRect(246, B - 40, 70, 40, hs);                                      // main block
  gfx->fillTriangle(240, B - 40, 322, B - 40, 281, B - 62, hs);               // roof
  gfx->fillRect(252, B - 66, 16, 26, hs);                                      // tower
  gfx->fillTriangle(249, B - 66, 271, B - 66, 260, B - 88, hs);               // its spire
  gfx->fillRect(300, B - 58, 6, 14, hs);                                       // chimney
  uint16_t win = night ? C565(0xff, 0xd0, 0x70) : C565(0xd8, 0xc0, 0x80);
  static const int16_t WN[5][2] = { { 256, 56 }, { 262, 28 }, { 274, 24 }, { 290, 24 }, { 303, 24 } };   // x, height above the base
  for (int i = 0; i < 5; i++) {
    int flick = volNoise((int)(now / 160) + i * 7, 85);                         // some windows flicker out for a beat
    if (flick < (night ? 26 : 90)) continue;
    gfx->fillRect(WN[i][0], B - WN[i][1], 5, 7, i == 1 ? C565(0xa8, 0x80, 0xff) : win);
  }

  // low fog behind the fence
  uint16_t fog = lerp565(soil, C565(0xd0, 0xc8, 0xf0), 1, night ? 7 : 4);
  for (int k = 0; k < 3; k++) {
    int x = (int)((k * 220 + now / (60 + k * 17)) % 700) - 170;
    snowEll(x, B + 6 + k * 6, 110 + k * 10, 5, fog);
  }

  // iron fence along the back: pointed bars between two rails, with taller posts
  uint16_t fc = night ? C565(0x10, 0x10, 0x20) : C565(0x2e, 0x2a, 0x3c);
  uint16_t fl = night ? C565(0x34, 0x3c, 0x60) : C565(0x6c, 0x66, 0x80);
  gfx->fillRect(0, B + 6, 466, 2, fc);
  gfx->fillRect(0, B + 16, 466, 2, fc);
  for (int x = 4; x < 466; x += 9) {
    gfx->fillRect(x, B, 2, 22, fc);
    gfx->fillTriangle(x - 1, B, x + 3, B, x + 1, B - 5, fc);
    gfx->fillRect(x + 1, B + 1, 1, 18, fl);                                    // lit edge
  }
  for (int x = 40; x < 466; x += 117) {                                          // taller posts with ball tops
    gfx->fillRect(x, B - 8, 5, 30, fc);
    gfx->fillCircle(x + 2, B - 10, 4, fc);
    gfx->fillRect(x + 3, B - 6, 2, 26, fl);
  }

  // gravestones: three rows back to front, nothing in the poop corner
  graveStone(196, B + 20, 0, 22, 28, 0, 1, night);
  graveStone(262, B + 18, 1, 26, 34, 0, 2, night);
  graveStone(330, B + 20, 2, 22, 40, 0, 3, night);
  graveStone(404, B + 22, 3, 24, 28, 4, 4, night);
  graveStone(300, B + 40, 3, 26, 28, -5, 5, night);
  graveStone(372, B + 46, 0, 30, 36, 0, 6, night);
  graveStone(442, B + 40, 1, 26, 32, 0, 7, night);

  // bones scattered about the ground, clear of the poop corner
  {
    uint16_t earth = night ? C565(0x1c, 0x1a, 0x2a) : C565(0x44, 0x40, 0x52);
    graveSpine(178, 274, night);
    graveFemur(288, 268, 312, 261, night);
    graveSkull(356, 262, 6, true, night, earth);
    graveSkull(330, 284, 8, false, night, earth);
    graveFemur(380, 288, 406, 280, night);
    graveFemur(388, 280, 402, 292, night);                                  // the two cross in the middle
    graveRibs(422, 262, night);
  }

  // fog drifting in front of the stones
  for (int k = 0; k < 3; k++) {
    int x = (int)((k * 240 + 200 + now / (45 + k * 13)) % 720) - 180;
    snowEll(x, 286 + k * 8, 130 + k * 12, 6, lerp565(soil, C565(0xd0, 0xc8, 0xf0), 1, night ? 6 : 4));
  }

  // wisps drifting among the graves, more of them at night
  for (int i = 0; i < (night ? 5 : 2); i++) {
    int x = 190 + (int)(sinf(now / 2100.0f + i * 1.9f) * 80.0f) + i * 36;
    int y = 262 - (int)((sinf(now / 1300.0f + i * 2.4f) + 1.0f) * 12.0f) - i * 4;
    if (x < 170 && y > 250) continue;
    graveWisp(x, y, now, i, night);
  }

  // the small ghost: GASTLY by day, HAUNTER at night, drifting about the upper air
  int16_t want = night ? 93 : 92;
  if (gGhostDex != want) { gGhostMon.unload(); gGhostDex = want; gGhostMon.load(want, false); }
  if (gGhostMon.loaded) {
    float ph = now / 7400.0f;
    int x = 180 + (int)(110.0f * sinf(ph)), y = 176 + (int)(12.0f * sinf(now / 1900.0f));
    uint8_t act = cosf(ph) > 0 ? PMD_WALKR : PMD_WALKL;
    if (!gGhostMon.has(act)) act = PMD_IDLE;
    drawPmdActZ(gGhostMon, act, x, y, now, true, false, 1, night ? 80 : 100, 220);
  }

  // two bats crossing the night sky
  if (night)
    for (int g = 0; g < 2; g++) {
      int x = 520 - (int)(now / 22 + g * 300) % 640, y = 96 + g * 30 + (int)(sinf(now / 500.0f + g * 2.0f) * 8.0f);
      int up = ((now / 110 + g) & 1) ? -5 : 3;
      uint16_t bc = C565(0x0a, 0x08, 0x14);
      gfx->fillRect(x - 1, y - 1, 3, 3, bc);
      gfx->drawLine(x - 8, y + up, x - 1, y, bc);
      gfx->drawLine(x + 1, y, x + 8, y + up, bc);
      gfx->drawLine(x - 8, y + up, x - 5, y + up + 3, bc);
      gfx->drawLine(x + 8, y + up, x + 5, y + up + 3, bc);
    }
}

void drawScene(uint8_t biome, uint32_t now, bool night) {
  int h = sceneHour();
  uint16_t top, bot;
  if (night)            { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (h < 8)       { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }
  else if (h < 18)      { top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }
  else                  { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }

  for (int y = 0; y < HORIZON; y += 8)
    gfx->fillRect(0, y, 466, 8, lerp565(top, bot, y, HORIZON));

  if (night) {
    const int moonY = 122;
    drawStars(now, top, bot, HORIZON - 10);
    drawMoon(360, moonY, lerp565(top, bot, moonY, HORIZON));
  } else if (h < 18) {
    drawSun(360, 84, 24, h < 8 ? C565(0xff, 0xc8, 0x6a) : C565(0xff, 0xd9, 0x5c),
            lerp565(top, bot, 84, HORIZON), now);
    drawClouds(now, biome == BIOME_VOLCANO ? C565(0xc8, 0x9c, 0x90) : C565(0xff, 0xff, 0xff),
               lerp565(top, bot, 150, HORIZON));
  } else {
    drawSun(233, biome == BIOME_BEACH ? SEA_TOP : HORIZON - 6, 32, C565(0xff, 0xe6, 0xa0),
            lerp565(top, bot, biome == BIOME_BEACH ? SEA_TOP : HORIZON - 6, HORIZON), now);
  }

  uint16_t soil = BIOME_SOIL[biome < BIOME_COUNT ? biome : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  if (biome != BIOME_BEACH && (gBeachMon.loaded || gBeachTried || gBeachBird.loaded || gBeachBirdTried)) {
    gBeachMon.unload(); gBeachTried = false;
    gBeachBird.unload(); gBeachBirdTried = false;
  }
  if (biome != BIOME_GRAVEYARD && (gGhostMon.loaded || gGhostDex)) { gGhostMon.unload(); gGhostDex = 0; }
  if (biome != BIOME_MOUNTAIN && (gMtnFly.loaded || gMtnWalk.loaded || gMtnDay != -1)) {
    gMtnFly.unload(); gMtnWalk.unload(); gMtnDay = -1;
  }
  if (biome != BIOME_MEADOW && (gMeadowFly.loaded || gMeadowWalk.loaded || gMeadowFlyDex || gMeadowWalkTried)) {
    gMeadowFly.unload(); gMeadowWalk.unload(); gMeadowFlyDex = 0; gMeadowWalkTried = false;
  }
  if (biome != BIOME_SNOW && (gSnowMon.loaded || gSnowMonDex)) { gSnowMon.unload(); gSnowMonDex = 0; }
  if (biome != BIOME_VOLCANO && (gVolcanoMon.loaded || gVolcanoTried)) { gVolcanoMon.unload(); gVolcanoTried = false; }
  if (biome != BIOME_FOREST && (gForestBee.loaded || gForestBug.loaded || gForestBat.loaded || gForestTried || gForestBatTried)) {
    gForestBee.unload(); gForestBug.unload(); gForestBat.unload();
    gForestTried = gForestBatTried = false;
  }
  if (biome == BIOME_BEACH) {
    drawBeach(now, night, h, top, bot, soil);
    return;
  }

  gfx->fillRect(0, HORIZON, 466, 466 - HORIZON, soil);
  uint16_t hill = lerp565(soil, night ? C565(0x0c, 0x12, 0x24) : C565(0xff, 0xff, 0xff), 3, 16);
  gfx->fillRoundRect(-60, HORIZON - 14, 586, 60, 30, hill);

  uint16_t dk = lerp565(soil, C565(0x10, 0x18, 0x20), night ? 11 : 7, 16);
  (void)dk;
  if (biome == BIOME_FOREST) {
    drawForest(now, night, h, top, bot, soil);
  } else if (biome == BIOME_VOLCANO) {
    drawVolcano(now, night, top, bot);
  } else if (biome == BIOME_MOUNTAIN) {
    drawMountain(now, night, h, top, bot, soil);
  } else if (biome == BIOME_SNOW) {
    drawSnow(now, night, h, top, bot, soil);
  } else if (biome == BIOME_GRAVEYARD) {
    drawGraveyard(now, night, h, top, bot, soil);
  } else if (biome == BIOME_MEADOW) {
    drawMeadow(now, night, h, top, bot, soil);
  }
}

// primera partida: elige inicial entre Bulbasaur / Charmander / Squirtle
void renderStarterSelect() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  const char *t = T(S_CHOOSE_STARTER);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(t) * 6, 68);
  gfx->print(t);
  for (int i = 0; i < starterCountShown(pet.region); i++) {
    int16_t d = starterOf(pet.region, i);
    const DexEntry &de = DEX_TBL[d];
    int ry = STARTER_ROW_Y + i * (STARTER_ROW_H + STARTER_ROW_GAP);
    gfx->fillRoundRect(70, ry, 326, STARTER_ROW_H, 14, lerp565(de.accent, UI_WHITE, 6, 8));
    gfx->drawRoundRect(70, ry, 326, STARTER_ROW_H, 14, de.accent);
    const uint8_t *th = thumbs.get(d);     // miniatura del inicial (si la SD esta lista)
    if (th) drawThumb(th, 76, ry - 5, 3, false);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(3);
    gfx->setCursor(178, ry + 24);
    gfx->print(de.name);
  }
  {
    const char *rl = T(S_STARTER_RANDOM);
    gfx->fillRoundRect(STARTER_RAND_X, STARTER_RAND_Y, STARTER_RAND_W, STARTER_RAND_H, 14, UI_WHITE);
    gfx->drawRoundRect(STARTER_RAND_X, STARTER_RAND_Y, STARTER_RAND_W, STARTER_RAND_H, 14, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(rl) * 6, STARTER_RAND_Y + STARTER_RAND_H / 2 - 8);
    gfx->print(rl);
  }
  gfx->flush();
}

// ---------- crash breadcrumbs ----------
//
// "It has a mini crash" is not a bug report you can act on, because the board
// never says WHY it restarted. These three live in RTC memory, which survives
// a panic, a watchdog and a software reset -- everything except pulling the
// power -- so the next boot can say what the firmware was doing when it died.
//
// Written every frame. That is one word to RTC RAM per render, which costs
// nothing next to a full-screen redraw, and it means the crumb always names
// the screen that was actually on the panel rather than the last one opened.
#define CRUMB_MAGIC 0x7AABE10C
RTC_NOINIT_ATTR uint32_t gCrumbMagic;
RTC_NOINIT_ATTR uint32_t gCrumbScreen;
RTC_NOINIT_ATTR uint32_t gCrumbHeap;


// Which screen is on the panel RIGHT NOW, in the same order render() tests.
uint8_t uiCurrentScreen() {
  if (transferMode()) return SCR_TRANSFER;
  if (pet.awaitingStarter()) return starterRegionDone ? SCR_STARTER : SCR_REGION;
  if (evoPickOpen) return SCR_EVOPICK;
  if (galleryOpen) return galleryPick ? SCR_DEXPICK : SCR_GALLERY;
  if (movePickOpen) return SCR_MOVEPICK;
  if (partyOpen) return boxOpen ? SCR_BOX : SCR_PARTY;
  if (bagOpen) return SCR_BAGSCR;
  if (kbOpen) return SCR_KEYBOARD;
  if (cardOpen) return SCR_CARD;
  if (playerOpen) return SCR_PLAYER;
  if (medalsOpen) return SCR_MEDALS;
  if (clockOpen) return SCR_CLOCK;
  if (btlWinUntil) return SCR_WIN;
  if (battleOpen) return SCR_BATTLE;
  if (pickOpen) return SCR_PICK;
  if (lanOpen) return SCR_LAN;
  if (exploreOpen) return SCR_EXPLORE;
  if (gymOpen) return gymPick ? SCR_GYMPICK : SCR_GYM;
  if (pet.hasLearnOffer()) return SCR_LEARN;
  if (gameOpen || sackOpen || spdOpen || memoOpen) return SCR_GAME;
  if (trainOpen) return SCR_TRAIN;
  if (jukeOpen) return SCR_JUKEBOX;
  if (menuOpen) return SCR_MENU;
  return SCR_MAIN;
}

// ---------------------------------------------------------------------------
// The tile axis and the rim.
//
// Two rules replace the pile of per-screen opinions that used to live in
// onSwipe(), and between them they close the bug this project shipped FOUR
// times -- a paged screen that CLOSED instead of paging:
//
//   HORIZONTAL is the tile axis. Seven peer screens with the pet in the middle,
//   and it BUMPS at the ends. It can no longer close anything, so the gesture
//   that means "next page" and the gesture that means "exit" are different
//   gestures, which they never were before.
//
//   THE RIM is paging. A drag that starts outside RIM_R0 pages whatever is in
//   front of you, and CLAMPS at both ends. This is what frees the horizontal
//   axis -- A and C only work as a pair, and this is the joint.
//
// Vertical stays depth: up opens, down goes back.

// A tile's screen, and the chooser it opens on where it has one. Both belong to
// the same tile: the region choosers are not a level of depth, they ARE the gym
// and Pokedex tiles until a region is picked. One table, so uiTileIndex() and
// uiTileGo() cannot end up with different ideas of what is on the axis.
//
// PLAYER . MEDALS . PARTY . [PET] . EXPLORE . GYM . DEX -- yours on the left, the world
// on the right. Exploration is a primary loop, not a gym action, so it gets the
// first world-facing stop and the main screen can name it directly.
struct TileDef { uint8_t scr, alt; };
static const TileDef TILE[TILE_COUNT] = {
  { SCR_PLAYER, SCR_PLAYER }, { SCR_MEDALS, SCR_MEDALS }, { SCR_PARTY, SCR_PARTY },
  { SCR_MAIN, SCR_MAIN },
  { SCR_EXPLORE, SCR_EXPLORE }, { SCR_GYM, SCR_GYMPICK },
  { SCR_GALLERY, SCR_DEXPICK },
};

int uiTileIndex() {
  uint8_t s = uiCurrentScreen();
  for (int i = 0; i < TILE_COUNT; i++)
    if (TILE[i].scr == s || TILE[i].alt == s) return i;
  return -1;   // not on the axis: a modal, a minigame, a fight
}

// Jump to tile i. Every stop on the axis is a top-level screen, so this closes
// the others rather than stacking on them.
void uiTileGo(int i) {
  if (i < 0) i = 0;
  if (i >= TILE_COUNT) i = TILE_COUNT - 1;
  exploreOpen = gymOpen = partyOpen = galleryOpen = playerOpen = medalsOpen = false;
  boxOpen = false;
  boxJumpOpen = false;
  boxDetail = partyDetail = 0;
  galleryDetail = 0;
  switch (TILE[i].scr) {
    // The two multi-region screens land on their CHOOSER, never on whichever
    // region was last set. Opening straight into it is how Johto and Hoenn came
    // to be built, reachable, and completely invisible.
    case SCR_GYM: gymOpen = true; gymPick = true; gymPage = 0; rpickPage = 0; break;
    case SCR_EXPLORE: exploreOpen = true; break;
    case SCR_PARTY: partyOpen = true; break;
    case SCR_GALLERY:
      galleryOpen = true; galleryPick = true; galleryPage = 0; rpickPage = 0;
      galleryDirty = true;
      break;
    case SCR_PLAYER: playerOpen = true; playerPage = 0; break;
    case SCR_MEDALS: medalsOpen = true; medalsPage = 0; break;   // opens on TODAY
    default: break;   // the pet tile is everything else being shut
  }
  sfxPlay(SFX_TAP);
}

// ---------- the rim ----------

// The paged screens as ONE table: which variable holds the page, and how many
// there are. Three separate switches over the same list -- one to draw the
// scrollbar, one to page, one for the test -- is precisely the shape CLAUDE.md
// warns about, so draw, input and test all come through here.
static uint8_t *uiRimTarget(uint8_t *pages) {
  uint8_t n = 1;
  uint8_t *p = nullptr;
  switch (uiCurrentScreen()) {
    case SCR_GYM:     p = &gymPage;     n = (TRAINER_COUNT + GYM_ROWS - 1) / GYM_ROWS; break;
    // galleryPages(), not GAL_PAGES: with a filter on, the grid is shorter than
    // the region, and paging past the filtered end draws an empty screen.
    case SCR_GALLERY: p = &galleryPage; n = galleryPages(); break;
    case SCR_PLAYER:  p = &playerPage;  n = PLAYER_PAGES; break;
    case SCR_MEDALS:  p = &medalsPage;  n = MEDALS_PAGES; break;
    case SCR_BOX:
      if (!boxJumpOpen) { p = &boxPage; n = BOX_SLOTS / BOX_PER_PAGE; }
      break;
    case SCR_BAGSCR:  p = &bagPage;     n = bagPages(); break;
    case SCR_CARD:    p = &cardPage;    n = CARD_PAGES; break;
    // Only while the bag is open; the move grid and the switch list never page.
    case SCR_BATTLE:
      if (btlMenu == 3) { p = &btlBagPage; n = btlBagPages(); }
      break;
    // Settings became two pages when brightness and the reset arrived; the rim
    // is how every other paged screen here works, and uiDrawRimBar() reads the
    // same table, so the second page advertises itself with no new control.
    case SCR_CLOCK:   p = &clockPage;   n = CLOCK_PAGES; break;
    case SCR_JUKEBOX: p = &jukePage;    n = JUKE_PAGES;  break;
    case SCR_MOVEPICK: {
      uint8_t all[MOVE_COUNT];
      uint8_t k = learnableList(all, sizeof(all));
      p = &movePickPage;
      n = k ? (uint8_t)((k + MOVE_PICK_PER_PAGE - 1) / MOVE_PICK_PER_PAGE) : 1;
      break;
    }
    case SCR_PICK: {
      uint8_t k = pickCandidates();
      p = &pickPage;
      n = k ? (uint8_t)((k + PICK_PER_PAGE - 1) / PICK_PER_PAGE) : 1;
      break;
    }
    case SCR_EVOPICK: {
      int16_t options[EVO_BRANCH_MAX];
      uint8_t k = pet.evoOptions(pet.speciesId, options);
      p = &evoPickPage;
      n = k ? (uint8_t)((k + EVO_PICK_PER_PAGE - 1) / EVO_PICK_PER_PAGE) : 1;
      break;
    }
    default: break;
  }
  if (pages) *pages = n ? n : 1;
  return p;
}

uint8_t uiRimPages() {
  uint8_t n = 1;
  if (rpickModeNow() != 0xFF) return rpickPageCount(rpickModeNow());
  uiRimTarget(&n);
  return n;
}

uint8_t uiRimPage() {
  if (rpickModeNow() != 0xFF) return rpickPage;
  uint8_t *p = uiRimTarget(nullptr);
  return p ? *p : 0;
}

// dir > 0 is clockwise, which is forwards.
void onRim(int dir) {
  if (rpickSwipe(-dir)) return;   // the chooser pages itself, and WRAPS
  uint8_t n = 1;
  uint8_t *p = uiRimTarget(&n);
  if (!p || n < 2) return;
  int v = (int)(*p) + dir;
  if (v < 0) v = 0;
  if (v >= n) v = n - 1;          // CLAMPS: the rim can never close a screen
  if (v == (int)(*p)) return;
  *p = (uint8_t)v;
  if (uiCurrentScreen() == SCR_GALLERY) galleryDirty = true;
  sfxPlay(SFX_TAP);
}

bool uiOnRim(int16_t x, int16_t y) {
  int dx = x - CX, dy = y - CY;
  return dx * dx + dy * dy >= RIM_R0 * RIM_R0;
}

float uiRimAngle(int16_t x, int16_t y) {
  return atan2f((float)(y - CY), (float)(x - CX)) * 57.29578f;
}

// ---------- the chrome that makes both of the above visible ----------
#define TILE_DOT_Y 440

static inline uint16_t uiChromeInk() {
  return uiCurrentScreen() == SCR_MAIN ? inkColor() : UI_INK;
}

// The tile axis, drawn on every screen that is on it. Five bound gestures that
// nothing on the panel mentions is precisely how Johto and Hoenn came to be
// built, reachable and completely invisible; an axis nobody can see is that
// same mistake one level up.
void uiDrawTileDots() {
  int t = uiTileIndex();
  if (t < 0) return;
  uint16_t ink = uiChromeInk();
  if (t == TILE_PET) {
    char left[28], right[24];
    snprintf(left, sizeof(left), "< %s", T(S_PARTY));
    snprintf(right, sizeof(right), "%s >", T(S_EXPLORE));
    gfx->setTextColor(ink);
    gfx->setTextSize(2);
    gfx->setCursor(CX - 8 - (int)strlen(left) * 12, 408);
    gfx->print(left);
    gfx->setCursor(CX + 8, 408);
    gfx->print(right);
  }
  for (int i = 0; i < TILE_COUNT; i++) {
    int x = CX - (TILE_COUNT - 1) * 13 + i * 26;
    if (i == t) gfx->fillRoundRect(x - 9, TILE_DOT_Y - 3, 18, 7, 3, ink);
    else gfx->fillCircle(x, TILE_DOT_Y, 3, UI_TRACK);
  }
}

// The arc scrollbar, at 2-4 o'clock, replacing the dot rows the paged screens
// used to carry. A straight bar is at its thinnest exactly where a list is
// longest, and dots show position without showing extent. Both this and the
// rim drag read uiRimTarget(), so they cannot disagree about where you are.
void uiDrawRimBar() {
  uint8_t n = uiRimPages();
  if (n < 2) return;
  uint8_t p = uiRimPage();
  if (p >= n) p = n - 1;
  int span = RIM_BAR_A1 - RIM_BAR_A0;
  int thumb = span / n;
  if (thumb < 9) thumb = 9;
  int a0 = RIM_BAR_A0 + (span - thumb) * (int)p / (int)(n - 1);
  uiArc(CX, CY, RIM_BAR_R, RIM_BAR_A0, RIM_BAR_A1, 7, UI_TRACK);
  uiArc(CX, CY, RIM_BAR_R, a0, a0 + thumb, 7, uiChromeInk());
}

// Called by every renderer that is on the axis or pages, immediately before it
// flushes. One call, so a screen either has the chrome or visibly does not.
void uiChrome() {
  uiDrawTileDots();
  uiDrawRimBar();
}

uint8_t currentExploreRegion() {
  if (exploreRegion < REGION_COUNT && regionAvailable(exploreRegion)) return exploreRegion;
  uint8_t base = pet.region % REGION_COUNT;
  exploreRegion = regionAvailable(base) ? base : nextAvailableRegion(base);
  return exploreRegion;
}

int8_t exploreRegionHit(int16_t x, int16_t y) {
  if (y < EXPLORE_REGION_Y || y > EXPLORE_REGION_Y + EXPLORE_REGION_H ||
      x < EXPLORE_REGION_X || x > EXPLORE_REGION_X + EXPLORE_REGION_W) return 0;
  if (x <= EXPLORE_REGION_X + EXPLORE_REGION_ARROW_W) return -1;
  if (x >= EXPLORE_REGION_X + EXPLORE_REGION_W - EXPLORE_REGION_ARROW_W) return 1;
  return 0;
}

void exploreStepRegion(int8_t step) {
  int region = currentExploreRegion();
  for (uint8_t i = 0; i < REGION_COUNT; i++) {
    region += step < 0 ? -1 : 1;
    if (region < 0) region = REGION_COUNT - 1;
    else if (region >= REGION_COUNT) region = 0;
    if (regionAvailable((uint8_t)region)) {
      exploreRegion = (uint8_t)region;
      return;
    }
  }
}

void renderExplore() {
  int h = sceneHour();
  gNight = pet.sleeping || h < 6 || h >= 20;
  drawScene(sceneBiome(), millis(), gNight);

  gfx->fillRoundRect(78, 28, 310, 54, 14, UI_BG_DAY);
  gfx->drawRoundRect(78, 28, 310, 54, 14, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)strlen(T(S_EXPLORE)) * 9, 44);
  gfx->print(T(S_EXPLORE));

  drawMap(SPR_ICON_PLAY, 16, CX - 40, 88, 5, false);
  const char *region = REGIONS[currentExploreRegion()].name;
  gfx->fillRoundRect(EXPLORE_REGION_X, EXPLORE_REGION_Y, EXPLORE_REGION_W,
                     EXPLORE_REGION_H, 16, UI_BG_DAY);
  gfx->drawRoundRect(EXPLORE_REGION_X, EXPLORE_REGION_Y, EXPLORE_REGION_W,
                     EXPLORE_REGION_H, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(EXPLORE_REGION_X + 38, EXPLORE_REGION_Y + 29);
  gfx->print("<");
  gfx->setCursor(EXPLORE_REGION_X + EXPLORE_REGION_W - 56, EXPLORE_REGION_Y + 29);
  gfx->print(">");
  gfx->setTextSize(strlen(region) <= 10 ? 3 : 2);
  int regionCharHalf = strlen(region) <= 10 ? 9 : 6;
  gfx->setCursor(CX - (int)strlen(region) * regionCharHalf,
                 EXPLORE_REGION_Y + (strlen(region) <= 10 ? 29 : 35));
  gfx->print(region);

  const char *dif = T(exploreHard ? S_HARD : S_EASY);
  int dw = (int)strlen(dif) * 12 + 48;
  if (dw < 140) dw = 140;
  gfx->fillRoundRect(CX - dw / 2, EXPLORE_DIF_Y, dw, EXPLORE_DIF_H, 12,
                     exploreHard ? UI_BAR_BAD : UI_TRACK);
  gfx->setTextColor(exploreHard ? UI_BG_DAY : UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(dif) * 6, EXPLORE_DIF_Y + 14);
  gfx->print(dif);

  bool live = !pet.isEgg() && pet.ceremony == CER_NONE;
  gfx->fillRoundRect(EXPLORE_BTN_X, EXPLORE_BTN_Y, EXPLORE_BTN_W, EXPLORE_BTN_H,
                     16, live ? UI_BAR_OK : UI_TRACK);
  gfx->drawRoundRect(EXPLORE_BTN_X, EXPLORE_BTN_Y, EXPLORE_BTN_W, EXPLORE_BTN_H,
                     16, live ? UI_INK : UI_INK_SOFT);
  gfx->setTextColor(live ? UI_WHITE : UI_INK_SOFT);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)strlen(T(S_EXPLORE)) * 9, EXPLORE_BTN_Y + 24);
  gfx->print(T(S_EXPLORE));
  uiChrome();
  gfx->flush();
}

static void crumbDrop() {
  gCrumbMagic = CRUMB_MAGIC;
  gCrumbScreen = uiCurrentScreen();
  gCrumbHeap = ESP.getFreeHeap();
}

static const char *resetReasonName(int r) {
  switch (r) {
    case ESP_RST_POWERON:  return "power on";
    case ESP_RST_EXT:      return "reset pin";
    case ESP_RST_SW:       return "software (our own restart)";
    case ESP_RST_PANIC:    return "PANIC -- a crash";
    case ESP_RST_INT_WDT:  return "INTERRUPT WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK WATCHDOG -- something blocked too long";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT -- the supply sagged";
    case ESP_RST_DEEPSLEEP: return "deep sleep";
    default: return "unknown";
  }
}

// Printed once at boot. On a clean start it is one line; after a crash it says
// which screen was up and how much heap was left, which is the whole point.
void bootReport() {
  int r = (int)esp_reset_reason();
  bool bad = (r == ESP_RST_PANIC || r == ESP_RST_INT_WDT ||
              r == ESP_RST_TASK_WDT || r == ESP_RST_WDT || r == ESP_RST_BROWNOUT);
  Serial.printf("boot: reset=%s heap=%u psram=%u\n", resetReasonName(r),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  if (bad && gCrumbMagic == CRUMB_MAGIC) {
    Serial.printf("CRASH: it died on the '%s' screen with heap=%u\n",
                  gCrumbScreen < SCR_COUNT ? SCREEN_NAME[gCrumbScreen] : "?",
                  (unsigned)gCrumbHeap);
  } else if (bad) {
    Serial.println("CRASH: no breadcrumb (RTC memory was lost too)");
  }
  gCrumbMagic = 0;   // one report per crash, not on every later boot
}

// Dark on purpose: it can sit on an AMOLED for the length of a whole install.
void renderTransfer() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(UI_INK_NIGHT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(T(S_XFER)) * 6, 160);
  gfx->print(T(S_XFER));
  char n[32];
  snprintf(n, sizeof(n), T(S_XFER_FMT), (unsigned)sdTransferFiles);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)strlen(n) * 9, 212);
  gfx->print(n);
  gfx->setTextColor(UI_SCROLL_NIGHT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(T(S_XFER_HINT)) * 6, 290);
  gfx->print(T(S_XFER_HINT));
  gfx->flush();
}

void render() {
  crumbDrop();   // so a crash can name the screen it happened on
  if (transferMode()) {
    renderTransfer();
    return;
  }
  if (pet.awaitingStarter()) {  // primera partida: region y luego inicial
    if (!starterRegionDone) renderRegionPick(RPICK_FOR_START);
    else renderStarterSelect();
    return;
  }
  if (evoPickOpen) {
    renderEvoPick();
    return;
  }
  if (galleryOpen) {
    if (galleryPick) { renderRegionPick(RPICK_FOR_DEX); return; }
    renderGallery();
    return;
  }
  if (movePickOpen) {
    renderMovePick();
    return;
  }
  if (partyOpen) {
    if (boxOpen) {
      if (boxDetail) renderBoxDetail();
      else renderBox();
    } else if (partyDetail) renderPartyDetail();
    else renderParty();
    return;
  }
  if (gameOpen) {
    renderGame();
    return;
  }
  if (bagOpen) {
    renderBag();
    return;
  }
  if (sackOpen) {
    renderSack();
    return;
  }
  if (spdOpen) {
    renderSpeed();
    return;
  }
  if (memoOpen) {
    renderMemo();
    return;
  }
  if (trainOpen) {
    renderTrain();
    return;
  }
  if (jukeOpen) {
    renderJukebox();
    return;
  }
  if (kbOpen) {
    renderKeyboard();
    return;
  }
  if (clockOpen) {
    renderClock();
    return;
  }
  if (battleOpen) {
    btlLinkPoll();
    renderBattle();
    return;
  }
  if (pickOpen) {
    renderPick();
    return;
  }
  if (lanOpen) {
    renderLan();
    return;
  }
  if (exploreOpen) {
    renderExplore();
    return;
  }
  if (gymOpen) {
    if (gymPick) renderRegionPick(RPICK_FOR_GYMS);
    else renderGyms();
    return;
  }
  if (playerOpen) {
    renderPlayer();
    return;
  }
  if (medalsOpen) {
    renderMedals();
    return;
  }
  if (pet.hasLearnOffer()) {
    renderLearn();
    return;
  }
  if (cardOpen) {
    renderCard();
    return;
  }
  int h = sceneHour();
  gNight = pet.sleeping || h < 6 || h >= 20;
  // drawScene cubre los 466x466 completos: sin fillScreen(NEGRO) previo para
  // que un flush DMA solapado nunca capture negro a medias (anti-parpadeo)
  drawScene(sceneBiome(), millis(), gNight);

  if (pet.ceremony) {
    const DexEntry &d = DEX_TBL[pet.speciesId];
    const char *msg = (pet.ceremony == CER_FAREWELL) ? T(S_FAREWELL)
                      : (pet.ceremony == CER_RUNAWAY) ? T(S_RUNAWAY)
                                                      : T(S_GOODBYE);
    drawHeader(d.name, d.accent, msg);
    drawCeremony();
    gfx->flush();
    return;
  }

  if (pet.isEgg()) {
    drawHeader(T(S_EGG_HDR), inkColor(), eggMsg());
    int s = 5, x = CX - 16 * s, y = PET_CY - 16 * s;
    drawMap(SPR_EGG, SPRITE_H, x, y, s, false);
    if (pet.eggCracks() >= 1)
      for (auto &c : CRACK1) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    if (pet.eggCracks() >= 2)
      for (auto &c : CRACK2) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    if (pet.eggRarity() >= R_RARO) {
      const char *rar = (pet.eggRarity() == R_LEGENDARIO) ? T(S_EGG_LEGEND) : T(S_EGG_RARE);
      gfx->setTextColor(pet.eggRarity() == R_LEGENDARIO ? UI_BAR_WARN : 0x4C98);
      gfx->setTextSize(2);
      gfx->setCursor(CX - strlen(rar) * 6, 316);
      gfx->print(rar);
    }
    char reg[24];
    snprintf(reg, sizeof(reg), T(S_POKEDEX_FMT),
             pet.dexCountIn(DEXV_COMPLETE, false, 1, DEX_COUNT), DEX_COUNT);
    gfx->fillRect(0, 312, 466, 154, gNight ? UI_BG_NIGHT : UI_BG_DAY);
    gfx->setTextColor(inkColor());
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(reg) * 6, 344);
    gfx->print(reg);

    // Which generation this egg comes from. It lives HERE rather than in the
    // settings screen because this is the only moment it does anything: the
    // species is decided when the egg appears, so choosing the region is
    // something you do to the egg in front of you.
    drawEggRegion();
  } else {
    const DexEntry &d = DEX_TBL[pet.speciesId];
    char name[28];
    const char *base = pet.nick[0] ? pet.nick : d.name;
    snprintf(name, sizeof(name), T(S_NAME_FMT), pet.shiny ? "*" : "", base, pet.level());
    drawHeader(name, gNight ? UI_INK_NIGHT : d.accent, statusMsg());
    drawStreakBadge();
    drawStepPill();
    drawPet();
    drawEatFood();
    drawBath();
    drawPoops();
    // panel inferior: base limpia para barras y botones sobre el paisaje
    gfx->fillRect(0, 312, 466, 154, gNight ? UI_BG_NIGHT : UI_BG_DAY);
    drawBars();
    drawButtons();
    drawCelebration();
    if (pet.wantEvolveButton()) drawEvolveButton();        // CTA rojo: evolucionar
    else if (pet.canRunawayNow()) drawRunawayButton();     // CTA sombrio: escapada (abandono)
    else if (pet.wantFarewellButton()) drawFarewellButton();  // CTA dorado: despedida
  }

  if (pet.sleeping) {
    gfx->setTextColor(UI_INK_NIGHT);
    gfx->setTextSize(3);
    gfx->setCursor(320, 130);
    gfx->print("Zz");
  }

  // selector de comida
  if (feedMenuUntil) {
    if (millis() > feedMenuUntil) {
      feedMenuUntil = 0;
    } else {
      gfx->fillRoundRect(FEED_MENU_X, FEED_MENU_Y, FEED_MENU_W, FEED_MENU_H, 14, UI_WHITE);
      gfx->drawRoundRect(FEED_MENU_X, FEED_MENU_Y, FEED_MENU_W, FEED_MENU_H, 14, inkColor());
      drawMap(SPR_ICON_FOOD, 16, FEED_MENU_X + 9, FEED_MENU_Y + 8, 3, false);
      drawMap(SPR_ICON_BERRY_B, 16, FEED_MENU_X + 75, FEED_MENU_Y + 8, 3, false);
      drawMap(SPR_ICON_BERRY_G, 16, FEED_MENU_X + 141, FEED_MENU_Y + 8, 3, false);
      drawMap(SPR_ICON_CANDY, 16, FEED_MENU_X + 207, FEED_MENU_Y + 8, 3, false);
    }
  }

  // dialogo "soltar?" (pulsacion larga sobre el bicho)
  if (confirmUntil) {
    if (millis() > confirmUntil) {
      confirmUntil = 0;
    } else {
      gfx->fillRoundRect(94, 168, 278, 152, 16, UI_WHITE);
      gfx->drawRoundRect(94, 168, 278, 152, 16, UI_INK);
      char q[28];
      snprintf(q, sizeof(q), T(S_RELEASE_FMT), DEX_TBL[pet.speciesId].name);
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - strlen(q) * 6, 196);
      gfx->print(q);
      gfx->fillRoundRect(118, 252, 100, 52, 12, UI_BAR_OK);
      gfx->setTextColor(UI_WHITE);
      gfx->setCursor(118 + (100 - (int)strlen(T(S_YES)) * 12) / 2, 270);
      gfx->print(T(S_YES));
      gfx->fillRoundRect(248, 252, 100, 52, 12, UI_BAR_BAD);
      gfx->setCursor(248 + (100 - (int)strlen(T(S_NO)) * 12) / 2, 270);
      gfx->print(T(S_NO));
    }
  }

  // dialogo de decision (evolucionar/mantener, despedirse/quedaros)
  if (choiceKind) {
    if (millis() > choiceUntil) choiceKind = 0;
    else drawChoiceDialog();
  }

  // "<name> joined the party!" after a farewell or release
  if (partyBannerUntil) {
    if (millis() > partyBannerUntil) {
      partyBannerUntil = 0;
    } else {
      char b[40];
      snprintf(b, sizeof(b), T(S_PARTY_JOINED), partyBannerName);
      gfx->fillRoundRect(53, 176, 360, 74, 16, UI_BAR_OK);
      gfx->drawRoundRect(53, 176, 360, 74, 16, UI_INK);
      gfx->setTextColor(UI_WHITE);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)strlen(b) * 6, 206);
      gfx->print(b);
    }
  }

  uiChrome();
  if (menuOpen) drawMenu();

  gfx->flush();
}

// ---------- minijuego: toques con la pokeball ----------

void startGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  gameOpen = true;
  gameOverUntil = 0;
  gameUntil = millis() + GAME_MS;
  gameScore = 0;
  gameMisses = 0;
  gameNewHi = false;
  hitTime = 0;
  gamePetX = 233;
  respawnBall();
}

void respawnBall() {
  ballX = 150 + random(166);
  ballY = 96;
  float sp = 1.6f + gameScore * 0.05f;  // mas viva segun avanzas
  if (sp > 4.0f) sp = 4.0f;
  ballVX = random(2) ? sp : -sp;
  ballVY = 0;
}

// Leaving a minigame early banks what was actually earned rather than voiding
// it. Quitting used to forfeit everything, which mattered little when the ball
// game trained a stat you could grind back -- but it is now purely about
// happiness, and a pet that just played should be happier for it. The
// gameOver/over guards stop a swipe during the results screen paying twice.
void leaveGame() {
  if (!gameOverUntil) pet.playResult(gameScore);
  gameOpen = false;
}
void leaveSack() {
  if (!sackOverUntil) pet.trainStrength(sackHits);
  sackOpen = false;
}
void leaveSpeed() {
  if (!spdOverUntil) pet.trainSpeed(spdHits);
  spdOpen = false;
}

void gameTap(int16_t x, int16_t y) {
  if (gameOverUntil) return;
  // The ball is checked BEFORE the quit strip. The ball bounces inside a circle
  // of radius 205 about the centre, so it reaches y=28 -- well inside the y<72
  // header. Reaching up to hit a high ball used to abandon the game instead,
  // silently forfeiting the score, the speed training and the record.
  float dx = ballX - x, dy = ballY - y;
  bool onBall = (dx * dx + dy * dy < 74 * 74);
  if (!onBall && y < 72) {  // tocar la cabecera = salir, conservando lo ganado
    leaveGame();
    return;
  }
  if (onBall) {  // toque a la bola!
    gameScore++;
    sfxPlay(SFX_PLAY);
    // golpe mas suave: impulso moderado que crece poco a poco con la puntuacion
    float lift = 6.6f + (gameScore > 16 ? 3.5f : gameScore * 0.22f);
    ballVY = -lift;
    ballVX += dx * 0.12f;
    if (ballVX > 6.5f) ballVX = 6.5f;
    if (ballVX < -6.5f) ballVX = -6.5f;
    hitX = ballX;
    hitY = ballY;
    hitTime = millis();
  }
}

void stepGame() {
  float grav = 0.40f + gameScore * 0.013f;  // cae un poco mas rapido cada vez
  if (grav > 0.80f) grav = 0.80f;
  ballVY += grav;
  ballX += ballVX;
  ballY += ballVY;
  // rebote en la pared circular
  float dx = ballX - CX, dy = ballY - CY;
  float d = sqrtf(dx * dx + dy * dy);
  if (d > 205) {
    float nx = dx / d, ny = dy / d;
    float dot = ballVX * nx + ballVY * ny;
    if (dot > 0) {
      ballVX = (ballVX - 2 * dot * nx) * 0.85f;
      ballVY = (ballVY - 2 * dot * ny) * 0.85f;
    }
    ballX = CX + nx * 205;
    ballY = CY + ny * 205;
  }
  // the clock, or three misses, whichever lands first
  if (gameUntil && millis() >= gameUntil && !gameOverUntil) {
    gameNewHi = (gameScore > pet.gameHi);
    pet.playResult(gameScore);
    sfxPlay(gameNewHi && gameScore > 0 ? SFX_MEDAL : SFX_LEVEL);
    gameOverUntil = millis() + 4000;
    return;
  }
  if (ballY > 384) {  // al suelo
    if (++gameMisses >= 3) {
      gameNewHi = (gameScore > pet.gameHi);
      pet.playResult(gameScore);  // actualiza el record y da felicidad
      sfxPlay(gameNewHi && gameScore > 0 ? SFX_MEDAL : SFX_LEVEL);
      gameOverUntil = millis() + 4000;
    } else {
      respawnBall();
    }
  }
  // el bicho la sigue por abajo
  float chase = (ballX - gamePetX) * 0.12f;
  if (chase > 7) chase = 7;
  if (chase < -7) chase = -7;
  gamePetX += chase;
}

// ---------- saco de entrenamiento (entrena la fuerza) ----------

void startSack() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  sackOpen = true;
  sackUntil = millis() + 10000;
  sackOverUntil = 0;
  sackHits = 0;
  sackShake = 0;
  sackNewHi = false;
}

void sackTap() {
  if (millis() >= sackUntil) return;  // ya termino el tiempo
  sackHits++;
  sackShake = 16;  // sacude el saco
}

void drawGameScene();  // prototipo (definida mas abajo)

void renderSack() {
  uint32_t now = millis();
  drawGameScene();  // fondo del habitat
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  // pantalla de resultado
  if (sackOverUntil) {
    if (now > sackOverUntil) { sackOpen = false; return; }
    char b[20];
    snprintf(b, sizeof(b), T(S_HITS_FMT), sackHits);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(CX - strlen(b) * 12, 150);
    gfx->print(b);
    char g[18];
    snprintf(g, sizeof(g), T(S_STR_GAIN_FMT), sackGain);
    gfx->setTextColor(UI_BAR_BAD);
    gfx->setTextSize(3);
    gfx->setCursor(CX - strlen(g) * 9, 210);
    gfx->print(g);
    gfx->setTextSize(2);
    if (sackNewHi && sackHits > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(CX - strlen(T(S_NEW_RECORD)) * 6, 256);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char r[18];
      snprintf(r, sizeof(r), T(S_RECORD_FMT), pet.strHi);
      gfx->setTextColor(ink);
      gfx->setCursor(CX - strlen(r) * 6, 256);
      gfx->print(r);
    }
    gfx->flush();
    return;
  }

  // se acabaron los 10 s: aplicar entrenamiento
  if (now >= sackUntil) {
    sackNewHi = (sackHits > pet.strHi);
    sackGain = pet.trainStrength(sackHits);
    sfxPlay(sackNewHi ? SFX_MEDAL : SFX_PLAY);
    sackOverUntil = now + 3500;
    gfx->flush();
    return;
  }

  // aporreo activo
  sackShake *= 0.84f;
  int off = (int)(sackShake * sinf(now * 0.05f));
  int sx = CX + off, top = 86, sy = 150;
  gfx->fillRect(CX - 3, 56, 6, top - 56, ink);          // gancho/cuerda
  gfx->fillRect(sx - 4, top - 30, 8, 34, ink);          // cadena
  gfx->fillRoundRect(sx - 42, top, 84, 150, 26, C565(0xb5, 0x3a, 0x3a));  // saco
  gfx->fillRoundRect(sx - 42, top, 84, 22, 18, C565(0x7e, 0x28, 0x28));   // tapa
  gfx->drawRoundRect(sx - 42, top, 84, 150, 26, ink);
  gfx->fillRect(sx - 42, top + 70, 84, 4, C565(0x7e, 0x28, 0x28));        // costura

  // contador de golpes
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", sackHits);
  gfx->setTextColor(ink);
  gfx->setTextSize(6);
  gfx->setCursor(CX - strlen(buf) * 18, 268);
  gfx->print(buf);

  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_HIT_FAST)) * 6, 322);
  gfx->print(T(S_HIT_FAST));

  // barra de tiempo
  uint32_t left = sackUntil - now;
  int bw = 280, fw = (int)((uint32_t)bw * left / 10000);
  gfx->fillRoundRect(CX - bw / 2, 350, bw, 16, 5, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(CX - bw / 2, 350, fw, 16, 5, UI_BAR_OK);

  gfx->flush();
}

// fondo del minijuego: hatibat del bicho (cielo por hora + suelo del bioma)
void drawGameScene() {
  int hh = sceneHour();
  bool night = hh < 6 || hh >= 20;
  uint16_t top, bot;
  if (night)       { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (hh < 8) { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }
  else if (hh < 18){ top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }
  else             { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }
  int hor = 376;
  for (int y = 0; y < hor; y += 8)
    gfx->fillRect(0, y, 466, 8, lerp565(top, bot, y, hor));
  if (night)
    drawStars(millis(), top, bot, 225);
  uint8_t bio = sceneBiome();
  uint16_t soil = BIOME_SOIL[bio < BIOME_COUNT ? bio : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  gfx->fillRect(0, hor, 466, 466 - hor, soil);
}

void renderGame() {
  // sin fillScreen(NEGRO): drawGameScene cubre los 466x466 completos. Si el
  // DMA del flush anterior aun lee el buffer, vera contenido valido (no negro
  // a medio pintar), que era el parpadeo a 25 fps.
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (gameOverUntil) {
    drawGameScene();
    if (millis() > gameOverUntil) {
      gameOpen = false;
      return;
    }
    char buf[22];
    snprintf(buf, sizeof(buf), T(S_SCORE_FMT), gameScore);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(CX - strlen(buf) * 12, 160);
    gfx->print(buf);
    gfx->setTextSize(2);
    if (gameNewHi && gameScore > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(CX - strlen(T(S_NEW_RECORD)) * 6, 214);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char rec[20];
      snprintf(rec, sizeof(rec), T(S_RECORD_FMT), pet.gameHi);
      gfx->setTextColor(ink);
      gfx->setCursor(CX - strlen(rec) * 6, 214);
      gfx->print(rec);
    }
    const char *msg = gameScore >= 10 ? T(S_GREAT_JOY) : T(S_PLUS_JOY);
    gfx->setTextColor(ink);
    gfx->setCursor(CX - strlen(msg) * 6, 250);
    gfx->print(msg);
    gfx->flush();
    return;
  }

  drawGameScene();
  stepGame();

  // marcador, record y vidas
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", gameScore);
  gfx->setTextColor(ink);
  gfx->setTextSize(4);
  gfx->setCursor(CX - strlen(buf) * 12, 30);
  gfx->print(buf);
  char rec[12];
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.gameHi);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(rec) * 6, 76);
  gfx->print(rec);
  for (int i = 0; i < 3; i++) {
    if (i < 3 - gameMisses) gfx->fillCircle(180 + i * 28, 104, 6, UI_BAR_BAD);
    else gfx->drawCircle(180 + i * 28, 104, 6, UI_TRACK);
  }
  // The clock, drawn like the bag's and the reaction test's so all three games
  // read the same way. Thin and near the rim: the middle belongs to the ball.
  if (!gameOverUntil) {
    uint32_t now2 = millis();
    uint32_t left = (gameUntil > now2) ? gameUntil - now2 : 0;
    int bw = 200, fw = (int)((uint32_t)bw * left / GAME_MS);
    gfx->fillRoundRect(CX - bw / 2, 124, bw, 10, 4, UI_TRACK);
    if (fw > 2)
      gfx->fillRoundRect(CX - bw / 2, 124, fw, 10, 4,
                         left < 5000 ? UI_BAR_WARN : UI_BAR_OK);
  }

  if (pmd.loaded) {
    uint8_t act = (ballX > gamePetX + 4) ? PMD_WALKR : (ballX < gamePetX - 4) ? PMD_WALKL : PMD_IDLE;
    if (!pmd.has(act)) act = PMD_IDLE;
    drawPmdAct(act, (int)gamePetX, 394, millis(), true, false, 3);
  } else if (mon.loaded) {
    int s = (mon.h * 2 > 130) ? 1 : 2;
    int w = mon.w * s, h = mon.h * s;
    uint16_t fm = mon.frameMs ? mon.frameMs : 100;
    uint16_t fi = (millis() / fm) % mon.frames;
    const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
    int px = (int)gamePetX - w / 2, py = 394 - h;
    for (int r = 0; r < mon.h; r++) {
      const uint8_t *row = fr + r * mon.w;
      forRuns(mon.w, [&](int c) { return indexedKey(row[c], false); },
              [&](int c0, int len, int k) { gfx->fillRect(px + c0 * s, py + r * s, len * s, s, mon.pal[k]); });
    }
  }

  // anillo de impacto que se expande y desvanece (feedback suave del golpe)
  uint32_t ht = millis() - hitTime;
  if (hitTime && ht < 260) {
    int rad = 22 + (int)(ht / 6);
    gfx->drawCircle((int)hitX, (int)hitY, rad, C565(0xff, 0xe7, 0x9f));
    gfx->drawCircle((int)hitX, (int)hitY, rad - 2, C565(0xff, 0xd9, 0x8a));
  }

  // la pokeball
  drawMap(SPR_ICON_PLAY, 16, (int)ballX - 24, (int)ballY - 24, 3, false);

  gfx->flush();
}

// ---------- ficha del bicho (deslizar vertical) ----------

// una fila de la ficha: etiqueta, barra, valor y (si iv != IV_NONE) el valor
// individual que fija el techo de ese stat
// (sin argumento por defecto: el generador de prototipos de Arduino los
// descarta y las llamadas que lo omitan no compilarian)
#define IV_NONE 0xFF
void drawCardStat(int y, const char *label, uint16_t val, uint16_t maxBar,
                  uint16_t color, uint8_t iv) {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(70, y);
  gfx->print(label);
  // The bar used to start at 112, which leaves 42px for a label drawn at size 2
  // -- three characters. BOND (EN), LIEN (FR) and LACO (PT) are four, so the
  // label ran under the bar. 132 fits five, with the bar narrowed to keep the
  // number clear of it.
  int bw = 130;
  int fw = (int)val * bw / maxBar;
  if (fw > bw) fw = bw;
  gfx->fillRoundRect(132, y + 2, bw, 11, 3, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(132, y + 2, fw, 11, 3, color);
  char num[8];
  snprintf(num, sizeof(num), "%u", val);
  gfx->setCursor(272, y);
  gfx->print(num);
  if (iv != IV_NONE) {
    char b[10];
    snprintf(b, sizeof(b), T(S_IV_FMT), iv);
    // un IV perfecto se resalta: es el golpe de suerte que el jugador busca
    gfx->setTextColor(iv >= 31 ? UI_BAR_WARN : UI_TRACK);
    gfx->setCursor(344, y);
    gfx->print(b);
  }
}

// ---------- ajuste de hora en pantalla (deslizar abajo) ----------
// El usuario pone su hora LOCAL a ojo; el firmware la usa tal cual, asi que
// no hay que gestionar zona horaria. Preserva el dia (no rompe racha/edad).

void openClock() {
  uint32_t e = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  clockH = (e / 3600) % 24;
  clockM = (e / 60) % 60;
  clockOpen = true;
  clockPage = 0;         // always opens on the clock, never on wherever it was
  resetConfirm = false;  // and never with a wipe half-asked
}

void applyClock() {
  uint32_t base = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  uint32_t e = (base / 86400) * 86400 + (uint32_t)clockH * 3600 + (uint32_t)clockM * 60;
  rtcSetEpoch(e);
  pet.setClock(e);
  clockOpen = false;
}

void drawClockBtn(int x, int y, const char *l) {
  gfx->fillRoundRect(x, y, 58, 58, 12, UI_WHITE);
  gfx->drawRoundRect(x, y, 58, 58, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(4);
  gfx->setCursor(x + 17, y + 15);
  gfx->print(l);
}

// ---------------------------------------------------------------------------
// SETTINGS IS TWO PAGES NOW, and it had to become two.
//
// It was one, and full: the clock took the top half and a single 30 px strip at
// y 296 carried the sound switch, the volume mixer and the language pill across
// 34..432 with nothing between them. There was no room for brightness, let alone
// an in-game reset, and the strip's controls were already only 30 px tall --
// under the finger floor CLAUDE.md section 4 keeps warning about.
//
// It pages through the RIM, like every other paged screen, so no new gesture is
// invented and the arc scrollbar advertises the second page by itself. Splitting
// on the OK button is what makes the split honest: only the TIME needs applying,
// and everything on page 1 takes effect the moment it is touched.
//
// CLOCK_PAGES and clockPage live up with the other screen state, because
// uiRimTarget() reaches them ~900 lines above here.

// Page 1's rows. As wide as the glass allows AT THEIR OWN HEIGHT, the same rule
// gymRowRect() follows -- one width for every row wastes the middle and hangs the
// bottom row's corners off the bezel.
// Its metrics live up with the other layout constants, because uiButtonHeights()
// holds SET_ROW_H to the finger floor ~1700 lines above here.
// The -/+ pair on a stepper row sits at its two ends, so the label and the bar
// own the middle.
#define SET_STEP_W 54
#define SET_NAV_Y 372
#define SET_NAV_H UI_TAP_MIN
#define SET_NAV_BTN_W 54
#define SET_NAV_GAP 8

// One answer for where a settings row is, asked by the draw path, the tap path
// and hit_test. Three copies of this arithmetic is how a control ends up
// somewhere its graphic is not.
void setRowRect(int i, int *x, int *y, int *w, int *h) {
  int ry = SET_ROW_Y(i);
  int half = uiSafeHalfWidthFor(ry, ry + SET_ROW_H) - SET_ROW_INSET;
  if (x) *x = CX - half;
  if (y) *y = ry;
  if (w) *w = half * 2;
  if (h) *h = SET_ROW_H;
}
void setNavRect(int which, int *x, int *y, int *w, int *h) {
  int half = uiSafeHalfWidthFor(SET_NAV_Y, SET_NAV_Y + SET_NAV_H) - 20;
  if (which < 0) {
    if (x) *x = CX - half;
    if (w) *w = SET_NAV_BTN_W;
  } else if (which > 0) {
    if (x) *x = CX + half - SET_NAV_BTN_W;
    if (w) *w = SET_NAV_BTN_W;
  } else {
    if (x) *x = CX - half + SET_NAV_BTN_W + SET_NAV_GAP;
    if (w) *w = half * 2 - 2 * (SET_NAV_BTN_W + SET_NAV_GAP);
  }
  if (y) *y = SET_NAV_Y;
  if (h) *h = SET_NAV_H;
}
int uiSettingsRowCenterY(int i) { return SET_ROW_Y(i) + SET_ROW_H / 2; }
int uiSettingsRows() { return SET_ROWS; }
int uiBrightMax() { return BRIGHT_LEVELS; }
int uiBrightDefault() { return BRIGHT_DEFAULT; }

// The training menu and quiz geometry, published for hit_test
// so it holds them to the finger floor and the round bezel without keeping its own
// copy of either -- a test that restates a layout proves the transcription and
// then drifts from it.
int uiTrainRows() { return TRAIN_ROWS; }
int uiTrainRowH() { return TRAIN_ROW_H; }
int uiTrainRowTop(int i) { return TRAIN_ROW_Y(i); }
int uiTrainPanelTop() { return TRAIN_Y; }
int uiTrainPanelBot() { return TRAIN_Y + TRAIN_H; }
int uiTrainPanelHalfW() { return TRAIN_W / 2; }
int uiQuizOpts() { return QUIZ_OPTS; }
int uiQuizOptH() { return QUIZ_OPT_H; }

static const char *const LANG_CODES[LANG_COUNT] = { "ES", "EN", "FR", "DE", "IT", "PT", "NL" };

// Page 1: the clock.
static void renderClockTime() {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_SET_TIME)) * 9, 30);
  gfx->print(T(S_SET_TIME));

  char t[8];
  snprintf(t, sizeof(t), "%02d:%02d", clockH, clockM);
  gfx->setTextSize(7);
  gfx->setCursor(CX - 105, 100);
  gfx->print(t);

  drawClockBtn(104, 184, "-");  // hora -
  drawClockBtn(170, 184, "+");  // hora +
  drawClockBtn(252, 184, "-");  // min -
  drawClockBtn(318, 184, "+");  // min +
  gfx->setTextSize(2);
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setCursor(120, 250);
  gfx->print(T(S_HOUR));
  gfx->setCursor(276, 250);
  gfx->print(T(S_MIN));

  gfx->fillRoundRect(133, 310, 200, 48, 14, UI_BAR_OK);
  gfx->setTextColor(UI_BG_DAY);
  gfx->setTextSize(3);
  gfx->setCursor(CX - 18, 322);
  gfx->print("OK");
}

// A stepper row: label, a level bar, and -/+ at the two ends. Volume and
// brightness are the same control over different state, so they are drawn by the
// same function -- two copies would drift the moment one of them was retuned.
// `fmt` carries its own %u, so the volume row keeps using the string it always
// had ("VOL %u") rather than needing a bare label added beside it in all six
// languages -- and STRINGS is positional, so every avoidable entry is one fewer
// chance to shift a row.
static void drawStepperRow(int i, const char *fmt, uint8_t v, uint8_t vmax,
                           uint16_t barCol) {
  int rx, ry, rw, rh;
  setRowRect(i, &rx, &ry, &rw, &rh);
  gfx->fillRoundRect(rx, ry, rw, rh, 10, UI_BG_DAY);
  gfx->drawRoundRect(rx, ry, rw, rh, 10, UI_INK);
  for (int b = 0; b < 2; b++) {
    const bool plus = (b != 0);
    const int bx = plus ? rx + rw - SET_STEP_W : rx;
    const bool live = plus ? (v < vmax) : (v > 0);
    gfx->fillRoundRect(bx, ry, SET_STEP_W, rh, 10, live ? UI_WHITE : UI_TRACK);
    gfx->drawRoundRect(bx, ry, SET_STEP_W, rh, 10, UI_INK);
    gfx->setTextColor(live ? UI_INK : 0x8410);
    gfx->setTextSize(3);
    gfx->setCursor(bx + SET_STEP_W / 2 - 8, ry + rh / 2 - 11);
    gfx->print(plus ? "+" : "-");
  }
  const int mid = rx + SET_STEP_W, midW = rw - SET_STEP_W * 2;
  char l[24];
  snprintf(l, sizeof(l), fmt, (unsigned)v);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(mid + (midW - (int)strlen(l) * 12) / 2, ry + 6);
  gfx->print(l);
  const int barW = midW - 24;
  gfx->fillRoundRect(mid + 12, ry + rh - 14, barW, 8, 3, UI_TRACK);
  if (v) gfx->fillRoundRect(mid + 12, ry + rh - 14, barW * v / vmax, 8, 3, barCol);
}

// Page 0: everything that takes effect the moment it is touched (opens first).
static void renderClockOptions() {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_SETTINGS)) * 9, 30);
  gfx->print(T(S_SETTINGS));

  int rx, ry, rw, rh;
  // sound: a full-width switch, so the master control is unmissable
  {
    setRowRect(SET_SOUND, &rx, &ry, &rw, &rh);
    const bool snd = audioEnabled();
    const char *sl = snd ? T(S_SND_ON) : T(S_SND_OFF);
    gfx->fillRoundRect(rx, ry, rw, rh, 10, snd ? UI_BAR_OK : UI_WHITE);
    gfx->drawRoundRect(rx, ry, rw, rh, 10, UI_INK);
    gfx->setTextColor(snd ? UI_BG_DAY : UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(sl) * 6, ry + rh / 2 - 8);
    gfx->print(sl);
  }
  drawStepperRow(SET_VOL, T(S_VOL_FMT), audioVolume(), 10, UI_BAR_OK);
  drawStepperRow(SET_BRIGHT, T(S_BRIGHT_FMT), gBrightLevel, BRIGHT_LEVELS,
                 UI_BAR_WARN);
  // language: a pill that cycles
  {
    setRowRect(SET_LANG, &rx, &ry, &rw, &rh);
    gfx->fillRoundRect(rx, ry, rw, rh, 10, UI_WHITE);
    gfx->drawRoundRect(rx, ry, rw, rh, 10, UI_INK);
    char lp[24];
    snprintf(lp, sizeof(lp), "%s  %s >", T(S_LANG_LABEL), LANG_CODES[gLang]);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(lp) * 6, ry + rh / 2 - 8);
    gfx->print(lp);
  }
  // reset: drawn as the destructive thing it is, and it only ever OPENS the
  // confirm -- see clockTap()
  {
    setRowRect(SET_RESET, &rx, &ry, &rw, &rh);
    gfx->fillRoundRect(rx, ry, rw, rh, 10, UI_TRACK);
    gfx->drawRoundRect(rx, ry, rw, rh, 10, UI_BAR_BAD);
    gfx->setTextColor(UI_BAR_BAD);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(T(S_RESET)) * 6, ry + rh / 2 - 8);
    gfx->print(T(S_RESET));
  }
}

// ---------- page 3: GAME ----------------------------------------------------
// The settings that change how a life runs: real minutes per level (1..60),
// farewell level (50..100), and whether eggs prefer unfinished lines. Each
// numeric value sits between four buttons (-10, -1, +1, +10) that stop at the
// ends rather than wrapping, with a "ONE LIFE" duration readout underneath.
#define GAME_ROW_H 44
#define GAME_BTN_W 48
#define GAME_BTN_GAP 4
#define GAME_HALF 170          // the row spans CX - 170 .. CX + 170
#define GAME_LBL_Y(r) (80 + (r) * 74)
#define GAME_ROW_Y(r) (GAME_LBL_Y(r) + 20)
#define GAME_LIFE_Y 226
#define GAME_EGG_Y 252
#define GAME_EGG_H UI_TAP_MIN
#define GAME_STEP_Y 308
#define GAME_STEP_H UI_TAP_MIN

void setStepCounter(bool on) {
  stepCounterSet(on);
  imuSetEnabled(on);
}

static void gameToggleRect(int y, int h, int *rx, int *rw) {
  int half = uiSafeHalfWidthFor(y, y + h) - 18;
  *rx = CX - half;
  *rw = half * 2;
}

int gameStepAt(int r, int16_t x, int16_t y) {
  if (y < GAME_ROW_Y(r) || y > GAME_ROW_Y(r) + GAME_ROW_H) return 0;
  const int l0 = CX - GAME_HALF, l1 = l0 + GAME_BTN_W + GAME_BTN_GAP;
  const int r1 = CX + GAME_HALF - GAME_BTN_W, r0 = r1 - GAME_BTN_W - GAME_BTN_GAP;
  if (x >= l0 && x < l0 + GAME_BTN_W) return -10;
  if (x >= l1 && x < l1 + GAME_BTN_W) return -1;
  if (x >= r0 && x < r0 + GAME_BTN_W) return 1;
  if (x >= r1 && x < r1 + GAME_BTN_W) return 10;
  return 0;
}
int uiGameRowY(int r) { return GAME_ROW_Y(r) + GAME_ROW_H / 2; }
int uiGameEggY() { return GAME_EGG_Y + GAME_EGG_H / 2; }
int uiGameStepY() { return GAME_STEP_Y + GAME_STEP_H / 2; }
bool gameEggAt(int16_t x, int16_t y) {
  int rx, rw;
  gameToggleRect(GAME_EGG_Y, GAME_EGG_H, &rx, &rw);
  return y >= GAME_EGG_Y && y <= GAME_EGG_Y + GAME_EGG_H && x >= rx && x <= rx + rw;
}
bool gameStepToggleAt(int16_t x, int16_t y) {
  int rx, rw;
  gameToggleRect(GAME_STEP_Y, GAME_STEP_H, &rx, &rw);
  return y >= GAME_STEP_Y && y <= GAME_STEP_Y + GAME_STEP_H && x >= rx && x <= rx + rw;
}
int uiGameBtnX(int step) {
  const int l0 = CX - GAME_HALF, l1 = l0 + GAME_BTN_W + GAME_BTN_GAP;
  const int r1 = CX + GAME_HALF - GAME_BTN_W, r0 = r1 - GAME_BTN_W - GAME_BTN_GAP;
  const int x = step == -10 ? l0 : step == -1 ? l1 : step == 1 ? r0 : r1;
  return x + GAME_BTN_W / 2;
}

static void drawGameRow(int r, const char *label, int v, int lo, int hi) {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(label) * 6, GAME_LBL_Y(r));
  gfx->print(label);
  const int y = GAME_ROW_Y(r);
  static const int steps[4] = { -10, -1, 1, 10 };
  static const char *const glyph[4] = { "<<", "<", ">", ">>" };
  for (int k = 0; k < 4; k++) {
    const int bx = uiGameBtnX(steps[k]) - GAME_BTN_W / 2;
    const bool live = steps[k] < 0 ? v > lo : v < hi;
    gfx->fillRoundRect(bx, y, GAME_BTN_W, GAME_ROW_H, 10, live ? UI_WHITE : UI_TRACK);
    gfx->drawRoundRect(bx, y, GAME_BTN_W, GAME_ROW_H, 10, UI_INK);
    gfx->setTextColor(live ? UI_INK : 0x8410);
    gfx->setTextSize(2);
    gfx->setCursor(bx + GAME_BTN_W / 2 - (int)strlen(glyph[k]) * 6, y + GAME_ROW_H / 2 - 8);
    gfx->print(glyph[k]);
  }
  char val[8];
  snprintf(val, sizeof(val), "%d", v);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)strlen(val) * 9, y + GAME_ROW_H / 2 - 11);
  gfx->print(val);
}

// "2 d 11 h", "5 h 40 min", "49 min": a life from hatching to the farewell.
void lifeSpanText(char *out, size_t cap) {
  const uint32_t mins = (uint32_t)(gFarewellLevel - 1) * gLevelMins;
  const uint32_t d = mins / 1440, h = (mins % 1440) / 60, m = mins % 60;
  if (d) snprintf(out, cap, "%lu d %lu h", (unsigned long)d, (unsigned long)h);
  else if (h) snprintf(out, cap, "%lu h %lu min", (unsigned long)h, (unsigned long)m);
  else snprintf(out, cap, "%lu min", (unsigned long)m);
}

void renderGameOptions() {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)strlen(T(S_GAME_TITLE)) * 9, 30);
  gfx->print(T(S_GAME_TITLE));
  drawGameRow(0, T(S_LEVEL_SPEED_FMT), gLevelMins, LEVEL_SPEED_MIN, LEVEL_SPEED_MAX);
  drawGameRow(1, T(S_FAREWELL_LVL), gFarewellLevel, FAREWELL_LVL_MIN, FAREWELL_LVL_MAX);
  char span[24], life[40];
  lifeSpanText(span, sizeof(span));
  snprintf(life, sizeof(life), T(S_LIFE_FMT), span);
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(life) * 6, GAME_LIFE_Y);
  gfx->print(life);

  // Eggs toggle pill
  {
    int rx, rw;
    gameToggleRect(GAME_EGG_Y, GAME_EGG_H, &rx, &rw);
    const char *lbl = T(S_EGG_NEW);
    const char *yn = T(gEggPreferNew ? S_YES : S_NO);
    gfx->fillRoundRect(rx, GAME_EGG_Y, rw, GAME_EGG_H, 10, gEggPreferNew ? UI_BAR_OK : UI_WHITE);
    gfx->drawRoundRect(rx, GAME_EGG_Y, rw, GAME_EGG_H, 10, UI_INK);
    gfx->setTextColor(gEggPreferNew ? UI_BG_DAY : UI_INK);
    gfx->setTextSize(2);
    const int ty = GAME_EGG_Y + GAME_EGG_H / 2 - 8;
    const int lw = (int)strlen(lbl) * 12, yw = (int)strlen(yn) * 12;
    if (lw + yw + 36 <= rw) {
      gfx->setCursor(rx + 14, ty);
      gfx->print(lbl);
      gfx->setCursor(rx + rw - 14 - yw, ty);
      gfx->print(yn);
    } else {
      char comb[48];
      snprintf(comb, sizeof(comb), "%s %s", lbl, yn);
      gfx->setCursor(CX - (int)strlen(comb) * 6, ty);
      gfx->print(comb);
    }
  }

  // Step counter toggle pill
  {
    int rx, rw;
    gameToggleRect(GAME_STEP_Y, GAME_STEP_H, &rx, &rw);
    const char *sl = T(gStepCounterEnabled ? S_STEP_ON : S_STEP_OFF);
    gfx->fillRoundRect(rx, GAME_STEP_Y, rw, GAME_STEP_H, 10, gStepCounterEnabled ? UI_BAR_OK : UI_WHITE);
    gfx->drawRoundRect(rx, GAME_STEP_Y, rw, GAME_STEP_H, 10, UI_INK);
    gfx->setTextColor(gStepCounterEnabled ? UI_BG_DAY : UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(sl) * 6, GAME_STEP_Y + GAME_STEP_H / 2 - 8);
    gfx->print(sl);
  }
}

void gameOptionsTap(int16_t x, int16_t y) {
  if (gameEggAt(x, y)) {
    eggPreferNewSet(!gEggPreferNew);
    sfxPlay(SFX_TAP);
    return;
  }
  if (gameStepToggleAt(x, y)) {
    setStepCounter(!gStepCounterEnabled);
    sfxPlay(SFX_TAP);
    return;
  }
  for (int r = 0; r < 2; r++) {
    const int step = gameStepAt(r, x, y);
    if (!step) continue;
    if (r == 0) {
      const uint8_t before = gLevelMins;
      levelSpeedSet((int)gLevelMins + step);
      sfxPlay(gLevelMins != before ? SFX_TAP : SFX_DENY);
    } else {
      const uint8_t before = gFarewellLevel;
      farewellLevelSet((int)gFarewellLevel + step);
      sfxPlay(gFarewellLevel != before ? SFX_TAP : SFX_DENY);
    }
    return;
  }
}

void renderClock() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  if (clockPage == 0) renderClockOptions();
  else if (clockPage == 1) renderClockTime();
  else renderGameOptions();

  // page indicator (and battery readout on Audio & Display) under the title
  {
    char sub[28];
    int pc = (clockPage == 0) ? batPercent() : -1;
    if (clockPage == 0 && pc >= 0)
      snprintf(sub, sizeof(sub), "%u/%u  %s %d%%", (unsigned)(clockPage + 1),
               (unsigned)CLOCK_PAGES, batCharging() ? "CHG" : "BAT", pc);
    else
      snprintf(sub, sizeof(sub), "%u/%u", (unsigned)(clockPage + 1),
               (unsigned)CLOCK_PAGES);
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(sub) * 6, 60);
    gfx->print(sub);
  }

  // shared footer navigation bar: <  CLOSE  >
  for (int which = -1; which <= 1; which++) {
    int nx, ny, nw, nh;
    setNavRect(which, &nx, &ny, &nw, &nh);
    const bool live = (which == 0) || (which < 0 ? clockPage > 0 : clockPage + 1 < CLOCK_PAGES);
    gfx->fillRoundRect(nx, ny, nw, nh, 10, live ? UI_WHITE : UI_TRACK);
    gfx->drawRoundRect(nx, ny, nw, nh, 10, UI_INK);
    gfx->setTextColor(live ? UI_INK : 0x8410);
    if (which == 0) {
      const char *bl = T(S_CLOSE);
      gfx->setTextSize(2);
      gfx->setCursor(nx + (nw - (int)strlen(bl) * 12) / 2, ny + nh / 2 - 8);
      gfx->print(bl);
    } else {
      gfx->setTextSize(3);
      gfx->setCursor(nx + nw / 2 - 9, ny + nh / 2 - 11);
      gfx->print(which < 0 ? "<" : ">");
    }
  }

  // version del firmware (discreta, abajo del todo)
  char ver[20];
  snprintf(ver, sizeof(ver), "TamaPoke v%s", FW_VERSION);
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(ver) * 3, 436);
  gfx->print(ver);
  // the arc scrollbar and the tile dots, so the other pages advertise themselves
  uiChrome();
  // Drawn LAST and over everything, because it is modal: while it is up nothing
  // else on the screen responds (clockTap checks it first).
  if (resetConfirm)
    drawConfirmPanel(T(S_RESET_Q), T(S_RESET_COST), nullptr, UI_BAR_BAD,
                     T(S_RESET), UI_BAR_BAD, UI_WHITE, T(S_NO), UI_BAR_OK, UI_WHITE);
  gfx->flush();
}

void clockTap(int16_t x, int16_t y) {
  // MODAL FIRST. The confirm covers the rows underneath it, and its NO button
  // sits over the reset row -- so with the rows tested first, declining the wipe
  // would re-open the very dialog it was declining.
  if (resetConfirm) {
    int c1t, c1b, c2t, c2b;
    uiConfirmRects(&c1t, &c1b, &c2t, &c2b);
    if (x >= CONFIRM_BTN_X && x <= CONFIRM_BTN_X + CONFIRM_BTN_W) {
      if (y >= c1t && y <= c1b) {            // yes, wipe it
        resetConfirm = false;
        // Record the intent, stop anything writing the old creature back, and
        // restart. The wipe itself happens at the top of the next boot, before
        // any of this is loaded again -- see save.h section RESET_NS.
        resetArm();
        if (!resetArmed()) {
          Serial.println("reset: intent could not be verified; save left intact");
          sfxPlay(SFX_DENY);
          return;
        }
        saveInhibited = true;
        Serial.println("reset: armed by the player; restarting");
        delay(100);
        ESP.restart();
        return;
      }
      if (y >= c2t && y <= c2b) {            // no
        resetConfirm = false;
        sfxPlay(SFX_TAP);
        return;
      }
    }
    return;      // anything else while it is up does nothing at all
  }

  for (int which = -1; which <= 1; which++) {
    int nx, ny, nw, nh;
    setNavRect(which, &nx, &ny, &nw, &nh);
    if (x >= nx && x <= nx + nw && y >= ny && y <= ny + nh) {
      if (which < 0) {
        if (clockPage > 0) { clockPage--; sfxPlay(SFX_TAP); }
        else sfxPlay(SFX_DENY);
      } else if (which > 0) {
        if (clockPage + 1 < CLOCK_PAGES) { clockPage++; sfxPlay(SFX_TAP); }
        else sfxPlay(SFX_DENY);
      } else {
        clockOpen = false;
        sfxPlay(SFX_TAP);
      }
      return;
    }
  }

  if (clockPage == 2) { gameOptionsTap(x, y); return; }
  if (clockPage == 0) {
    for (int i = 0; i < SET_ROWS; i++) {
      int rx, ry, rw, rh;
      setRowRect(i, &rx, &ry, &rw, &rh);
      if (x < rx || x > rx + rw || y < ry || y > ry + rh) continue;
      if (i == SET_SOUND) {
        audioSetEnabled(!audioEnabled());
        if (audioEnabled()) sfxPlay(SFX_TAP);    // confirma al encender
        return;
      }
      if (i == SET_VOL || i == SET_BRIGHT) {
        const bool minus = (x < rx + SET_STEP_W);
        const bool plus = (x > rx + rw - SET_STEP_W);
        if (!minus && !plus) {
          const int barX0 = rx + SET_STEP_W + 12;
          const int barW = rw - SET_STEP_W * 2 - 24;
          int rel = x - barX0;
          if (rel < 0) rel = 0;
          if (rel > barW) rel = barW;
          if (i == SET_VOL) {
            uint8_t v = (uint8_t)((rel * 10 + barW / 2) / barW);
            if (v > 10) v = 10;
            audioSetVolume(v);
            sfxPlay(SFX_TAP);
          } else {
            uint8_t lv = (uint8_t)(1 + (rel * (BRIGHT_LEVELS - 1) + barW / 2) / barW);
            if (lv > BRIGHT_LEVELS) lv = BRIGHT_LEVELS;
            brightSetLevel(lv);
            sfxPlay(SFX_TAP);
          }
          return;
        }
        if (i == SET_VOL) {
          uint8_t v = audioVolume();
          if (plus && v < 10) audioSetVolume(v + 1);
          else if (minus && v > 0) audioSetVolume(v - 1);
          sfxPlay(SFX_TAP);                      // so the new level is audible
        } else {
          // 1, not 0: see brightBase(). A settings screen that could reach a
          // black panel would leave no visible way back from it.
          if (plus) brightSetLevel(gBrightLevel + 1);
          else if (gBrightLevel > 1) brightSetLevel(gBrightLevel - 1);
          sfxPlay(SFX_TAP);
        }
        return;
      }
      if (i == SET_LANG) {
        setLang((Lang)((gLang + 1) % LANG_COUNT));
        sfxPlay(SFX_TAP);
        return;
      }
      if (i == SET_RESET) {                      // ASKS. Always.
        resetConfirm = true;
        sfxPlay(SFX_DENY);
        return;
      }
    }
    return;
  }

  if (y >= 184 && y <= 242) {  // fila de botones +/-
    if (x >= 104 && x < 162) clockH = (clockH + 23) % 24;
    else if (x >= 170 && x < 228) clockH = (clockH + 1) % 24;
    else if (x >= 252 && x < 310) clockM = (clockM + 59) % 60;
    else if (x >= 318 && x < 376) clockM = (clockM + 1) % 60;
    return;
  }
  if (y >= 310 && y <= 358 && x >= 133 && x <= 333) { applyClock(); return; }
}

// llama + numero de racha arriba a la izquierda
void drawStreakBadge() {
  if (pet.streak < 1) return;
  int x = 26, y = 16;
  gfx->fillTriangle(x + 8, y, x + 1, y + 17, x + 15, y + 17, UI_BAR_BAD);
  gfx->fillTriangle(x + 8, y + 7, x + 4, y + 17, x + 12, y + 17, UI_BAR_WARN);
  char s[6];
  snprintf(s, sizeof(s), "%u", pet.streak);
  gfx->setTextColor(inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(x + 22, y + 2);
  gfx->print(s);
}

// Compact daily step counter pill centered below statusMsg(). Hidden whenever
// the player turns the step counter off in Settings -> GAME.
void drawStepPill() {
  if (!gStepCounterEnabled) return;
  const int w = 132, h = 24;
  const int x = CX - w / 2, y = 114;
  const uint32_t steps = pet.stepsToday();
  const uint16_t ink = inkColor();
  gfx->fillRoundRect(x, y, w, h, 10, gNight ? C565(0x1c, 0x26, 0x44) : UI_WHITE);
  gfx->drawRoundRect(x, y, w, h, 10, ink);

  // Two small staggered footprints on the left of the pill
  for (int f = 0; f < 2; f++) {
    int fx = x + 10 + f * 6;
    int fy = y + (f ? 7 : 4);
    gfx->fillRoundRect(fx, fy, 4, 6, 1, ink);
    gfx->fillRect(fx + 1, fy + 7, 2, 2, ink);
  }

  char buf[8];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)steps);
  const int tw = (int)strlen(buf) * 12;
  const int areaX = x + 28, areaW = w - 36;
  gfx->setTextColor(ink);
  gfx->setTextSize(2);
  gfx->setCursor(areaX + (areaW - tw) / 2, y + 3);
  gfx->print(buf);

  const int barX = x + 10, barY = y + h - 5, barW = w - 20;
  gfx->fillRoundRect(barX, barY, barW, 3, 1, UI_TRACK);
  const uint32_t capped = steps > STEP_GOAL ? STEP_GOAL : steps;
  int fillW = (int)(capped * (uint32_t)barW / STEP_GOAL);
  if (fillW > 0)
    gfx->fillRoundRect(barX, barY, fillW, 3, 1, steps >= STEP_GOAL ? UI_BAR_WARN : UI_BAR_OK);
}

// banner temporal: medalla nueva o hito de racha
void drawCelebration() {
  const char *l1 = nullptr, *l2 = nullptr;
  char buf[20];
  if (pet.showMedal()) {
    for (int i = 0; i < MED_COUNT; i++)
      if (pet.newMedal & (1 << i)) { l2 = medalName(i); break; }
    l1 = T(S_MEDAL_BANNER);
  } else if (pet.showMilestone()) {
    snprintf(buf, sizeof(buf), T(S_STREAK_DAYS_FMT), pet.streak);
    l1 = T(S_GREAT);
    l2 = buf;
  }
  if (!l1) return;
  gfx->fillRoundRect(73, 150, 320, 96, 16, UI_BAR_WARN);
  gfx->drawRoundRect(73, 150, 320, 96, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(l1) * 9, 176);
  gfx->print(l1);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(l2) * 6, 212);
  gfx->print(l2);
}

// medallas en la ficha: badge con etiqueta, color si conseguida
void drawMedalBadge(int x, int y, int i) {
  bool got = pet.hasMedal(1 << i);
  gfx->fillRoundRect(x, y, 100, 24, 6, got ? UI_BAR_OK : UI_TRACK);
  if (!got) gfx->drawRoundRect(x, y, 100, 24, 6, UI_TRACK);
  gfx->setTextColor(got ? UI_BG_DAY : 0x9492);
  gfx->setTextSize(2);
  gfx->setCursor(x + (100 - (int)strlen(medalLabel(i)) * 12) / 2, y + 5);
  gfx->print(medalLabel(i));
}

// pagina 0: perfil (retrato grande, identidad, racha, vinculo, baya)
void renderCardProfile() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  const char *nm = pet.nick[0] ? pet.nick : d.name;
  char head[26];
  snprintf(head, sizeof(head), T(S_NAME_FMT), pet.shiny ? "*" : "", nm, pet.level());
  gfx->setTextColor(d.accent);
  // auto-encoge: a tamano 3 los nombres largos no caben en la franja estrecha de
  // arriba de la pantalla redonda, asi que se cortaban por el borde
  int hlen = strlen(head);
  int hts = (hlen <= 11) ? 3 : 2;
  gfx->setTextSize(hts);
  gfx->setCursor(CX - hlen * (hts == 3 ? 9 : 6), hts == 3 ? 34 : 40);
  gfx->print(head);
  if (pet.nick[0]) {  // especie real bajo el apodo
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (strlen(d.name) + 2) * 6, 64);
    gfx->printf("(%s)", d.name);
  }

  // retrato grande animado
  if (pmd.loaded) drawPmdAct(PMD_IDLE, CX, 206, millis(), true, false, 4);

  // racha con llama
  int sx = 138, sy = 224;
  gfx->fillTriangle(sx + 8, sy, sx + 1, sy + 18, sx + 15, sy + 18, UI_BAR_BAD);
  gfx->fillTriangle(sx + 8, sy + 7, sx + 4, sy + 18, sx + 12, sy + 18, UI_BAR_WARN);
  char rl[30];
  snprintf(rl, sizeof(rl), T(S_STREAK_FMT), pet.streak, pet.bestStreak);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(sx + 24, sy + 2);
  gfx->print(rl);

  drawCardStat(258, T(S_VIN), pet.bond, 100, C565(0xd4, 0x52, 0x7e), IV_NONE);

  const char *berry = !pet.berryKnown ? T(S_BERRY_UNK)
                      : pet.lovesBerry(0) ? T(S_BERRY_RED)
                      : pet.lovesBerry(1) ? T(S_BERRY_BLUE)
                                          : T(S_BERRY_GREEN);
  char info[40];
  snprintf(info, sizeof(info), T(S_INFO_FMT), berry,
           (unsigned long)(pet.realAgeMin / 1440));   // REAL days, at any speed
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(info) * 6, 296);
  gfx->print(info);

  // Its temperament, beside the berry it likes -- the other thing about this
  // individual that is true from the moment it hatches and never changes. Derived
  // from the IVs, so it costs the save nothing; see Pet::personality().
  gfx->setTextColor(DEX_TBL[pet.speciesId].accent);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(personalityName()) * 6, 320);
  gfx->print(personalityName());

  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(1);
  gfx->setCursor(CX - strlen(T(S_RENAME_HINT)) * 3, 346);
  gfx->print(T(S_RENAME_HINT));
}

// pagina 1: combate (4 barras + boton de entrenar)
void renderCardStats() {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_BATTLE)) * 9, 44);
  gfx->print(T(S_BATTLE));

  // typing, in the accent colour of the species (English in every language,
  // same as the species names themselves)
  const DexEntry &de = DEX_TBL[pet.speciesId];
  char ty[24];
  if (de.type2 == T_NONE) snprintf(ty, sizeof(ty), "%s", typeName(de.type1));
  else snprintf(ty, sizeof(ty), "%s/%s", typeName(de.type1), typeName(de.type2));
  gfx->setTextColor(de.accent);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(ty) * 6, 76);
  gfx->print(ty);

  // 360 de tope de barra: a nivel 73 (fin de ciclo) el stat mas alto de toda
  // la dex es la vitalidad de CHANSEY (355). El 260 anterior ya se desbordaba.
  drawCardStat(104, T(S_STAT_ATK), pet.atkStat(), 360, UI_BAR_BAD, pet.ivAtk);
  drawCardStat(144, T(S_STAT_DEF), pet.defStat(), 360, 0x4C98, pet.ivDef);
  drawCardStat(184, T(S_STAT_SPE), pet.speStat(), 360, UI_BAR_WARN, pet.ivSpe);
  drawCardStat(224, T(S_STAT_VIT), pet.vitStat(), 360, UI_BAR_OK, pet.ivHp);
  drawCardStat(264, T(S_STAT_WGT), pet.weight, 100, 0xB3C8, IV_NONE);

}

// Draws one move as a row: name, its type in the type's own colour, and either
// power or a STATUS marker. Shared by the moves page and the picker so a move
// looks the same wherever you meet it.
// A filled chip in the type's own colour, label in whichever of black/white
// reads on it. Returns its width so a caller can lay out beside it.
int drawTypeChip(int x, int y, uint8_t type) {
  const char *nm = typeName(type);
  int w = (int)strlen(nm) * 6 + 10;
  gfx->fillRoundRect(x, y, w, 15, 4, typeColor(type));
  gfx->setTextSize(1);
  gfx->setTextColor(typeColorIsLight(type) ? UI_INK : UI_WHITE);
  gfx->setCursor(x + 5, y + 4);
  gfx->print(nm);
  return w;
}

void drawMoveRow(int y, uint8_t mv, bool highlight, int16_t dex) {
  gfx->fillRoundRect(70, y, 326, 50, 12, highlight ? UI_BAR_WARN : UI_BG_DAY);
  gfx->drawRoundRect(70, y, 326, 50, 12, UI_INK);
  if (!mv) {
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(T(S_MOVE_EMPTY)) * 6, y + 17);
    gfx->print(T(S_MOVE_EMPTY));
    return;
  }
  const MoveEntry &m = MOVE_TBL[mv];
  const char *name = moveName(mv);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(82, y + 8);
  gfx->print(name);
  // There is no per-type palette (DexEntry.accent is per species), and inventing
  // one by hand would duplicate what gen_dex.py generates. Colouring same-type
  // moves in the species accent is more useful anyway: STAB is a 1.5x damage
  // bonus, so this marks the moves that actually hit hardest for this creature.
  // The chip carries the TYPE; STAB moved onto the power figure, where it
  // belongs -- STAB is a damage bonus, so saying it next to the damage reads
  // straight, and it leaves the type free to be its own colour.
  bool stab = hasStab(dex, m.type) && m.cat != MC_STATUS;
  int cw = drawTypeChip(82, y + 29, m.type);
  if (stab) {
    gfx->setTextColor(DEX_TBL[dex].accent);
    gfx->setTextSize(1);
    gfx->setCursor(82 + cw + 6, y + 33);
    gfx->print("STAB");
  }
  char pw[16];
  if (m.cat == MC_STATUS) snprintf(pw, sizeof(pw), "%s", T(S_MOVE_STATUS));
  else snprintf(pw, sizeof(pw), T(S_MOVE_PWR), m.power);
  gfx->setTextColor(stab ? DEX_TBL[dex].accent : UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(384 - (int)strlen(pw) * 6, y + 33);
  gfx->print(pw);
}

// card page 4: the four known moves. Tapping a slot opens the picker.
void renderCardMoves() {
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_MOVES)) * 9, 44);
  gfx->print(T(S_MOVES));
  for (int i = 0; i < MOVE_SLOTS; i++) drawMoveRow(MOVE_ROW_Y(i), pet.moves[i], false, pet.speciesId);
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(T(S_MOVE_TAP)) * 3, 340);
  gfx->print(T(S_MOVE_TAP));
}

// Every move the species can learn by this level, so a slot can be swapped for
// anything legal -- not just the handful a level-up would have offered. That
// includes what its earlier forms learn, each behind THEIR level: evolving late
// skips the prompts for the levels already passed, and some later forms drop a
// move the earlier one had (Sirfetch'd and Air Slash). Either way it stays here.
uint8_t learnableFor(int16_t dex, uint8_t lvl, uint8_t *out, uint8_t max) {
  if (dex < 1 || dex > DEX_COUNT) return 0;
  int16_t line[1 + PRE_EVO_MAX];
  line[0] = dex;
  const uint8_t forms = 1 + preEvolutions(dex, line + 1, PRE_EVO_MAX);
  uint8_t w = 0;
  for (uint8_t f = 0; f < forms; f++) {
    const int16_t sp = line[f];
    const uint8_t n = learnCount(sp);
    for (uint8_t i = 0; i < n && w < max; i++) {
      // moveUnlockLevel(), NOT learnLevel(): a TM is stored as level 0 and would
      // otherwise clear this check at level 1. That is how a level 22 Charmeleon
      // came to be offered FIRE BLAST -- the same class of bug as the level 1
      // Squirtle with SURF, in the one path that fix did not reach.
      if (moveUnlockLevel(sp, i) > lvl) continue;
      uint8_t mv = learnMove(sp, i);
      if (!mv || mv >= MOVE_COUNT) continue;
      bool dup = false;
      for (uint8_t j = 0; j < w; j++)
        if (out[j] == mv) { dup = true; break; }
      if (!dup) out[w++] = mv;
    }
  }
  return w;
}

// The picker targets either the live pet or a banked member. A banked one keeps
// its frozen level, so it can only relearn what it could have known back then.
uint8_t learnableList(uint8_t *out, uint8_t max) {
  if (movePickParty) {
    const PartyMon &m = party.slots[movePickParty - 1];
    return learnableFor(m.dex, (uint8_t)m.level, out, max);
  }
  return pet.isEgg() ? 0 : learnableFor(pet.speciesId, pet.level(), out, max);
}

uint8_t *pickTargetMoves() {
  return movePickParty ? party.slots[movePickParty - 1].moves : pet.moves;
}
int16_t pickTargetDex() {
  return movePickParty ? party.slots[movePickParty - 1].dex : pet.speciesId;
}

void renderMovePick() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_MOVE_PICK)) * 6, 40);
  gfx->print(T(S_MOVE_PICK));

  uint8_t all[MOVE_COUNT];
  uint8_t n = learnableList(all, sizeof(all));
  uint8_t pages = n ? (n + MOVE_PICK_PER_PAGE - 1) / MOVE_PICK_PER_PAGE : 1;
  if (movePickPage >= pages) movePickPage = 0;
  for (uint8_t i = 0; i < MOVE_PICK_PER_PAGE; i++) {
    uint8_t idx = movePickPage * MOVE_PICK_PER_PAGE + i;
    if (idx >= n) break;
    // the move already in this slot is highlighted, so replacing like for like
    // is obvious rather than a guess
    drawMoveRow(MOVE_PICK_Y(i), all[idx], all[idx] == pickTargetMoves()[movePickSlot], pickTargetDex());
  }
  for (uint8_t i = 0; i < pages && pages > 1; i++) {
    if (i == movePickPage) gfx->fillCircle(CX - (pages - 1) * 13 + i * 26, 380, 5, UI_INK);
    else gfx->drawCircle(CX - (pages - 1) * 13 + i * 26, 380, 4, UI_INK);
  }
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 402);
  gfx->print(T(S_BACK));
  gfx->flush();
}

void renderEvoPick() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(T(S_EVO_PICK)) * 6, 48);
  gfx->print(T(S_EVO_PICK));

  int16_t options[EVO_BRANCH_MAX];
  uint8_t count = pet.evoOptions(pet.speciesId, options);
  uint8_t pages = count ? (count + EVO_PICK_PER_PAGE - 1) / EVO_PICK_PER_PAGE : 1;
  if (evoPickPage >= pages) evoPickPage = 0;
  for (uint8_t i = 0; i < EVO_PICK_PER_PAGE; i++) {
    uint8_t index = evoPickPage * EVO_PICK_PER_PAGE + i;
    if (index >= count) break;
    const DexEntry &entry = DEX_TBL[options[index]];
    int x, y, w, h;
    evoPickRowRect(i, &x, &y, &w, &h);
    gfx->fillRoundRect(x, y, w, h, 18, entry.accent);
    gfx->drawRoundRect(x, y, w, h, 18, UI_INK);
    const uint16_t color = entry.accent;
    const int luminance = ((color >> 11) & 31) * 24 +
                          ((color >> 5) & 63) * 24 + (color & 31) * 8;
    const uint16_t text = luminance > 1400 ? UI_INK : UI_WHITE;
    gfx->setTextColor(text);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(entry.name) * 6, y + (h - 16) / 2);
    gfx->print(entry.name);
    if (!pet.isRegistered(options[index])) gfx->fillCircle(x + 22, y + h / 2, 6, text);
  }
  for (uint8_t i = 0; i < pages && pages > 1; i++) {
    const int x = CX - (pages - 1) * 13 + i * 26;
    if (i == evoPickPage) gfx->fillCircle(x, 382, 5, UI_INK);
    else gfx->drawCircle(x, 382, 4, UI_INK);
  }
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(T(S_BACK)) * 6, 406);
  gfx->print(T(S_BACK));
  uiChrome();
  gfx->flush();
}

bool evoPickRowRect(uint8_t row, int *x, int *y, int *w, int *h) {
  if (row >= EVO_PICK_PER_PAGE) return false;
  const int rowY = EVO_PICK_Y(row);
  const int half = uiSafeHalfWidthFor(rowY, rowY + EVO_PICK_ROW_H) - 6;
  if (x) *x = CX - half;
  if (y) *y = rowY;
  if (w) *w = half * 2;
  if (h) *h = EVO_PICK_ROW_H;
  return true;
}

void evoPickTap(int16_t x, int16_t y) {
  int16_t options[EVO_BRANCH_MAX];
  uint8_t count = pet.evoOptions(pet.speciesId, options);
  for (uint8_t i = 0; i < EVO_PICK_PER_PAGE; i++) {
    uint8_t index = evoPickPage * EVO_PICK_PER_PAGE + i;
    if (index >= count) break;
    int rowX, rowY, rowW, rowH;
    evoPickRowRect(i, &rowX, &rowY, &rowW, &rowH);
    if (x < rowX || x > rowX + rowW || y < rowY || y > rowY + rowH) continue;
    const int16_t oldSpecies = pet.speciesId;
    if (pet.evolveTo(options[index])) {
      evoPickOpen = false;
      evoPmd.load(oldSpecies, pet.shiny);
      sfxPlay(SFX_TAP);
    } else {
      sfxPlay(SFX_DENY);
    }
    return;
  }
  evoPickOpen = false;
  sfxPlay(SFX_TAP);
}

// ---------- battle ----------

// Draws a battle backdrop scaled 2x. Emitted as runs of identical indices
// rather than a write per pixel: this is flat pixel art with long horizontal
// runs, and 240x112 at 2x would otherwise be 26,880 fillRect calls a frame.
// The round bezel crops the overhang physically, so nothing is clipped here.
static void drawBack(const BackScene &b, int y0) {
  const int SC = 2;
  int x0 = CX - (b.w * SC) / 2;
  for (int r = 0; r < b.h; r++) {
    const uint8_t *row = b.idx + (uint32_t)r * b.w;
    int c = 0;
    while (c < b.w) {
      uint8_t v = row[c];
      int run = 1;
      while (c + run < b.w && row[c + run] == v) run++;
      uint16_t col = b.pal[v];
      gfx->fillRect(x0 + c * SC, y0 + r * SC, run * SC, SC, col);
      c += run;
    }
  }
}

// Which scene: the FOE's biome, since a battle happens where it lives, and the
// same day/night split the main screen already uses.
static void drawBattleBack() {
  int16_t dex = btlFoe.dex;
  if (dex < 1 || dex > DEX_COUNT) { gfx->fillCircle(CX, CY, 231, UI_BG_DAY); return; }
  uint8_t bi = speciesBiome(dex);
  if (bi >= BACK_BIOMES) bi = (bi == BIOME_GRAVEYARD) ? BIOME_FOREST : BIOME_MEADOW;
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  drawBack(BACKS[bi][night ? 1 : 0], 30);
}

// Streams a side's sprite if it is not already the one loaded. Called whenever
// a creature steps in, never per frame.
static void btlSyncSprite(uint8_t who, const Combatant &c) {
  int16_t key = c.dex * (c.shiny ? -1 : 1);
  if (btlPmdDex[who] == key && btlPmd[who].loaded) return;
  btlPmd[who].unload();
  btlPmdDex[who] = 0;
  if (c.dex < 1 || c.dex > DEX_COUNT) return;
  if (btlPmd[who].load(c.dex, c.shiny)) btlPmdDex[who] = key;   // NOT (uint8_t): Hoenn runs past 255
}

static void btlFreeSprites() {
  for (int i = 0; i < 2; i++) { btlPmd[i].unload(); btlPmdDex[i] = 0; }
}

static void btlSay(const char *fmt, ...) {
  if (btlMsgCount >= 6) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(btlMsg[btlMsgCount], sizeof(btlMsg[0]), fmt, ap);
  va_end(ap);
  btlMsgCount++;
}

// Turns a TurnLog into narration. Everything here was already decided by the
// engine -- nothing is recomputed, so the text can never disagree with the maths.
// Picks the cue for an action from the TurnLog, so the sound can never
// disagree with what actually happened.
static void btlSfxFor(const TurnLog &lg) {
  if (lg.targetFainted) { sfxPlay(SFX_FAINT); return; }
  if (lg.inflicted) { sfxPlay(SFX_STATUS); return; }
  if (lg.damage && lg.effPct > 100) { sfxPlay(SFX_SUPER); return; }
  if (lg.damage) {
    sfxPlay(lg.move && MOVE_TBL[lg.move].cat == MC_SPEC ? SFX_BEAM : SFX_HIT);
    return;
  }
  if (lg.move && MOVE_TBL[lg.move].cat == MC_STATUS && !lg.missed) sfxPlay(SFX_STATUS);
}

static void btlNarrate(const Combatant &actor, const Combatant &target, const TurnLog &lg) {
  if (lg.skipped) return;
  btlSfxFor(lg);
  if (lg.hurtSelf) { btlSay(T(S_BTL_HURTSELF)); return; }
  if (lg.charged) { btlSay(T(S_BTL_USED), actor.name, moveName(lg.move)); return; }
  if (lg.move) btlSay(T(S_BTL_USED), actor.name, moveName(lg.move));
  if (lg.missed) { btlSay(T(S_BTL_MISS), actor.name); return; }
  if (lg.immune) { btlSay(T(S_BTL_IMMUNE)); return; }
  if (lg.crit) btlSay(T(S_BTL_CRIT));
  if (lg.damage && lg.effPct > 100) btlSay(T(S_BTL_SUPER));
  else if (lg.damage && lg.effPct < 100) btlSay(T(S_BTL_WEAK));
  if (lg.inflicted) {
    static const StrId AIL_STR[] = { S_AIL_PARA, S_AIL_PARA, S_AIL_BURN, S_AIL_POISON,
                                     S_AIL_SLEEP, S_AIL_FREEZE, S_AIL_CONFUSE };
    if (lg.inflicted < 7) btlSay(T(S_BTL_STATUS), target.name, T(AIL_STR[lg.inflicted]));
  }
  if (lg.targetFainted) btlSay(T(S_BTL_FAINT), target.name);
}

// Builds one opponent through Pet, so it gets the same stat formula and the
// same learnset-driven moveset the player's creatures do.
static void foeFromSpecies(Combatant &c, int16_t dex, uint8_t lvl, uint8_t iv) {
  Pet foe;
  foe.dbgHatchAs(dex, false);
  foe.ivAtk = foe.ivDef = foe.ivSpe = foe.ivHp = iv;
  foe.ageMinutes = (uint32_t)(lvl ? lvl - 1 : 0) * MINUTES_PER_LEVEL;
  foe.relearnFromLevel();
  combatantFromPet(c, foe);
}

// Your side: the live pet first, then the banked party.
//
// Both ladders cap your LEVEL to the leader's best, so a gym is always fought
// on its own terms and grinding is never the answer -- the type chart, the
// movesets and the choices are. Hard additionally caps your team SIZE to the
// leader's, so Brock is two-on-two. The caps are applied while BUILDING the
// combatants, so nothing is ever written back to the stored creature, exactly
// like ailments.
static void buildSquad(uint8_t maxLvl, uint8_t maxCount, uint16_t mask) {
  btlSquadN = 0;
  btlSquadAt = 0;
  btlPetIn = false;
  if (maxCount > TRAINER_TEAM_MAX) maxCount = TRAINER_TEAM_MAX;
  if (!pet.isEgg() && btlSquadN < maxCount && (mask & 1)) {
    combatantFromPet(btlSquad[btlSquadN++], pet, maxLvl);
    btlPetIn = true;      // the training reward goes to whoever fought for it
  }
  for (int i = 0; i < PARTY_SLOTS && btlSquadN < maxCount; i++) {
    if (party.slots[i].empty() || !(mask & (1 << (i + 1)))) continue;
    PartyMon m = party.slots[i];
    if (maxLvl && m.level > maxLvl) m.level = maxLvl;
    combatantFromParty(btlSquad[btlSquadN++], m);
  }
  if (btlSquadN) btlYou = btlSquad[0];
}

// How many you may bring: the leader's own count in hard mode, six otherwise.
// PICK_TRAINERS, not TRAINERS -- this answers for the fight being set up, so it
// must read the region the picker was opened for.
uint8_t squadCap(uint8_t idx, bool hard) {
  if (idx >= TRAINER_COUNT) return TRAINER_TEAM_MAX;   // PICK_LAN: uncapped six
  return hard ? PICK_TRAINERS[idx].count : TRAINER_TEAM_MAX;
}

// A fight against another device. The squads are already exchanged; the host
// owns resolution and the guest renders what it is sent.
void startLinkBattle() {
  if (!lan.mineN || !lan.theirsN) return;
  // Rebuilt from lan.mine, NOT from squadMask. What we fight with has to be
  // exactly what the peer was told we have -- rebuilding from the party would
  // silently diverge if anything changed between offering and starting.
  btlSquadN = 0;
  btlSquadAt = 0;
  for (uint8_t i = 0; i < lan.mineN && i < TRAINER_TEAM_MAX; i++)
    linkMonTo(btlSquad[btlSquadN++], lan.mine[i]);
  if (!btlSquadN) return;
  btlYou = btlSquad[0];
  btlLink = true;
  btlLinkHost = lan.isHost;
  btlWild = false;
  btlTrainer = -1;
  btlHard = false;
  btlFoeAt = 0;
  btlFoeSquadN = 0;
  for (uint8_t i = 0; i < lan.theirsN && i < TRAINER_TEAM_MAX; i++)
    linkMonTo(btlFoeSquad[btlFoeSquadN++], lan.theirs[i]);
  btlFoe = btlFoeSquad[0];
  btlMyAct = 0;
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlMenu = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  battleOpen = true;
}

void startTrainerBattle(uint8_t idx, bool hard) {
  if (idx >= TRAINER_COUNT || pet.isEgg() || pet.ceremony != CER_NONE) return;
  uint8_t region = gymRegion % GYM_REGIONS;
  const Trainer &tr = TRAINER_SETS[region].list[idx];
  uint8_t top = 0;
  for (int k = 0; k < tr.count; k++)
    if (tr.team[k].level > top) top = tr.team[k].level;
  // BOTH ladders cap your level to the leader's best. Without it a L73 team
  // walks every trainer at 100% and the type chart never matters. Hard adds the
  // size cap on top, plus a smarter AI and better opposing IVs.
  buildSquad(top, hard ? tr.count : TRAINER_TEAM_MAX, squadMask);
  if (!btlSquadN) return;
  btlTrainer = (int8_t)idx;
  btlRegion = region;
  btlHard = hard;
  btlWild = false;
  btlFoeAt = 0;
  foeFromSpecies(btlFoe, tr.team[0].dex, tr.team[0].level, hard ? HARD_IV : EASY_IV);
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlMenu = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  audioCry(btlFoe.dex);
  btlLungeUntil[0] = btlLungeUntil[1] = 0;
  btlHitUntil[0] = btlHitUntil[1] = 0;
  battleOpen = true;
}

void startBattle(int16_t dex, uint8_t lvl) {
  if (pet.isEgg() || pet.ceremony != CER_NONE) return;
  if (dex < 1 || dex > DEX_COUNT) return;
  buildSquad(0, TRAINER_TEAM_MAX, 0xFFFF);
  if (!btlSquadN) return;
  // The opponent is built through Pet so it gets the same stat formula and the
  // same learnset-driven moveset the player's creature does -- no special-cased
  // "enemy" maths that could quietly diverge.
  Pet foe;
  foe.dbgHatchAs(dex, false);
  foe.ivAtk = foe.ivDef = foe.ivSpe = foe.ivHp = 20;
  foe.ageMinutes = (uint32_t)(lvl ? lvl - 1 : 0) * MINUTES_PER_LEVEL;
  foe.relearnFromLevel();
  combatantFromPet(btlFoe, foe);
  btlWild = false;
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlMenu = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  audioCry(btlFoe.dex);
  btlLungeUntil[0] = btlLungeUntil[1] = 0;
  btlHitUntil[0] = btlHitUntil[1] = 0;
  battleOpen = true;
}

// The guest's whole turn: copy in what the host resolved and play the same
// animations the host is playing. It runs no battle logic at all -- that is the
// entire point of one side being authoritative (see link.h).
static void btlApplyResult() {
  if (lan.resultN < sizeof(LinkResult)) { lan.resultNew = false; return; }
  LinkResult r;
  memcpy(&r, lan.result, sizeof(r));
  lan.resultNew = false;

  // The wire says "host"/"guest"; here we are always the guest, so their fields
  // are the foe's and ours are ours.
  uint32_t now = millis();
  if (r.guestIdx < btlSquadN && r.guestIdx != btlSquadAt) {
    btlSquad[btlSquadAt] = btlYou;
    btlSquadAt = r.guestIdx;
    btlYou = btlSquad[btlSquadAt];
    btlSyncSprite(0, btlYou);
    btlLungeUntil[0] = btlHitUntil[0] = btlFaintUntil[0] = 0;
    btlEnterUntil[0] = now + BTL_ENTER_MS;
  }
  if (r.hostIdx != btlFoeAt && r.hostIdx < lan.theirsN) {
    btlFoeAt = r.hostIdx;
    linkMonTo(btlFoe, lan.theirs[btlFoeAt]);
    btlHpShown[1] = btlFoe.maxHp;
    btlSyncSprite(1, btlFoe);
    btlLungeUntil[1] = btlHitUntil[1] = btlFaintUntil[1] = 0;
    btlEnterUntil[1] = now + BTL_ENTER_MS;
  }
  btlYou.hp = r.guestHp > btlYou.maxHp ? btlYou.maxHp : r.guestHp;
  btlFoe.hp = r.hostHp > btlFoe.maxHp ? btlFoe.maxHp : r.hostHp;
  btlYou.ailment = r.guestAil;
  btlFoe.ailment = r.hostAil;

  btlMsgCount = 0;
  if (r.hostMove) btlSay(T(S_BTL_USED), btlFoe.name, moveName(r.hostMove));
  if (r.guestMove) btlSay(T(S_BTL_USED), btlYou.name, moveName(r.guestMove));
  if (r.guestDmg) { btlHitUntil[0] = now + BTL_HIT_MS; sfxPlay(SFX_HIT); }
  if (r.hostDmg) { btlHitUntil[1] = now + BTL_HIT_MS; sfxPlay(SFX_HIT); }
  if (btlYou.fainted()) {
    btlFaintUntil[0] = now + BTL_FAINT_MS;
    btlSay(T(S_BTL_FAINT), btlYou.name);
  }
  if (btlFoe.fainted()) {
    btlFaintUntil[1] = now + BTL_FAINT_MS;
    btlSay(T(S_BTL_FAINT), btlFoe.name);
  }
}

// Radio packets land on another task, so the guest picks them up here, once a
// frame, rather than rendering from inside an interrupt.
static void btlLinkPoll() {
  if (!btlLink) return;

  // A peer that stopped answering. Ending the fight is the only honest thing to
  // do -- there is no result coming, and pretending otherwise is the hang this
  // whole layer exists to remove.
  if (!lan.live() && !btlOver) {
    btlOver = true;
    btlWon = false;
    audioMusic(MUS_NONE);
    btlMsgCount = 0;
    btlSay("%s", T(S_LAN_GONE));
    return;
  }

  if (btlLinkHost) {
    // Our own action was latched when it was tapped; theirs arrives whenever
    // the radio manages it. Whichever is second sets the turn going.
    if (btlMyAct && lan.hasPeerAct() && !btlOver && !btlMsgCount &&
        btlSwapWho < 0) {
      uint8_t act = btlMyAct;
      btlMyAct = 0;
      if (LINK_ACT_IS_SWITCH(act)) btlSwitchTo(LINK_ACT_SLOT(act));
      else btlResolve(btlYou.moves[LINK_ACT_SLOT(act) % MOVE_SLOTS]);
    }
    return;
  }

  if (lan.resultNew) btlApplyResult();
  if (lan.state == LINK_DONE && !btlOver) {
    btlOver = true;
    btlWon = lan.youWon;
    audioMusic(btlWon ? MUS_VICTORY : MUS_NONE);
    if (btlWon) sfxPlay(SFX_VICTORY);
    btlSay("%s", btlWon ? T(S_BTL_WIN) : T(S_BTL_LOSE));
  }
}

// Packs the outcome for the guest. Only the host ever calls this.
static void btlShipResult(uint8_t yourMove, uint8_t theirMove,
                          uint16_t hp0You, uint16_t hp0Foe) {
  LinkResult r = {};
  r.hostHp = btlYou.hp;   r.guestHp = btlFoe.hp;
  r.hostAil = btlYou.ailment; r.guestAil = btlFoe.ailment;
  r.hostMove = yourMove;  r.guestMove = theirMove;
  r.hostDmg = (hp0You > btlYou.hp) ? hp0You - btlYou.hp : 0;
  r.guestDmg = (hp0Foe > btlFoe.hp) ? hp0Foe - btlFoe.hp : 0;
  r.hostIdx = btlSquadAt; r.guestIdx = btlFoeAt;
  if (btlYou.fainted() || btlFoe.fainted()) r.flags |= 0x04;
  lan.sendResult((const uint8_t *)&r, (uint8_t)sizeof(r));
}

// ---------- wild battle actions ----------

// What beating a wild creature is worth. Each draw EXCLUDES every earlier one,
// so a settlement can never list the same item twice -- which would read as a
// bug rather than as luck, the same reasoning that stops rewardTraining()
// picking a stat with no headroom.
static void wildGrantDrops() {
  wildDropN = 0;
  uint8_t n = wildDropCount(btlHard, (uint8_t)random(100));
  for (uint8_t i = 0; i < n && i < 4; i++) {
    uint32_t total = bag.weightTotal(wildDrops, wildDropN);
    if (!total) break;
    ItemKey k = bag.weightedDrop((uint32_t)random(total), wildDrops, wildDropN);
    if (!k) break;
    bag.add(k);
    wildDrops[wildDropN++] = k;
    btlSay(T(S_FOUND_ITEM), itemEntry(k).name);
  }
}

// Where a caught creature goes. It reuses the SAME slot-finding path a farewell
// uses -- party, then box, then ask -- rather than growing a second copy of it.
// CLAUDE.md's most repeated bug is a rule enforced in one path but not its
// twin, and "which slot does a creature take" is exactly that shape.
static bool wildStoreCaught() {
  PartyMon m = wildToPartyMon();
  // NOT registered as raised here: switchTo() does that when the creature
  // actually becomes active. Once durable storage succeeds, noteCaught() records
  // it in the CAUGHT Pokedex (and CAUGHT SHINY if shiny).
  if (!party.writable()) return false;
  bool stored = false;
  if (party.firstFree() >= 0) stored = party.add(m);
  else if (party.boxFirstFree() >= 0) stored = party.boxAdd(m);
  else {
    // Both full: hand it to the chooser the endings already own.
    if (pet.endedKind != CER_NONE) return false;
    const uint32_t gen = pet.petGeneration();
    pet.setEnded(m, CER_RELEASE);
    if (pet.petGeneration() == gen) {
      pet.endedMon = PartyMon();
      pet.endedKind = CER_NONE;
      return false;
    }
    stored = true;
  }
  if (!stored) return false;
  pet.noteCaught(m.dex, m.shiny != 0);
  pet.saveNow();
  return true;
}

// Throwing a ball. Spends the turn either way: a free capture attempt every
// round would make weakening the target pointless.
static void btlThrowBall(ItemKey k) {
  if (!party.writable() || pet.endedKind != CER_NONE) {
    sfxPlay(SFX_DENY);
    return;
  }
  if (!bag.consume(k)) { sfxPlay(SFX_DENY); return; }
  btlMenu = 0;
  btlSay(T(S_THREW), itemEntry(k).name);
  uint8_t chance = wildCaptureChance(DEX_TBL[btlFoe.dex].rarity, btlFoe.hp,
                                     btlFoe.maxHp, btlFoe.ailment != AIL_NONE,
                                     itemEntry(k).param);
  if (random(100) < chance) {
    if (!wildStoreCaught()) {
      Serial.println("save: capture storage failed; encounter retained");
      sfxPlay(SFX_DENY);
      btlSay("%s", T(S_ITEM_NOUSE));
      return;
    }
    wildCaught = true;
    btlOver = true;
    btlWon = true;
    audioMusic(MUS_VICTORY);
    sfxPlay(SFX_VICTORY);
    btlSay(T(S_CAUGHT), btlFoe.name);
    wildGrantDrops();
    return;
  }
  sfxPlay(SFX_DENY);
  btlSay("%s", T(S_BROKE_FREE));
  btlResolve(0);   // move 0 = no attack, so only the foe acts
}

// A potion or a full heal, on whichever creature is out. It works on the
// Combatant, so like every ailment it is battle-only and is never written back.
static void btlUseItem(ItemKey k) {
  const ItemEntry &e = itemEntry(k);
  bool useful = (e.category == IC_HEAL) ? btlYou.hp < btlYou.maxHp
              : (e.category == IC_CURE) ? btlYou.ailment != AIL_NONE
              : false;
  if (!useful) { sfxPlay(SFX_DENY); btlSay("%s", T(S_ITEM_NOUSE)); btlMenu = 0; return; }
  if (!bag.consume(k)) { sfxPlay(SFX_DENY); return; }
  if (e.category == IC_HEAL) {
    uint32_t hp = (uint32_t)btlYou.hp + (uint16_t)e.param;
    btlYou.hp = hp > btlYou.maxHp ? btlYou.maxHp : (uint16_t)hp;
  } else {
    btlYou.ailment = AIL_NONE;
    btlYou.ailTurns = 0;
    btlYou.confuseTurns = 0;
  }
  btlMenu = 0;
  sfxPlay(SFX_TAP);
  btlSay(T(S_ITEM_USED), e.name);
  btlResolve(0);   // using an item costs the turn, as it does in the real games
}

// One exchange: both sides act in speed order, then burn/poison chip.
static void btlResolve(uint8_t yourMove) {
  TurnLog lg;
  // Against another device the opponent's move comes off the wire, never from
  // the AI -- and the host is the only side that runs this at all.
  uint8_t foeMove;
  bool foeSwitched = false;
  uint32_t now = millis();
  if (btlLink) {
    // Off the wire, never from the AI. A switch is carried in the same message
    // as a move, and like our own switch it costs the turn: they change, we act.
    uint8_t act = lan.pendingAct;
    if (LINK_ACT_IS_SWITCH(act)) {
      uint8_t to = LINK_ACT_SLOT(act);
      if (to < btlFoeSquadN && to != btlFoeAt && !btlFoeSquad[to].fainted()) {
        btlFoeSquad[btlFoeAt] = btlFoe;     // remember how battered it was
        btlFoeAt = to;
        btlFoe = btlFoeSquad[to];
        btlHpShown[1] = btlFoe.hp;
        btlSyncSprite(1, btlFoe);
        btlLungeUntil[1] = btlHitUntil[1] = btlFaintUntil[1] = 0;
        btlEnterUntil[1] = now + BTL_ENTER_MS;
        btlSay(T(S_BTL_SENDS), lan.peerName, btlFoe.name);
        foeSwitched = true;
      }
      foeMove = 0;
    } else {
      foeMove = btlFoe.moves[LINK_ACT_SLOT(act) % MOVE_SLOTS];
    }
    lan.pendingAct = 0;
  } else {
    foeMove = aiChooseMove(btlFoe, btlYou, btlHard);
    // A wild creature can leave. It spends its ACTION doing so rather than
    // fleeing and attacking in the same turn, so the fight simply ends -- with
    // no drops, which is what makes weakening it worth doing before it goes.
    if (btlWild && random(100) < wildFoeEscapeChance(btlFoe.hp, btlFoe.maxHp)) {
      btlOver = true;
      btlWon = false;
      audioMusic(MUS_NONE);
      btlSay(T(S_WILD_FLED), btlFoe.name);
      return;
    }
  }
  (void)foeSwitched;

  bool youFirst = battleMovesFirst(btlYou, yourMove, btlFoe, foeMove);
  Combatant *a = youFirst ? &btlYou : &btlFoe;
  Combatant *b = youFirst ? &btlFoe : &btlYou;
  uint8_t ma = youFirst ? yourMove : foeMove;
  uint8_t mb = youFirst ? foeMove : yourMove;

  uint16_t hp0You = btlYou.hp, hp0Foe = btlFoe.hp;
  battleAct(*a, *b, ma, lg);
  btlNarrate(*a, *b, lg);
  if (lg.damage && !lg.hurtSelf) btlLungeUntil[a == &btlYou ? 0 : 1] = now + BTL_LUNGE_MS;
  if (!b->fainted()) {
    battleAct(*b, *a, mb, lg);
    btlNarrate(*b, *a, lg);
    if (lg.damage && !lg.hurtSelf)
      btlLungeUntil[b == &btlYou ? 0 : 1] = now + BTL_LUNGE_MS + BTL_LUNGE_MS;
  }
  // whoever actually lost health flinches, whichever side dealt it
  if (btlYou.hp < hp0You) btlHitUntil[0] = now + BTL_HIT_MS;
  if (btlFoe.hp < hp0Foe) btlHitUntil[1] = now + BTL_HIT_MS;
  if (btlLink && btlLinkHost) btlShipResult(yourMove, foeMove, hp0You, hp0Foe);
  if (!btlYou.fainted() && !btlFoe.fainted()) {
    battleEndTurn(btlYou, lg);
    if (lg.damage) btlNarrate(btlYou, btlYou, lg);
    battleEndTurn(btlFoe, lg);
    if (lg.damage) btlNarrate(btlFoe, btlFoe, lg);
  }
  // Someone went down. The replacement is NOT swapped in here -- that made the
  // change instant and read as a jump cut. Flag it, let the sprite drop out of
  // frame, and swap when the player dismisses the message.
  if (btlFoe.fainted() && btlLink && btlFoeAt + 1 < btlFoeSquadN) {
    btlFaintUntil[1] = millis() + BTL_FAINT_MS;
    btlSwapWho = 1;
    return;
  }
  if (btlFoe.fainted() && btlTrainer >= 0 && btlFoeAt + 1 < BTL_TRAINERS[btlTrainer].count) {
    btlFaintUntil[1] = millis() + BTL_FAINT_MS;
    btlSwapWho = 1;
    return;
  }
  if (btlYou.fainted() && btlSquadAt + 1 < btlSquadN) {
    btlFaintUntil[0] = millis() + BTL_FAINT_MS;
    btlSwapWho = 0;
    return;
  }
  if (btlFoe.fainted() || btlYou.fainted()) {
    btlOver = true;
    btlWon = btlFoe.fainted();
    btlNewBadge = false;
    btlTrainGain = 0;
    if (btlWon && btlTrainer >= 0 && !pet.hasBadge(btlRegion, btlTrainer, btlHard)) {
      pet.winBadge(btlRegion, btlTrainer, btlHard);
      btlNewBadge = true;
    }
    // A badge and nothing else made the ladder a one-way checklist. A win now
    // trains the creature that fought for it -- so a leader you can already
    // beat is worth returning to. It goes to the LIVE pet only: banked members
    // are frozen at the level and training they were banked with, and battling
    // already costs the live pet energy, which is what rate-limits the grind
    // without needing a cooldown.
    if (btlWon && btlTrainer >= 0 && btlPetIn) {
      // Later leaders are worth more, and hard mode is worth roughly double.
      uint8_t amt = (btlHard ? 6 + random(5) : 3 + random(3)) + btlTrainer / 3;
      btlTrainGain = pet.rewardTraining(amt, btlTrainWhich);
    }
    audioMusic(btlWon ? MUS_VICTORY : MUS_NONE);
    if (btlWon) sfxPlay(SFX_VICTORY);
    // Tell the peer before anything else: if we stop here without sending, the
    // other device sits on a battle that will never take another turn.
    if (btlLink && btlLinkHost) lan.sendEnd(btlWon);
    if (btlLink) { btlSay("%s", btlWon ? T(S_BTL_WIN) : T(S_BTL_LOSE)); return; }
    // A wild win reports its drops as narration rather than on the win screen:
    // that screen is the badge ceremony, and it has no badge to show.
    if (btlWon && btlWild) { btlSay("%s", T(S_BTL_WIN)); wildGrantDrops(); return; }
    if (btlWon && btlTrainer >= 0) { btlWinUntil = millis() + 60000; return; }
    btlSay("%s", T(S_BTL_LOSE));
  }
}

static void btlHpBar(int x, int y, int w, const Combatant &c, uint16_t shown) {
  int fw = c.maxHp ? (w - 4) * shown / c.maxHp : 0;
  uint16_t col = (shown * 2 > c.maxHp) ? UI_BAR_OK
                 : (shown * 4 > c.maxHp) ? UI_BAR_WARN : UI_BAR_BAD;
  gfx->fillRoundRect(x, y, w, 14, 4, UI_TRACK);
  if (fw > 0) gfx->fillRoundRect(x + 2, y + 2, fw, 10, 3, col);
  gfx->drawRoundRect(x, y, w, 14, 4, UI_INK);
}

static void btlSide(int tx, int ty, int sx, int sy, const Combatant &c, uint8_t who) {
  // the scenes are busy, so the name and bar sit on their own plate rather
  // than fighting the artwork for contrast
  int ph = (who == 0) ? 54 : 40;
  gfx->fillRoundRect(tx - 8, ty - 8, 158, ph, 8, UI_BG_DAY);
  gfx->drawRoundRect(tx - 8, ty - 8, 158, ph, 8, UI_INK);
  char l[28];
  snprintf(l, sizeof(l), "%s Lv.%u", c.name, c.level);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(tx, ty);
  gfx->print(l);
  gfx->setTextColor(UI_BAR_WARN);
  gfx->setCursor(tx, ty + 14);
  gfx->print("HP");
  btlHpBar(tx + 18, ty + 12, 122, c, btlHpShown[who]);
  if (who == 0) {                 // your own numbers, as the games do
    char hp[16];
    snprintf(hp, sizeof(hp), "%u/%u", btlHpShown[who], c.maxHp);
    gfx->setTextColor(UI_INK);
    gfx->setCursor(tx + 140 - (int)strlen(hp) * 6, ty + 28);
    gfx->print(hp);
  }
  if (c.ailment != AIL_NONE) {   // a status is the thing you most need to see
    static const StrId AIL_STR[] = { S_AIL_PARA, S_AIL_PARA, S_AIL_BURN, S_AIL_POISON,
                                     S_AIL_SLEEP, S_AIL_FREEZE, S_AIL_CONFUSE };
    gfx->setTextColor(UI_BAR_BAD);
    gfx->setCursor(tx + 18, ty + 28);
    gfx->print(T(AIL_STR[c.ailment]));
  }
  // a platform under each creature, so they stand in the scene rather than
  // floating over it
  uint32_t now = millis();
  int ox = 0, oy = 0;
  bool flash = false;
  if (now < btlFaintUntil[who]) {          // sinks out of frame as it faints
    uint32_t left = btlFaintUntil[who] - now;
    oy += (int)((BTL_FAINT_MS - left) * 70 / BTL_FAINT_MS);
  } else if (c.fainted() && btlSwapWho == (int8_t)who) {
    return;                                // gone, waiting to be replaced
  }
  if (now < btlEnterUntil[who]) {          // and the next one rises into place
    uint32_t left = btlEnterUntil[who] - now;
    oy += (int)(left * 70 / BTL_ENTER_MS);
  }
  if (now < btlLungeUntil[who]) {          // lean in, then back out
    uint32_t left = btlLungeUntil[who] - now;
    int amt = (int)(left > BTL_LUNGE_MS / 2 ? BTL_LUNGE_MS - left : left) * 22 / (BTL_LUNGE_MS / 2);
    ox = who == 0 ? amt : -amt;            // you lunge right, the foe lunges left
    oy = who == 0 ? -amt / 2 : amt / 2;
  }
  if (now < btlHitUntil[who]) {
    uint32_t left = btlHitUntil[who] - now;
    ox += ((left / 50) % 2) ? 5 : -5;      // jitter
  }

  // Real PMD playback when the sprite streamed: attack while lunging, hurt
  // while flinching, idle otherwise. `has()` guards every one, because not
  // every species ships every action -- falling through to idle, and to the
  // flat thumbnail if the sprite is missing entirely (no SD).
  if (btlPmd[who].loaded) {
    uint8_t act = PMD_IDLE;
    bool loop = true;
    uint32_t t = now;
    if (now < btlHitUntil[who] && btlPmd[who].has(PMD_HURT)) {
      act = PMD_HURT; loop = false; t = now - (btlHitUntil[who] - BTL_HIT_MS);
    } else if (now < btlLungeUntil[who] && btlPmd[who].has(PMD_ATTACK)) {
      act = PMD_ATTACK; loop = false; t = now - (btlLungeUntil[who] - BTL_LUNGE_MS);
    }
    drawPmdActM(btlPmd[who], act, sx + 24 + ox, sy + 78 + oy, t, loop, false, 4);
    return;
  }
  const uint8_t *th = thumbs.get(c.dex);
  if (!th) return;
  if (now < btlHitUntil[who]) flash = ((btlHitUntil[who] - now) / 60) % 2 == 0;
  drawThumb(th, sx + ox, sy + oy, 3, flash);
}

// Bars drain rather than snap: a hit that removes half your health should be
// visible as it happens, not as a value that was already different.
static void btlEaseBars() {
  const uint16_t real[2] = { btlYou.hp, btlFoe.hp };
  for (int i = 0; i < 2; i++) {
    int diff = (int)real[i] - (int)btlHpShown[i];
    if (!diff) continue;
    int step = diff / 5;
    if (!step) step = diff > 0 ? 1 : -1;
    btlHpShown[i] = (uint16_t)((int)btlHpShown[i] + step);
  }
}

// The moment the ladder builds toward. It used to be one more line in the same
// message box as "It's super effective!", with the badge awarded silently.
void renderWin() {
  const Trainer &t = BTL_TRAINERS[btlTrainer];
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);

  gfx->setTextColor(UI_BAR_WARN);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_BTL_WIN)) * 9, 54);
  gfx->print(T(S_BTL_WIN));

  char l[40];
  snprintf(l, sizeof(l), T(S_BTL_BEAT), t.name);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(l) * 6, 96);
  gfx->print(l);

  // the badge, large, with the hard-mode halo if that is how it was won
  if (btlTrainer < TRAINER_GYMS) {
    int by = 190;
    if (btlHard) {
      for (int r = 62; r >= 56; r--) gfx->drawCircle(CX, by, r, r % 2 ? 0xFEA0 : 0xFF60);
    }
    const BadgeArt *a = badgeArtFor(btlRegion, btlTrainer);
    if (a) {
      for (int r = 0; r < BADGE_PX; r++)
        for (int c = 0; c < BADGE_PX; c++) {
          uint8_t v = a->idx[r * BADGE_PX + c];
          if (v == 0xFF) continue;
          // 3x, so it reads as a prize rather than a list entry
          gfx->fillRect(CX - BADGE_PX * 3 / 2 + c * 3, by - BADGE_PX * 3 / 2 + r * 3,
                        3, 3, a->pal[v]);
        }
    } else {
      // This region has no badge art. Say so with a plain medal in the leader's
      // type colour rather than borrowing another region's badge.
      const Trainer &tr = TRAINER_SETS[btlRegion % GYM_REGIONS].list[btlTrainer];
      gfx->fillCircle(CX, by, 26, typeColor(tr.type));
      gfx->drawCircle(CX, by, 26, UI_INK);
      char n[4];
      snprintf(n, sizeof(n), "%u", (unsigned)(btlTrainer + 1));
      gfx->setTextSize(3);
      gfx->setTextColor(typeColorIsLight(tr.type) ? UI_INK : UI_WHITE);
      gfx->setCursor(CX - (int)strlen(n) * 9, by - 11);
      gfx->print(n);
    }
    if (btlNewBadge) {
      gfx->setTextColor(UI_BAR_OK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - strlen(T(S_BTL_NEWBADGE)) * 6, 286);
      gfx->print(T(S_BTL_NEWBADGE));
    }
  }
  snprintf(l, sizeof(l), T(S_BADGES_FMT), pet.badgeCountIn(btlRegion, btlHard));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(l) * 6, 316);
  gfx->print(l);

  // what the win was worth beyond the badge
  if (btlTrainGain) {
    static const StrId NAMES[3] = { S_TR_ATK, S_TR_DEF, S_TR_SPE };
    snprintf(l, sizeof(l), T(S_WIN_TRAIN_FMT),
             T(NAMES[btlTrainWhich % 3]), btlTrainGain);
    gfx->setTextColor(UI_BAR_OK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(l) * 6, 344);
    gfx->print(l);
  } else if (btlPetIn && btlTrainer >= 0) {
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)strlen(T(S_WIN_MAXED)) * 3, 348);
    gfx->print(T(S_WIN_MAXED));
  }

  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 380);
  gfx->print(T(S_BACK));
  gfx->flush();
}

void renderBattle() {
  if (btlWinUntil) { renderWin(); return; }
  btlEaseBars();
  gfx->fillScreen(RGB565_BLACK);
  drawBattleBack();
  // the lower band stays flat so the move grid and the HP text keep their
  // contrast against it
  gfx->fillRect(0, 254, 466, 212, UI_BG_DAY);

  // x=82 not 58: at y=60 the round bezel starts around x=77, and a longer
  // name like BLASTOISE was losing its first characters off the edge
  btlSide(82, 82, 300, 40, btlFoe, 1);    // foe reads top-left, sprite top-right
  btlSide(250, 190, 76, 168, btlYou, 0);  // you read bottom-right, sprite bottom-left

  // Waiting on the other device. Without this the screen is identical to the
  // one where it is your turn, so a tap that has been sent and a tap that was
  // never registered look exactly the same.
  bool lanWait = btlLink && !btlOver &&
                 (btlLinkHost ? (btlMyAct && !lan.hasPeerAct())
                              : (lan.state == LINK_WAITING));
  if (lanWait && !btlMsgCount) {
    gfx->fillRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_GRID_H, 12, UI_WHITE);
    gfx->drawRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_GRID_H, 12, UI_INK);
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(2);
    const char *w = T(S_LAN_WAITFOE);
    gfx->setCursor(CX - (int)strlen(w) * 6, BTL_GRID_Y + 48);
    gfx->print(w);
  } else if (btlMsgCount) {            // narration takes over the menu area
    gfx->fillRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_GRID_H, 12, UI_WHITE);
    gfx->drawRoundRect(BTL_GRID_X, BTL_GRID_Y, 328, BTL_GRID_H, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    char lines[3][25] = {{0}};
    const char *p = btlMsg[0];
    uint8_t lineCount = 0;
    while (*p && lineCount < 3) {
      while (*p == ' ') p++;
      int take = (int)strlen(p);
      if (take > 24) take = 24;
      if (p[take] && p[take] != ' ') {
        int word = take;
        while (word > 0 && p[word] != ' ') word--;
        if (word > 0) take = word;
      }
      memcpy(lines[lineCount], p, take);
      lines[lineCount][take] = 0;
      p += take;
      lineCount++;
    }
    int firstY = BTL_GRID_Y + 18 + (3 - lineCount) * 10;
    for (uint8_t i = 0; i < lineCount; i++) {
      gfx->setCursor(CX - (int)strlen(lines[i]) * 6, firstY + i * 24);
      gfx->print(lines[i]);
    }
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(1);
    gfx->setCursor(CX - 18, BTL_GRID_Y + BTL_GRID_H - 14);
    gfx->print("tap...");
  } else if (btlMenu == 0) {
    // Four options on the 2x2 grid: FIGHT and BAG on top, POKEMON and RUN
    // below. FIGHT used to be a full-width row, which left nowhere for the bag;
    // all four now reuse the move grid's cells and so inherit its padded hit
    // areas -- the same padding that fixed POKEMON being hard to press.
    const char *opt[4] = { T(S_FIGHT), T(S_BAG), T(S_BTL_SWITCH), T(S_BTL_RUN) };
    for (int i = 0; i < 4; i++) {
      int x = BTL_CELL_X(i), y = BTL_CELL_Y(i);
      // The bag is dead in a link fight: an item the peer was never told about
      // would desync the two devices, and the host is authoritative.
      bool live = !(i == 1 && btlLink);
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, live ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, live ? UI_INK : 0x8410);
      gfx->setTextColor(live ? UI_INK : 0x8410);
      gfx->setTextSize(2);
      gfx->setCursor(x + (BTL_CELL_W - (int)strlen(opt[i]) * 12) / 2, y + 19);
      gfx->print(opt[i]);
    }
  } else if (btlMenu == 3) {
    drawBtlBack();
    // The battle bag: balls and medicine, never vitamins -- training is
    // permanent and belongs to the care sim.
    // Using up the last item of the last page leaves the page past the end.
    if (btlBagPage >= btlBagPages()) btlBagPage = btlBagPages() - 1;
    uint8_t shown = 0;
    uint8_t n = bag.distinctCount();
    for (uint8_t idx = 0; idx < n; idx++) {
      ItemKey k = bag.keyAt(idx);
      if (!itemUsableInBattle(k)) continue;
      if (shown < btlBagPage * 4) { shown++; continue; }
      int cell = shown - btlBagPage * 4;
      if (cell >= 4) break;
      int x = BTL_CELL_X(cell), y = BTL_CELL_Y(cell);
      bool live = !itemIsBall(k) || btlWild;   // a ball needs a wild target
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, live ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, live ? UI_INK : 0x8410);
      gfx->setTextColor(live ? UI_INK : 0x8410);
      gfx->setTextSize(strlen(itemEntry(k).name) <= 11 ? 2 : 1);
      gfx->setCursor(x + 10, y + 8);
      gfx->print(itemEntry(k).name);
      gfx->setTextSize(1);
      char cnt[8];
      snprintf(cnt, sizeof(cnt), "x%u", bag.count(k));
      gfx->setCursor(x + 10, y + 28);
      gfx->print(cnt);
      shown++;
    }
    if (!shown) {
      gfx->setTextColor(UI_INK_SOFT);
      gfx->setTextSize(1);
      gfx->setCursor(CX - (int)strlen(T(S_BAG_EMPTY)) * 3, BTL_GRID_Y + 40);
      gfx->print(T(S_BAG_EMPTY));
    }
  } else if (btlMenu == 2) {
    drawBtlBack();
    // who to bring on instead; the current one and anything fainted is inert
    for (uint8_t i = 0; i < btlSquadN && i < 4; i++) {
      int x = BTL_CELL_X(i), y = BTL_CELL_Y(i);
      const Combatant &m = (i == btlSquadAt) ? btlYou : btlSquad[i];
      bool usable = (i != btlSquadAt) && !m.fainted();
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, usable ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, usable ? UI_INK : 0x8410);
      gfx->setTextColor(usable ? UI_INK : 0x8410);
      gfx->setTextSize(strlen(m.name) <= 11 ? 2 : 1);
      gfx->setCursor(x + 10, y + 8);
      gfx->print(m.name);
      gfx->setTextSize(1);
      char hp[20];
      snprintf(hp, sizeof(hp), "%u/%u", m.hp, m.maxHp);
      gfx->setCursor(x + 10, y + 28);
      gfx->print(hp);
    }
  } else {
    drawBtlBack();
    for (int i = 0; i < MOVE_SLOTS; i++) {
      int x = BTL_CELL_X(i), y = BTL_CELL_Y(i);
      uint8_t mv = btlYou.moves[i];
      gfx->fillRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, mv ? UI_BG_DAY : UI_TRACK);
      gfx->drawRoundRect(x, y, BTL_CELL_W, BTL_CELL_H, 10, UI_INK);
      if (!mv) continue;
      const char *name = moveName(mv);
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(strlen(name) <= 11 ? 2 : 1);
      gfx->setCursor(x + 10, y + 7);
      gfx->print(name);
      // Same chip as the move list: in a fight the type IS the decision, and
      // grey 6px text was the least visible thing on the busiest screen.
      int cw = drawTypeChip(x + 10, y + 34, MOVE_TBL[mv].type);
      if (hasStab(btlYou.dex, MOVE_TBL[mv].type) &&
          MOVE_TBL[mv].cat != MC_STATUS) {
        gfx->setTextSize(1);
        gfx->setTextColor(DEX_TBL[btlYou.dex].accent);
        gfx->setCursor(x + 10 + cw + 4, y + 38);
        gfx->print("+");
      }
    }
  }
  if (btlMenu == 3) uiDrawRimBar();   // the bag pages: say so, as every list does
  gfx->flush();
}

// Brings on the flagged replacement and starts its entrance.
static void btlDoSwap() {
  uint32_t now = millis();
  if (btlSwapWho == 1 && btlLink) {
    btlFoeSquad[btlFoeAt] = btlFoe;
    // the next one still standing, not simply the next index
    uint8_t nxt = btlFoeAt;
    while (++nxt < btlFoeSquadN && btlFoeSquad[nxt].fainted()) {}
    if (nxt >= btlFoeSquadN) { btlSwapWho = -1; return; }
    btlFoeAt = nxt;
    btlFoe = btlFoeSquad[btlFoeAt];
    btlHpShown[1] = btlFoe.hp;
    btlSyncSprite(1, btlFoe);
    btlLungeUntil[1] = btlHitUntil[1] = btlFaintUntil[1] = 0;
    btlEnterUntil[1] = now + BTL_ENTER_MS;
    btlSay(T(S_BTL_SENDS), lan.peerName, btlFoe.name);
  } else if (btlSwapWho == 1) {
    const Trainer &t = BTL_TRAINERS[btlTrainer];
    btlFoeAt++;
    foeFromSpecies(btlFoe, t.team[btlFoeAt].dex, t.team[btlFoeAt].level,
                   btlHard ? HARD_IV : EASY_IV);
    btlHpShown[1] = btlFoe.maxHp;
    btlSyncSprite(1, btlFoe);
    btlLungeUntil[1] = btlHitUntil[1] = btlFaintUntil[1] = 0;
    btlEnterUntil[1] = now + BTL_ENTER_MS;
    btlSay(T(S_BTL_SENDS), t.name, btlFoe.name);
  } else if (btlSwapWho == 0) {
    btlSquad[btlSquadAt] = btlYou;     // remember how battered it was
    btlSquadAt++;
    btlYou = btlSquad[btlSquadAt];
    btlHpShown[0] = btlYou.hp;
    btlSyncSprite(0, btlYou);
    btlLungeUntil[0] = btlHitUntil[0] = btlFaintUntil[0] = 0;
    btlEnterUntil[0] = now + BTL_ENTER_MS;
    btlSay(T(S_BTL_GO), btlYou.name);
  }
  btlSwapWho = -1;
}

// Switching spends your turn: the opponent still acts. That is what stops it
// being a free look at the matchup every round.
static void btlSwitchTo(uint8_t i) {
  if (i >= btlSquadN || i == btlSquadAt) return;
  btlSquad[btlSquadAt] = btlYou;
  btlSquadAt = i;
  btlYou = btlSquad[i];
  btlHpShown[0] = btlYou.hp;
  btlSyncSprite(0, btlYou);
  btlLungeUntil[0] = btlHitUntil[0] = btlFaintUntil[0] = 0;
  btlEnterUntil[0] = millis() + BTL_ENTER_MS;
  btlMenu = 0;
  btlSay(T(S_BTL_GO), btlYou.name);
  audioCry(btlYou.dex);      // it announces itself as it steps in
  btlResolve(0);          // move 0 = no attack, so only the foe acts
}

int btlCellIndexAt(int16_t x, int16_t y) {
  for (int i = 0; i < 4; i++)
    if (btlCellHit(i, x, y)) return i;
  return -1;
}

// The way out of the move and switch screens. Without it the only exits were
// choosing something or leaving the fight entirely.
static void drawBtlBack() {
  gfx->fillRoundRect(BTL_BACK_X, BTL_BACK_Y, BTL_BACK_W, BTL_BACK_H, 11, UI_TRACK);
  gfx->drawRoundRect(BTL_BACK_X, BTL_BACK_Y, BTL_BACK_W, BTL_BACK_H, 11, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(T(S_BACK)) * 6, BTL_BACK_Y + 14);
  gfx->print(T(S_BACK));
}

static bool btlBackTap(int16_t x, int16_t y) {
  if (x < BTL_BACK_X || x > BTL_BACK_X + BTL_BACK_W ||
      y < BTL_BACK_Y || y > BTL_BACK_Y + BTL_BACK_H) return false;
  btlMenu = 0;
  sfxPlay(SFX_TAP);
  return true;
}

// Running. A gym leader keeps their badge and the fight simply ends; against
// another device the peer is told, so it does not sit waiting for a move that
// will never come.
//
// A WILD fight is the exception: getting away is a roll, and failing it costs
// the turn. That is what stops a ball being risk-free -- you cannot throw one,
// see it fail and simply leave.
static void btlRun() {
  if (btlWild && !btlOver) {
    btlMenu = 0;
    if (random(100) >= wildEscapeChance(btlYou.level, btlFoe.level)) {
      sfxPlay(SFX_DENY);
      btlSay("%s", T(S_NO_ESCAPE));
      btlResolve(0);   // move 0 = no attack, so only the foe acts
      return;
    }
    // Narrated and left OVER rather than closed here, so the message is
    // actually seen: battleTap() dismisses a finished fight, and closing on the
    // same beat would flash the line for one frame.
    sfxPlay(SFX_TAP);
    btlOver = true;
    btlWon = false;
    audioMusic(MUS_NONE);
    btlSay("%s", T(S_GOT_AWAY));
    return;
  }
  sfxPlay(SFX_DENY);
  btlFreeSprites();
  audioMusic(MUS_NONE);
  if (btlLink) { lanLeave(); btlLink = false; lanOpen = true; }
  battleOpen = false;
  btlWild = false;
  btlMenu = 0;
}

void battleTap(int16_t x, int16_t y) {
  if (btlWinUntil) {          // dismiss the win screen and leave the fight
    btlWinUntil = 0;
    btlFreeSprites();
    audioMusic(MUS_NONE);
    battleOpen = false;
    btlWild = false;
    if (btlLink) { btlLink = false; lanOpen = true; }
    return;
  }
  if (btlMsgCount) {          // one readable event per tap; never skip queued text
    for (uint8_t i = 1; i < btlMsgCount; i++)
      memcpy(btlMsg[i - 1], btlMsg[i], sizeof(btlMsg[0]));
    btlMsgCount--;
    if (btlMsgCount) return;
    if (btlOver) {
      btlFreeSprites();
      // Its twin above (the btlWinUntil dismiss) already stops the music; a
      // wild win/loss and a losing trainer fight end here instead and were the
      // one path that left MUS_BATTLE/MUS_VICTORY looping under the next screen.
      audioMusic(MUS_NONE);
      battleOpen = false;
      btlWild = false;
      // Back to the LAN screen rather than all the way out: that is where a
      // rematch is offered, and re-pairing for every fight would be tedious.
      if (btlLink) { btlLink = false; lanOpen = true; }
      return;
    }
    if (btlSwapWho >= 0) btlDoSwap();   // the replacement arrives on this beat
    return;
  }
  if (btlMenu == 0) {
    if (btlCellHit(0, x, y)) { sfxPlay(SFX_TAP); btlMenu = 1; return; }   // FIGHT
    if (btlCellHit(1, x, y)) {
      if (btlLink) { sfxPlay(SFX_DENY); return; }   // see renderBattle
      sfxPlay(SFX_TAP); btlMenu = 3; btlBagPage = 0; return;
    }
    if (btlCellHit(2, x, y)) { sfxPlay(SFX_TAP); btlMenu = 2; return; }
    if (btlCellHit(3, x, y)) { btlRun(); return; }
    return;
  }
  if (btlMenu == 3) {
    if (btlBackTap(x, y)) return;
    if (btlBagPage >= btlBagPages()) btlBagPage = btlBagPages() - 1;   // as the draw does
    uint8_t shown = 0;
    uint8_t n = bag.distinctCount();
    for (uint8_t idx = 0; idx < n; idx++) {
      ItemKey k = bag.keyAt(idx);
      if (!itemUsableInBattle(k)) continue;
      if (shown < btlBagPage * 4) { shown++; continue; }
      int cell = shown - btlBagPage * 4;
      if (cell >= 4) break;
      shown++;
      if (!btlCellHit(cell, x, y)) continue;
      // ONE predicate decides whether a ball may be thrown, and the draw above
      // asks the same one -- a greyed cell that still worked is the exact fault
      // uiButtonDisabled() exists to prevent on the home row.
      if (itemIsBall(k)) {
        if (!btlWild) { sfxPlay(SFX_DENY); return; }
        btlThrowBall(k);
      } else {
        btlUseItem(k);
      }
      return;
    }
    btlMenu = 0;      // anywhere else backs out
    return;
  }
  if (btlMenu == 2) {
    if (btlBackTap(x, y)) return;
    for (uint8_t i = 0; i < btlSquadN && i < 4; i++) {
      if (!btlCellHit(i, x, y)) continue;
      const Combatant &m = (i == btlSquadAt) ? btlYou : btlSquad[i];
      if (i == btlSquadAt || m.fainted()) { sfxPlay(SFX_DENY); return; }
      sfxPlay(SFX_TAP);
      if (btlLink && !btlLinkHost) {
        // The guest asks; it never switches on its own. A switch rides the same
        // message as a move, so the host spends the turn on it exactly as it
        // would for us.
        lan.sendAct(LINK_ACT_SWITCH_TO(i));
        btlMenu = 0;
        return;
      }
      if (btlLink) {            // host: latched like a move, see btlLinkPoll
        btlMyAct = LINK_ACT_SWITCH_TO(i);
        btlMenu = 0;
        if (!lan.hasPeerAct()) return;
        btlMyAct = 0;
      }
      btlSwitchTo(i);
      return;
    }
    btlMenu = 0;      // anywhere else backs out
    return;
  }
  if (btlBackTap(x, y)) return;
  for (int i = 0; i < MOVE_SLOTS; i++) {
    if (!btlYou.moves[i]) continue;
    if (!btlCellHit(i, x, y)) continue;
    sfxPlay(SFX_TAP);
    btlMenu = 0;
    if (btlLink && !btlLinkHost) {
      lan.sendAct(LINK_ACT_MOVE(i));   // the guest asks; the host decides
      return;
    }
    if (btlLink) {
      // Latched, not discarded. The host used to throw the tap away when the
      // rival had not chosen yet, so you had to keep jabbing at the move until
      // the timing happened to line up.
      btlMyAct = LINK_ACT_MOVE(i);
      if (!lan.hasPeerAct()) return;   // resolved by btlLinkPoll when it lands
      btlMyAct = 0;
    }
    btlResolve(btlYou.moves[i]);
    return;
  }
  btlMenu = 0;        // a tap off the grid goes back to FIGHT/POKEMON
}

// ---------- player card (swipe down) ----------
// Everything here is player-wide and outlives the creature: badges, the daily
// streak, the Pokedex and the party. No player sprite yet -- the SD carries
// PMD creature sprites only, so a trainer portrait needs new art.
// Page 1: who you are -- avatar, badges, totals. Tap the avatar to cycle it;
// the four sprites are hand-drawn (see species.h) because SpriteCollab has no
// trainer art and ripped sprites would be unlicensed.
static void renderPlayerBadges() {
  // the player's name if they have set one, the generic title if not; either
  // way tapping it opens the keyboard
  const char *tn = pet.trainerName[0] ? pet.trainerName : T(S_TRAINER);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)strlen(tn) * 9, 40);
  gfx->print(tn);

  // Pages 1 and 2 are the other regions' ladders: name them, and drop the
  // avatar so the badges have the room. Only page 0 is "you".
  if (playerBadgeRegion != 0) {
    const char *rn = TRAINER_SETS[playerBadgeRegion].region;
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(rn) * 6, 120);
    gfx->print(rn);
  } else if (gShowAllAvatars) {          // emulator only: every avatar at once
    for (uint8_t i = 0; i < AVATAR_COUNT; i++)
      drawAvatar(i, 60 + (i % 4) * 88, 60 + (i / 4) * 60, 3);
  } else {
    drawAvatar(pet.avatar, CX - AVATAR_PX * 2, 72, 4);
    // the sprite alone is not always obvious at 16x16, so it is named
    const char *an = AVATARS[pet.avatar % AVATAR_COUNT].name;
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)strlen(an) * 3, 143);
    gfx->print(an);
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setCursor(CX - (int)strlen(T(S_AVATAR_HINT)) * 3, 154);
    gfx->print(T(S_AVATAR_HINT));
  }

  // The real badges, 2x4. Unearned ones draw as a faint outline so the shape
  // of what is missing is still visible.
  for (int i = 0; i < TRAINER_GYMS; i++) {
    int bx = 140 + (i % 4) * 62, by = 188 + (i / 4) * 62;
    bool got = pet.hasBadge(playerBadgeRegion, i, false);
    bool hard = pet.hasBadge(playerBadgeRegion, i, true);
    if (hard) {
      // Beaten on hard: a golden halo. Concentric rings, not a filled disc --
      // a disc sat behind the art and read as a gold coin rather than a glow.
      gfx->drawCircle(bx, by, 25, 0xFDE0);
      gfx->drawCircle(bx, by, 24, 0xFEA0);
      gfx->drawCircle(bx, by, 23, 0xFF60);
      gfx->drawCircle(bx, by, 22, 0xFEA0);
      gfx->drawCircle(bx, by, 21, 0xFDE0);
    }
    if (!got) {
      gfx->drawCircle(bx, by, 20, UI_TRACK);
      continue;
    }
    const BadgeArt *a = badgeArtFor(playerBadgeRegion, i);
    if (!a) {                 // no art for this region: a filled disc, won is won
      gfx->fillCircle(bx, by, 14, typeColor(TRAINER_SETS[playerBadgeRegion % GYM_REGIONS].list[i].type));
      gfx->drawCircle(bx, by, 14, UI_INK);
      continue;
    }
    for (int r = 0; r < BADGE_PX; r++)
      for (int c = 0; c < BADGE_PX; c++) {
        uint8_t v = a->idx[r * BADGE_PX + c];
        if (v == 0xFF) continue;
        gfx->fillRect(bx - BADGE_PX / 2 + c, by - BADGE_PX / 2 + r, 1, 1, a->pal[v]);
      }
  }
  char l[32];
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  snprintf(l, sizeof(l), T(S_STREAK_FMT), pet.streak, pet.bestStreak);
  gfx->setCursor(CX - (int)strlen(l) * 6, 286);
  gfx->print(l);
  snprintf(l, sizeof(l), T(S_POKEDEX_FMT),
           pet.dexCountIn(DEXV_COMPLETE, false, 1, DEX_COUNT), DEX_COUNT);
  gfx->setCursor(CX - (int)strlen(l) * 6, 312);
  gfx->print(l);
  snprintf(l, sizeof(l), T(S_PARTY_FMT), party.count());
  gfx->setCursor(CX - (int)strlen(l) * 6, 338);
  gfx->print(l);
}

// The medals of the creature in front of you: the creature screen's second page.
static void renderPlayerMedals() {
  int got = 0;
  for (int i = 0; i < MED_COUNT; i++)
    if (pet.hasMedal(1 << i)) got++;
  char head[24];
  snprintf(head, sizeof(head), T(S_MEDALS_FMT), got, MED_COUNT);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(head) * 9, 44);
  gfx->print(head);

  for (int i = 0; i < MED_COUNT; i++) {
    int x = 46 + (i % 2) * 190, y = 96 + (i / 2) * 58;
    bool g = pet.hasMedal(1 << i);
    gfx->fillRoundRect(x, y, 180, 48, 10, g ? UI_BAR_OK : UI_TRACK);
    gfx->drawRoundRect(x, y, 180, 48, 10, UI_INK);
    gfx->setTextColor(g ? UI_BG_DAY : UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(x + (180 - (int)strlen(medalLabel(i)) * 12) / 2, y + 6);
    gfx->print(medalLabel(i));
    gfx->setTextSize(1);
    gfx->setCursor(x + (180 - (int)strlen(medalDesc(i)) * 6) / 2, y + 30);
    gfx->print(medalDesc(i));
  }
  char tot[28];
  snprintf(tot, sizeof(tot), T(S_MEDALS_TOTAL_FMT), pet.totalMedals);
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(tot) * 3, 344);
  gfx->print(tot);
}

// The trait's name. Indexed by the PERS_* value, in the same order as the enum,
// so there is no switch that could disagree with it.
const char *personalityName() {
  static const StrId NAMES[Pet::PERSONALITY_COUNT] = {
    S_PERS_BOLD, S_PERS_STURDY, S_PERS_BRISK, S_PERS_HARDY, S_PERS_EAGER, S_PERS_CALM,
  };
  return T(NAMES[pet.personality() % Pet::PERSONALITY_COUNT]);
}

// ---------------------------------------------------------------------------
// TODAY'S CARE CHECKLIST.
//
// NON-DESTRUCTIVE, and that is the whole design rather than a caveat. Every line
// is COMPUTED from state the save already holds, so:
//   * nothing new is written, no field is added, no migration exists to get wrong;
//   * the list resets itself with the day, because lastCareDay and the bars
//     already do;
//   * and there is deliberately NO REWARD. A reward would have to be marked as
//     claimed, which means a per-day persisted flag, which is the migration this
//     avoids -- and the fork this idea came from pays for its version by
//     CONSUMING A LIVING CREATURE, which is not a trade worth making. The value
//     here is answering "what does it need right now", and a checklist does that
//     without being a currency.
//
// Every check reads existing state rather than a separate "did you do X" counter.
struct GoalDef {
  StrId label;
  bool (*met)();
};
static bool goalCared()  { return pet.caredToday(); }
static bool goalFed()    { return pet.fullness >= 70; }
static bool goalClean()  { return pet.poops == 0 && pet.hygiene >= 70; }
static bool goalHappy()  { return pet.joy >= 70; }
static bool goalRested() { return pet.energy >= 70; }
static const GoalDef GOALS[] = {
  { S_GOAL_CARED,  goalCared  },
  { S_GOAL_FED,    goalFed    },
  { S_GOAL_CLEAN,  goalClean  },
  { S_GOAL_HAPPY,  goalHappy  },
  { S_GOAL_RESTED, goalRested },
};
#define GOAL_COUNT ((int)(sizeof(GOALS) / sizeof(GOALS[0])))
// Its metrics live up with the other layout constants; uiRowHeights() holds
// GOAL_ROW_H to the hard floor far above here.

// Published so a test can drive the firmware's own answers rather than
// re-implementing five predicates and proving only the transcription.
int uiGoalCount() { return GOAL_COUNT; }
bool uiGoalMet(int i) { return i >= 0 && i < GOAL_COUNT && GOALS[i].met(); }
int uiGoalsMet() {
  int n = 0;
  for (int i = 0; i < GOAL_COUNT; i++) if (GOALS[i].met()) n++;
  return n;
}

static void renderPlayerGoals() {
  const int done = uiGoalsMet();
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_GOALS)) * 9, 44);
  gfx->print(T(S_GOALS));
  char sub[24];
  snprintf(sub, sizeof(sub), T(S_GOALS_FMT), done, GOAL_COUNT);
  gfx->setTextColor(done == GOAL_COUNT ? UI_BAR_OK : UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(sub) * 6, 76);
  gfx->print(sub);

  for (int i = 0; i < GOAL_COUNT; i++) {
    const int ry = GOAL_ROW_Y(i);
    // As wide as the glass allows AT THIS HEIGHT, the same rule every other row
    // list here follows -- one width for all five would hang the last one's
    // corners off the bezel.
    const int half = uiSafeHalfWidthFor(ry, ry + GOAL_ROW_H) - 18;
    const int rx = CX - half;
    const bool met = GOALS[i].met();
    gfx->fillRoundRect(rx, ry, half * 2, GOAL_ROW_H, 10, met ? UI_BAR_OK : UI_TRACK);
    gfx->drawRoundRect(rx, ry, half * 2, GOAL_ROW_H, 10, UI_INK);
    // A tick or an empty box, so the state does not rest on colour alone -- the
    // filled green and the grey track are close in luminance and this screen has
    // to read for somebody who cannot tell them apart.
    const int bx = rx + 14, by = ry + GOAL_ROW_H / 2;
    if (met) {
      gfx->fillCircle(bx + 8, by, 9, UI_BG_DAY);
      gfx->drawLine(bx + 4, by, bx + 7, by + 4, UI_INK);
      gfx->drawLine(bx + 5, by, bx + 8, by + 4, UI_INK);
      gfx->drawLine(bx + 7, by + 4, bx + 13, by - 5, UI_INK);
      gfx->drawLine(bx + 8, by + 4, bx + 14, by - 5, UI_INK);
    } else {
      gfx->drawCircle(bx + 8, by, 9, UI_INK_SOFT);
    }
    const char *l = T(GOALS[i].label);
    gfx->setTextColor(met ? UI_BG_DAY : UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(bx + 30, by - 8);
    gfx->print(l);
  }
}

void renderPlayer() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  renderPlayerBadges();

  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 392);
  gfx->print(T(S_BACK));
  uiChrome();
  gfx->flush();
}

// The creature screen: today's checklist, and on the rim its medals.
void renderMedals() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  if (medalsPage == 0) renderPlayerGoals();
  else renderPlayerMedals();

  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 392);
  gfx->print(T(S_BACK));
  uiChrome();
  gfx->flush();
}

// ---------- reaction test (trains SPEED) ----------
// The third training verb. The bag is a masher and the ball is a juggler, so
// this one is a reaction: a target appears somewhere on the panel and you tap
// it before it expires. The window shrinks as you go, which is what makes it
// read as speed rather than as endurance.
#define SPD_MS 15000UL       // session length
#define SPD_LIFE0 1100       // first target's window, ms
#define SPD_LIFE_MIN 380
#define SPD_R 46             // target radius

void spdSpawn() {
  // Keep the whole target inside the bezel: pick an angle and a radius that
  // leave SPD_R of margin, rather than a square that clips at the corners.
  int ang = random(360);
  int rad = random(150);
  float a = ang * 3.14159f / 180.0f;
  spdX = CX + (int)(cosf(a) * rad);
  spdY = CY + (int)(sinf(a) * rad);
  spdBorn = millis();
}

void startSpeedGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  spdOpen = true;
  spdUntil = millis() + SPD_MS;
  spdOverUntil = 0;
  spdHits = 0;
  spdMisses = 0;
  spdGain = 0;
  spdNewHi = false;
  spdSpawn();
}

static uint16_t spdLife() {
  int life = SPD_LIFE0 - spdHits * 32;
  return life < SPD_LIFE_MIN ? SPD_LIFE_MIN : life;
}

void spdTap(int16_t x, int16_t y) {
  if (spdOverUntil) return;
  int dx = x - spdX, dy = y - spdY;
  if (dx * dx + dy * dy <= (SPD_R + 14) * (SPD_R + 14)) {
    spdHits++;
    sfxPlay(SFX_TAP);
    spdSpawn();
  }
}

void renderSpeed() {
  uint32_t now = millis();
  drawGameScene();
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (spdOverUntil) {
    if (now > spdOverUntil) { spdOpen = false; return; }
    char b[24];
    snprintf(b, sizeof(b), T(S_SCORE_FMT), spdHits);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(CX - strlen(b) * 12, 150);
    gfx->print(b);
    char g[20];
    snprintf(g, sizeof(g), T(S_SPD_GAIN_FMT), spdGain);
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setTextSize(3);
    gfx->setCursor(CX - strlen(g) * 9, 210);
    gfx->print(g);
    gfx->setTextSize(2);
    if (spdNewHi && spdHits > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(CX - strlen(T(S_NEW_RECORD)) * 6, 256);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char r[20];
      snprintf(r, sizeof(r), T(S_RECORD_FMT), pet.spdHi);
      gfx->setTextColor(ink);
      gfx->setCursor(CX - strlen(r) * 6, 256);
      gfx->print(r);
    }
    gfx->flush();
    return;
  }

  // session over?
  if (now >= spdUntil) {
    spdNewHi = (spdHits > pet.spdHi);
    spdGain = pet.trainSpeed(spdHits);
    sfxPlay(spdNewHi ? SFX_MEDAL : SFX_PLAY);
    spdOverUntil = now + 3500;
    gfx->flush();
    return;
  }
  // target expired?
  if (now - spdBorn > spdLife()) {
    spdMisses++;
    spdSpawn();
  }

  // the target, shrinking as its window runs out so the urgency is visible
  uint32_t age = now - spdBorn;
  int life = spdLife();
  int r = SPD_R - (int)((uint32_t)SPD_R * age / (life ? life : 1) / 2);
  if (r < 8) r = 8;
  gfx->fillCircle(spdX, spdY, r, UI_BAR_BAD);
  gfx->fillCircle(spdX, spdY, r * 2 / 3, UI_WHITE);
  gfx->fillCircle(spdX, spdY, r / 3, UI_BAR_BAD);

  char b[12];
  snprintf(b, sizeof(b), "%u", spdHits);
  gfx->setTextColor(ink);
  gfx->setTextSize(4);
  gfx->setCursor(CX - strlen(b) * 12, 30);
  gfx->print(b);
  // seconds left
  uint32_t left = (spdUntil > now) ? (spdUntil - now + 999) / 1000 : 0;
  snprintf(b, sizeof(b), "%us", (unsigned)left);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(b) * 6, 76);
  gfx->print(b);
  gfx->flush();
}

// ---------- type quiz (joy and bond, no stat) ----------
//
// The fourth thing to do with a creature, and deliberately NOT a fourth stat
// trainer: the bag trains ATK, the reaction test SPE and the ball DEF. This one
// pays in joy and bond. It replaces the colour-sequence memory game; its
// score has its own checkpointed record because the scores are not comparable.
//
// One defending type is shown; three attacking types are offered and exactly
// one of them is super effective against it. 20 seconds for the whole game, or
// three mistakes. The score is the number of right answers.

bool memoOver = false;
uint32_t memoOverAt = 0;
uint8_t memoBest = 0;        // right answers this session (the score)
bool memoNewHi = false;
uint8_t memoGain = 0;
uint8_t quizDef = 0;                 // the defending type asked about
uint8_t quizOpt[QUIZ_OPTS];          // the three attacking types offered
uint8_t quizAns = 0;                 // which of them is the super effective one
uint8_t quizLives = QUIZ_LIVES;
uint32_t quizStartedAt = 0;
bool quizFlashing = false;
uint32_t quizFlashAt = 0;
int8_t quizPicked = -1;              // what was tapped, while that shows

// ONE answer for where an option is: the draw path and the tap path both ask.
void quizOptRect(int i, int *x, int *y, int *w, int *h) {
  if (x) *x = CX - QUIZ_OPT_W / 2;
  if (y) *y = QUIZ_OPT_Y(i);
  if (w) *w = QUIZ_OPT_W;
  if (h) *h = QUIZ_OPT_H;
}
int quizOptAt(int16_t x, int16_t y) {
  for (int i = 0; i < QUIZ_OPTS; i++) {
    int bx, by, bw, bh;
    quizOptRect(i, &bx, &by, &bw, &bh);
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) return i;
  }
  return -1;
}

// A question: a defending type (never the same twice in a row), one attacker
// that is super effective against it and two that are not, in random places.
// Every one of the 18 types has at least one weakness, so there is always one.
void quizNextQuestion() {
  uint8_t d;
  do { d = (uint8_t)random(TYPE_COUNT); } while (d == quizDef);   // 0xFF on a fresh game
  quizDef = d;
  uint8_t good[TYPE_COUNT], bad[TYPE_COUNT], ng = 0, nb = 0;
  for (uint8_t a = 0; a < TYPE_COUNT; a++) {
    if (TYPE_FX[a][d] > 10) good[ng++] = a;
    else bad[nb++] = a;
  }
  quizAns = (uint8_t)random(QUIZ_OPTS);
  const uint8_t right = good[random(ng)];
  const uint8_t b1 = bad[random(nb)];
  uint8_t b2;
  do { b2 = bad[random(nb)]; } while (b2 == b1);
  uint8_t k = 0;
  for (uint8_t i = 0; i < QUIZ_OPTS; i++)
    quizOpt[i] = (i == quizAns) ? right : (k++ == 0 ? b1 : b2);
  quizPicked = -1;
}

void startMemoGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  memoOpen = true;
  memoOver = false;
  memoBest = 0;
  memoNewHi = false;
  memoGain = 0;
  quizLives = QUIZ_LIVES;
  quizFlashing = false;
  quizDef = 0xFF;
  quizStartedAt = millis();
  quizNextQuestion();
}

// Ends the session and banks what was earned. Its own function because three
// paths reach it -- the clock, the third mistake, and walking away -- and the
// reward must be applied exactly once however it ended.
static void memoFinish() {
  if (memoOver) return;
  memoNewHi = (memoBest > pet.quizHi);
  memoGain = pet.playQuiz(memoBest);
  sfxPlay(memoNewHi && memoBest > 0 ? SFX_MEDAL : SFX_LEVEL);
  memoOver = true;
  memoOverAt = millis();
}

// Leaving early BANKS what was earned rather than voiding it, exactly as the
// other three do.
void leaveMemo() {
  memoFinish();
  memoOpen = false;
}

void memoTap(int16_t x, int16_t y) {
  if (memoOver) return;
  const uint32_t now = millis();
  if (now - quizStartedAt >= QUIZ_MS) {
    memoFinish();
    return;
  }
  if (quizFlashing) return;
  const int o = quizOptAt(x, y);
  if (o < 0) return;
  quizPicked = (int8_t)o;
  quizFlashing = true;
  quizFlashAt = now;
  if (o == quizAns) {
    memoBest++;
    sfxPlay(SFX_TAP);
  } else {
    sfxPlay(SFX_DENY);
    if (quizLives) quizLives--;
  }
}

void renderMemo() {
  const uint32_t now = millis();
  drawGameScene();
  const bool night = sceneHour() < 6 || sceneHour() >= 20;
  const uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (memoOver) {
    if (now - memoOverAt >= 3500) { memoOpen = false; gfx->flush(); return; }
    char b[24];
    snprintf(b, sizeof(b), T(S_MEMO_FMT), memoBest);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(CX - strlen(b) * 12, 150);
    gfx->print(b);
    char g[22];
    snprintf(g, sizeof(g), T(S_PLUS_JOY_FMT), memoGain);
    gfx->setTextColor(C565(0xd4, 0x52, 0x7e));
    gfx->setTextSize(3);
    gfx->setCursor(CX - strlen(g) * 9, 210);
    gfx->print(g);
    gfx->setTextSize(2);
    if (memoNewHi && memoBest > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(CX - strlen(T(S_NEW_RECORD)) * 6, 256);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char r[22];
      snprintf(r, sizeof(r), T(S_RECORD_FMT), pet.quizHi);
      gfx->setTextColor(ink);
      gfx->setCursor(CX - strlen(r) * 6, 256);
      gfx->print(r);
    }
    gfx->flush();
    return;
  }

  if (now - quizStartedAt >= QUIZ_MS) {
    sfxPlay(SFX_PLAY);
    memoFinish(); gfx->flush(); return;
  }
  // The third mistake ends the quiz after its answer has been shown.
  if (quizFlashing && now - quizFlashAt >= QUIZ_FLASH_MS) {
    quizFlashing = false;
    if (!quizLives) { memoFinish(); gfx->flush(); return; }
    quizNextQuestion();
  }

  // the question
  gfx->setTextColor(ink);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(T(S_MEMO_WATCH)) * 6, 62);
  gfx->print(T(S_MEMO_WATCH));
  {
    const char *nm = typeName(quizDef);
    const int w = (int)strlen(nm) * 18 + 36;
    gfx->fillRoundRect(CX - w / 2, 92, w, 52, 14, typeColor(quizDef));
    gfx->drawRoundRect(CX - w / 2, 92, w, 52, 14, ink);
    gfx->setTextColor(typeColorIsLight(quizDef) ? UI_INK : UI_WHITE);
    gfx->setTextSize(3);
    gfx->setCursor(CX - (int)strlen(nm) * 9, 106);
    gfx->print(nm);
  }

  // the three answers; after a tap the right one turns green, a wrong pick red
  for (int i = 0; i < QUIZ_OPTS; i++) {
    int bx, by, bw, bh;
    quizOptRect(i, &bx, &by, &bw, &bh);
    const uint8_t t = quizOpt[i];
    uint16_t edge = ink;
    if (quizFlashing) {
      if (i == quizAns) edge = UI_BAR_OK;
      else if (i == quizPicked) edge = UI_BAR_BAD;
    }
    gfx->fillRoundRect(bx, by, bw, bh, 12, typeColor(t));
    gfx->drawRoundRect(bx, by, bw, bh, 12, edge);
    if (edge != ink) {
      gfx->drawRoundRect(bx + 1, by + 1, bw - 2, bh - 2, 11, edge);
      gfx->drawRoundRect(bx + 2, by + 2, bw - 4, bh - 4, 10, edge);
      gfx->drawRoundRect(bx + 3, by + 3, bw - 6, bh - 6, 9, edge);
    }
    const char *nm = typeName(t);
    gfx->setTextColor(typeColorIsLight(t) ? UI_INK : UI_WHITE);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(nm) * 6, by + bh / 2 - 7);
    gfx->print(nm);
  }

  // the clock, the lives and the score
  const uint32_t left = QUIZ_MS - (now - quizStartedAt);
  const int tw = 200, tx = CX - tw / 2, ty = 366;
  gfx->fillRoundRect(tx, ty, tw, 10, 4, UI_TRACK);
  const int fw = (int)((uint32_t)(tw - 4) * left / QUIZ_MS);
  if (fw > 0) gfx->fillRoundRect(tx + 2, ty + 2, fw, 6, 3, left < 5000 ? UI_BAR_BAD : UI_BAR_OK);
  for (int i = 0; i < QUIZ_LIVES; i++) {
    const int lx = CX - 70 + i * 22, ly = 396;
    if (i < quizLives) gfx->fillCircle(lx, ly, 7, UI_BAR_BAD);
    else gfx->drawCircle(lx, ly, 7, ink);
  }
  char sc[8];
  snprintf(sc, sizeof(sc), "%u", memoBest);
  gfx->setTextColor(ink);
  gfx->setTextSize(3);
  gfx->setCursor(CX + 40, 386);
  gfx->print(sc);
  char rec[22];
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.quizHi);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(rec) * 3, 418);
  gfx->print(rec);
  gfx->flush();
}

// ---------- team select ----------
// Which creatures come to this fight. It exists because hard mode caps your
// team to the leader's size, so the difference between a sweep and a wipe is
// bringing the right type -- and the squad used to be simply whoever sat first
// in the party.

// candidate n: 0 = the live pet, 1..PARTY_SLOTS = banked members
bool pickExists(uint8_t n) {
  if (n == 0) return !pet.isEgg();
  return n <= PARTY_SLOTS && !party.slots[n - 1].empty();
}
uint8_t pickChosen() {
  uint8_t c = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++)
    if (pickExists(n) && (squadMask & (1 << n))) c++;
  return c;
}
uint8_t pickCandidates() {
  uint8_t c = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++)
    if (pickExists(n)) c++;
  return c;
}
// Trims the selection to the first `cap` candidates. The default used to be
// "everything", which with a live pet plus six banked is seven against a cap of
// six -- so the screen opened already invalid.
void pickDefault(uint8_t cap) {
  squadMask = 0;
  uint8_t taken = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS && taken < cap; n++)
    if (pickExists(n)) { squadMask |= (1 << n); taken++; }
}

static void drawPickCell(uint8_t n, int x, int y, uint8_t capLvl) {
  bool on = (squadMask & (1 << n)) != 0;
  int16_t dex; uint16_t lvl; const char *nm; bool shiny;
  if (n == 0) {
    dex = pet.speciesId; lvl = pet.level(); shiny = pet.shiny;
    nm = pet.nick[0] ? pet.nick : DEX_TBL[dex].name;
  } else {
    const PartyMon &m = party.slots[n - 1];
    dex = m.dex; lvl = m.level; shiny = m.shiny;
    nm = m.nick[0] ? m.nick : DEX_TBL[dex].name;
  }
  if (capLvl && lvl > capLvl) lvl = capLvl;   // show the level it will FIGHT at
  gfx->fillRoundRect(x, y, PICK_CELL_W, PICK_CELL_H, 10, on ? UI_BG_DAY : UI_TRACK);
  gfx->drawRoundRect(x, y, PICK_CELL_W, PICK_CELL_H, 10, on ? UI_INK : 0x8410);
  const uint8_t *th = thumbs.get(dex);
  if (th) drawThumb(th, x - 12, y - 6, 2, !on);
  gfx->setTextColor(on ? UI_INK : 0x8410);
  gfx->setTextSize(1);
  gfx->setCursor(x + 54, y + 14);
  gfx->print(nm);
  char l[16];
  snprintf(l, sizeof(l), "Lv.%u%s", (unsigned)lvl, shiny ? " *" : "");
  gfx->setCursor(x + 54, y + 30);
  gfx->print(l);
  // its typing is the whole reason you are on this screen
  const DexEntry &d = DEX_TBL[dex];
  gfx->setTextColor(on ? d.accent : 0x8410);
  gfx->setCursor(x + 54, y + 48);
  gfx->print(typeName(d.type1));
  if (d.type2 != T_NONE) {
    gfx->setCursor(x + 54, y + 60);
    gfx->print(typeName(d.type2));
  }
  if (on) {
    gfx->fillCircle(x + PICK_CELL_W - 16, y + 16, 9, UI_BAR_OK);
    gfx->setTextColor(UI_BG_DAY);
    gfx->setCursor(x + PICK_CELL_W - 19, y + 13);
    gfx->print("*");
  }
}

void renderPick() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  uint8_t cap = squadCap(pickTrainer, pickHard);
  uint8_t top = 0;          // the level cap shown on each cell; 0 = uncapped
  char head[40];
  if (pickTrainer == PICK_LAN) {
    snprintf(head, sizeof(head), "%s: %s", T(S_LAN),
             lanWantHost ? T(S_LAN_HOST) : T(S_LAN_JOIN));
  } else {
    const Trainer &t = PICK_TRAINERS[pickTrainer];
    for (int k = 0; k < t.count; k++)
      if (t.team[k].level > top) top = t.team[k].level;
    snprintf(head, sizeof(head), "%s  Lv.%u x%u", t.name, top, t.count);
  }
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(head) * 6, 44);
  gfx->print(head);
  char sub[28];
  snprintf(sub, sizeof(sub), T(S_PICK_FMT), pickChosen(), cap);
  gfx->setTextSize(1);
  gfx->setTextColor(pickChosen() > cap ? UI_BAR_BAD : UI_TRACK);
  gfx->setCursor(CX - (int)strlen(sub) * 3, 68);
  gfx->print(sub);

  uint8_t seen = 0, drawn = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++) {
    if (!pickExists(n)) continue;
    if (seen++ < pickPage * PICK_PER_PAGE) continue;
    if (drawn >= PICK_PER_PAGE) break;
    drawPickCell(n, PICK_X(drawn), PICK_Y(drawn), top);
    drawn++;
  }
  uint8_t pages = (pickCandidates() + PICK_PER_PAGE - 1) / PICK_PER_PAGE;
  if (!pages) pages = 1;
  for (uint8_t i = 0; i < pages && pages > 1; i++) {
    int dx = CX - (pages - 1) * 13 + i * 26;
    if (i == pickPage) gfx->fillCircle(dx, 332, 5, UI_INK);
    else gfx->drawCircle(dx, 332, 4, UI_INK);
  }

  bool ok = pickChosen() > 0 && pickChosen() <= cap;
  gfx->fillRoundRect(PICK_BACK_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H, 12, UI_TRACK);
  gfx->drawRoundRect(PICK_BACK_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(PICK_BACK_X + (PICK_BTN_W - (int)strlen(T(S_BACK)) * 12) / 2,
                 PICK_GO_Y + 14);
  gfx->print(T(S_BACK));
  gfx->fillRoundRect(PICK_GO_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H, 12,
                     ok ? UI_BAR_OK : UI_TRACK);
  gfx->drawRoundRect(PICK_GO_X, PICK_GO_Y, PICK_BTN_W, PICK_BTN_H, 12, UI_INK);
  gfx->setTextColor(ok ? UI_BG_DAY : 0x8410);
  gfx->setTextSize(2);
  gfx->setCursor(PICK_GO_X + (PICK_BTN_W - (int)strlen(T(S_FIGHT)) * 12) / 2,
                 PICK_GO_Y + 14);
  gfx->print(T(S_FIGHT));
  gfx->flush();
}

void pickTap(int16_t x, int16_t y) {
  if (y >= PICK_GO_Y && y <= PICK_GO_Y + PICK_BTN_H &&
      x >= PICK_BACK_X && x <= PICK_BACK_X + PICK_BTN_W) {   // BACK
    sfxPlay(SFX_TAP);
    pickOpen = false;
    if (pickTrainer == PICK_LAN) { lanOpen = true; }
    else { gymOpen = true; }
    return;
  }
  if (y >= PICK_GO_Y && y <= PICK_GO_Y + PICK_BTN_H &&
      x >= PICK_GO_X && x <= PICK_GO_X + PICK_BTN_W) {
    uint8_t cap = squadCap(pickTrainer, pickHard);
    if (pickChosen() == 0 || pickChosen() > cap) return;   // GO stays inert
    sfxPlay(SFX_TAP);
    pickOpen = false;
    if (pickTrainer == PICK_LAN) {
      // The squad is chosen BEFORE the radio comes up, so what gets offered to
      // the peer is what the player picked -- lanOffer() builds lan.mine from
      // squadMask, and the fight is then rebuilt from lan.mine rather than from
      // the party (see startLinkBattle).
      lanOffer(lanWantHost);
      lanOpen = true;
      return;
    }
    // The fight has to be the one the picker just showed you. startTrainerBattle()
    // snapshots gymRegion into btlRegion, so handing the picker's own region back
    // to the chooser here is what keeps the cap you were shown, the leader you
    // were shown and the ladder the badge lands on all the same region -- without
    // giving the function a second source of truth to disagree with.
    gymRegion = pickRegion;
    startTrainerBattle(pickTrainer, pickHard);
    return;
  }
  uint8_t seen = 0, drawn = 0;
  for (uint8_t n = 0; n <= PARTY_SLOTS; n++) {
    if (!pickExists(n)) continue;
    if (seen++ < pickPage * PICK_PER_PAGE) continue;
    if (drawn >= PICK_PER_PAGE) break;
    int cx0 = PICK_X(drawn), cy0 = PICK_Y(drawn);
    drawn++;
    if (x < cx0 || x > cx0 + PICK_CELL_W || y < cy0 || y > cy0 + PICK_CELL_H) continue;
    squadMask ^= (1 << n);
    sfxPlay(SFX_TAP);
    return;
  }
}

// ---------- LAN battle ----------
// Pairing on a touch-only screen: one device hosts, the other joins, and the
// protocol does the rest. There is no MAC entry because there is no keyboard
// worth typing one on.
void renderLan() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_LAN)) * 6, 44);
  gfx->print(T(S_LAN));

  const char *msg = T(S_LAN_PICK);
  switch (lan.state) {
    case LINK_HANDSHAKE:
    case LINK_LISTENING: msg = T(S_LAN_WAIT); break;
    case LINK_SQUADS:    msg = T(S_LAN_WAIT); break;
    case LINK_READY:     msg = T(S_LAN_READY); break;
    case LINK_REFUSED:   msg = T(S_LAN_REFUSED); break;
    case LINK_LOST:      msg = T(S_LAN_GONE); break;
    case LINK_DONE:      msg = lan.youWon ? T(S_BTL_WIN) : T(S_BTL_LOSE); break;
    default: break;
  }
  gfx->setTextColor((lan.state == LINK_REFUSED || lan.state == LINK_LOST)
                      ? UI_BAR_BAD : UI_TRACK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(msg) * 3, 76);
  gfx->print(msg);

  if (lan.state == LINK_OFF || lan.state == LINK_REFUSED ||
      lan.state == LINK_LOST) {
    const char *lab[2] = { T(S_LAN_HOST), T(S_LAN_JOIN) };
    for (int i = 0; i < 2; i++) {
      int y = 120 + i * 70;
      gfx->fillRoundRect(90, y, 286, 56, 12, UI_BG_DAY);
      gfx->drawRoundRect(90, y, 286, 56, 12, UI_INK);
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)strlen(lab[i]) * 6, y + 20);
      gfx->print(lab[i]);
    }
  } else if (lan.state == LINK_READY) {
    char l[40];
    if (lan.peerName[0]) {
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)strlen(lan.peerName) * 6, 130);
      gfx->print(lan.peerName);
    }
    snprintf(l, sizeof(l), T(S_LAN_VS), lan.theirsN);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(l) * 6, 150);
    gfx->print(l);
    gfx->fillRoundRect(120, 220, 226, 56, 12, UI_BAR_OK);
    gfx->drawRoundRect(120, 220, 226, 56, 12, UI_INK);
    gfx->setTextColor(UI_BG_DAY);
    gfx->setCursor(CX - strlen(T(S_FIGHT)) * 6, 240);
    gfx->print(T(S_FIGHT));
  } else if (lan.state == LINK_DONE) {
    // Both squads are still in hand on both devices, so going again costs one
    // packet -- there is nothing to re-exchange.
    if (lan.peerName[0]) {
      gfx->setTextColor(UI_INK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)strlen(lan.peerName) * 6, 140);
      gfx->print(lan.peerName);
    }
    gfx->fillRoundRect(120, 220, 226, 56, 12, UI_BG_DAY);
    gfx->drawRoundRect(120, 220, 226, 56, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(T(S_LAN_REMATCH)) * 6, 240);
    gfx->print(T(S_LAN_REMATCH));
  }
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 392);
  gfx->print(T(S_BACK));
  gfx->flush();
}

// Hands our chosen squad to the link, then announces.
// Leaving deliberately: tell the peer so it reports at once instead of sitting
// out the timeout, then free the radio -- it costs real current.
void lanLeave() {
  if (lan.live()) lan.sendBye();
  linkNowEnd();
  lan.state = LINK_OFF;
}

static void lanOffer(bool host) {
  // The squad is built BEFORE the radio is touched. What we advertise has to be
  // exactly what the player just chose in the picker, and that does not depend
  // on whether the radio comes up -- doing it the other way round meant a
  // failed radio skipped the squad entirely and left nothing to inspect.
  lan.begin(host, pet.trainerName);
  snprintf(lan.peerName, sizeof(lan.peerName), "%s", pet.trainerName);
  buildSquad(0, TRAINER_TEAM_MAX, squadMask);
  for (uint8_t i = 0; i < btlSquadN; i++) {
    LinkMon m;
    linkMonFrom(m, btlSquad[i]);
    lan.addMon(m);
  }
  if (!linkNowBegin(&lan)) {          // no radio: say so rather than hanging
    lan.state = LINK_REFUSED;
    return;
  }
  // BOTH sides announce. Which of them ends up hosting is settled by id inside
  // the hello, so the buttons are only a preference -- two players who both tap
  // HOST still get a working fight instead of two authorities, and two who both
  // tap JOIN still get one instead of mutual silence.
  lan.start();
}

void lanTap(int16_t x, int16_t y) {
  if (lan.state == LINK_OFF || lan.state == LINK_REFUSED ||
      lan.state == LINK_LOST) {
    for (int i = 0; i < 2; i++) {
      int by = 120 + i * 70;
      if (x < 90 || x > 376 || y < by || y > by + 56) continue;
      sfxPlay(SFX_TAP);
      lanWantHost = (i == 0);
      lanOpen = false;
      pickTrainer = PICK_LAN;
      pickHard = false;
      pickRegion = gymRegion;   // no ladder in a LAN fight; kept in step anyway
      pickPage = 0;
      pickDefault(squadCap(PICK_LAN, false));
      pickOpen = true;
      return;
    }
  } else if (lan.state == LINK_READY) {
    if (x >= 120 && x <= 346 && y >= 220 && y <= 276) {
      sfxPlay(SFX_TAP);
      lanOpen = false;
      startLinkBattle();
      return;
    }
  } else if (lan.state == LINK_DONE) {
    if (x >= 120 && x <= 346 && y >= 220 && y <= 276) {
      sfxPlay(SFX_TAP);
      lan.sendRematch();      // both sides go back to READY and tap FIGHT
      return;
    }
  }
  if (y > 370) { lanLeave(); lanOpen = false; }   // back
}

// ---------- gym list ----------
void renderGyms() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  // The ladder's own region in the title, since a vertical swipe moves between
  // three of them and "GYMS" alone would not say which you are looking at.
  char title[28];
  snprintf(title, sizeof(title), "%s %s", TRAINER_SETS[gymRegion % GYM_REGIONS].region,
           T(S_GYMS));
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(title) * 6, 42);
  gfx->print(title);
  // The badge count used to sit here, directly behind the difficulty pill. The
  // region chooser already shows it per region, which is where you are choosing
  // from, so it was both redundant and in the way.
  // difficulty pill: hard caps YOUR team to the leader's size and level, so it
  // is a different ladder with its own badges rather than a damage multiplier
  const char *dif = T(gymHard ? S_HARD : S_EASY);
  int dw = (int)strlen(dif) * 12 + 48;      // wider as well as taller
  if (dw < 120) dw = 120;
  gfx->fillRoundRect(CX - dw / 2, GYMDIF_Y, dw, GYMDIF_H, 12,
                     gymHard ? UI_BAR_BAD : UI_TRACK);
  gfx->setTextColor(gymHard ? UI_BG_DAY : UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(dif) * 6, GYMDIF_Y + 14);
  gfx->print(dif);

  for (int i = 0; i < GYM_ROWS; i++) {
    uint8_t idx = gymPage * GYM_ROWS + i;
    if (idx >= TRAINER_COUNT) break;
    const Trainer &t = TRAINERS[idx];
    int rx, y, rw, rh;
    gymRowRect(i, &rx, &y, &rw, &rh);
    bool done = pet.hasBadge(gymRegion, idx, gymHard);
    bool open_ = gymUnlocked(idx, gymHard);
    gfx->fillRoundRect(rx, y, rw, rh, 10, done ? UI_TRACK : UI_BG_DAY);
    gfx->drawRoundRect(rx, y, rw, rh, 10, open_ ? UI_INK : UI_TRACK);
    gfx->setTextColor(open_ ? UI_INK : UI_TRACK);
    gfx->setTextSize(2);
    gfx->setCursor(rx + 14, y + 8);
    gfx->print(t.name);
    gfx->setTextSize(1);
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setCursor(rx + 14, y + 28);
    gfx->print(open_ ? t.place : T(S_LOCKED));
    // the level of the strongest creature: the honest measure of the wall
    uint8_t top = 0;
    for (int k = 0; k < t.count; k++)
      if (t.team[k].level > top) top = t.team[k].level;
    char lv[16];
    snprintf(lv, sizeof(lv), "Lv.%u x%u", top, t.count);
    gfx->setTextColor(done ? UI_BAR_OK : (open_ ? UI_INK : UI_TRACK));
    gfx->setCursor(rx + rw - 12 - (int)strlen(lv) * 6, y + 28);
    gfx->print(lv);
    if (done) {
      gfx->setTextColor(UI_BAR_OK);
      gfx->setCursor(rx + rw - 26, y + 8);
      gfx->print("*");
    }
  }
  gfx->fillRoundRect(GYMBTN_X, GYMBTN_Y, GYMBTN_W, GYMBTN_H, 11, UI_BG_DAY);
  gfx->drawRoundRect(GYMBTN_X, GYMBTN_Y, GYMBTN_W, GYMBTN_H, 11, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(GYMBTN_X + (GYMBTN_W - (int)strlen(T(S_LAN)) * 12) / 2, GYMBTN_Y + 14);
  gfx->print(T(S_LAN));
  uiChrome();
  gfx->flush();
}

// The region pill under a waiting egg. Tapping it cycles; Pet::setRegion swaps
// the egg to that region's creature, keeping the rarity it was granted and
// remembering each region's answer so flipping back and forth is not a re-roll.
#define EGGREG_X 133
#define EGGREG_Y 374
#define EGGREG_W 200
#define EGGREG_H 34
// The hit area is BIGGER than the pill, like the BOX button and the battle
// grid, and for the same reason: a 34 px target is under UI_TAP_MIN and a
// finger is not a stylus.
//
// The guard band matters more than the padding. Missing this pill fell through
// to pet.eggTap(), and THREE taps hatch the egg -- so fumbling at the region
// selector hatched the very egg you were trying to re-aim. A near miss now
// does nothing at all, which is the correct answer for a control whose
// neighbour is irreversible.
#define EGGREG_PAD 16
#define EGGREG_GUARD 14

static void drawEggRegion() {
  char l[24];
  snprintf(l, sizeof(l), "%s >", pet.regionName());
  gfx->fillRoundRect(EGGREG_X, EGGREG_Y, EGGREG_W, EGGREG_H, 10, UI_WHITE);
  gfx->drawRoundRect(EGGREG_X, EGGREG_Y, EGGREG_W, EGGREG_H, 10, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(EGGREG_X + (EGGREG_W - (int)strlen(l) * 12) / 2, EGGREG_Y + 9);
  gfx->print(l);
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(T(S_EGG_REGION)) * 3, EGGREG_Y + EGGREG_H + 6);
  gfx->print(T(S_EGG_REGION));
}

// True if the tap was on the region pill, so the egg does not also get cracked.
// The egg's region pill: its graphic, and the area that actually accepts a tap.
// Exposed so a test can prove the second is bigger than the first and that a
// near miss does not reach pet.eggTap().
void uiEggPillRect(int *x, int *y, int *w, int *h, bool hitArea) {
  int pad = hitArea ? EGGREG_PAD : 0;
  if (x) *x = EGGREG_X - pad;
  if (y) *y = EGGREG_Y - pad;
  if (w) *w = EGGREG_W + 2 * pad;
  if (h) *h = EGGREG_H + 2 * pad;
}

// 1 = cycled the region, -1 = a near miss that must NOT reach the egg, 0 = not
// ours at all.
static int eggRegionTap(int16_t x, int16_t y) {
  if (!pet.isEgg()) return 0;
  int inset = EGGREG_PAD, guard = EGGREG_PAD + EGGREG_GUARD;
  bool hit = x >= EGGREG_X - inset && x <= EGGREG_X + EGGREG_W + inset &&
             y >= EGGREG_Y - inset && y <= EGGREG_Y + EGGREG_H + inset;
  if (hit) {
    pet.setRegion(nextAvailableRegion(pet.region));
    sfxPlay(SFX_TAP);
    return 1;
  }
  bool near = x >= EGGREG_X - guard && x <= EGGREG_X + EGGREG_W + guard &&
              y >= EGGREG_Y - guard && y <= EGGREG_Y + EGGREG_H + guard;
  if (near) { sfxPlay(SFX_DENY); return -1; }
  return 0;
}

// The region chooser used by the Pokedex and the gym ladder. Each row carries
// its own progress, so the screen answers "where am I up to" as well as "where
// do I want to go".
#define RPICK_X 74
#define RPICK_W 318
#define RPICK_H 62
#define RPICK_Y(i) (108 + (i) * 72)
// The page dots sit BETWEEN the last row and the LAN button, not over it.
// They used to be at y=366, which is inside the LAN button (336..380) and on
// top of its label -- invisible until a region chooser had more than one page,
// so it appeared the moment Sinnoh made GYM_REGIONS 4 and went unnoticed
// because swipe_test drives the paging and never looks at the pixels.
#define RPICK_DOTS_Y 325
#define RPICK_DOT_R 5
// Guards, not decoration: both of these are the collision that shipped, and a
// static_assert is the only check that cannot be forgotten when somebody moves
// a button. The dots must clear the LAN button below and the last row above.
static_assert(RPICK_DOTS_Y + RPICK_DOT_R < LANBTN_Y,
              "the region-chooser page dots overlap the LAN button");
static_assert(RPICK_DOTS_Y - RPICK_DOT_R > RPICK_Y(RPICK_PER_PAGE - 1) + RPICK_H,
              "the region-chooser page dots overlap the last region row");

// How many regions this mode lists. Gyms genuinely only exist for three
// (GYM_REGIONS); the Pokedex and the starter screen list every REAL region,
// which is GAL_REGIONS -- REGION_COUNT minus the ALL pseudo-region.
//
// This used to be GYM_REGIONS for ALL THREE MODES, with the names read out of
// TRAINER_SETS -- a trainer table driving the Pokedex chooser. So the moment
// Sinnoh landed it had no row, while the gallery's vertical swipe cycled it
// happily: built, reachable, and looking absent. That is the exact failure the
// chooser was added to prevent.
uint8_t rpickRegions(uint8_t mode) {
  return (mode == RPICK_FOR_GYMS) ? (uint8_t)GYM_REGIONS : (uint8_t)GAL_REGIONS;
}

uint8_t rpickPageCount(uint8_t mode) {
  uint8_t n = rpickRegions(mode);
  uint8_t p = (uint8_t)((n + RPICK_PER_PAGE - 1) / RPICK_PER_PAGE);
  return p ? p : 1;
}

// The chooser WRAPS rather than closing off the end, unlike the other paged
// screens. It is the root of its own screen, and at first boot there is
// nowhere to go back to at all -- exiting would strand the player before they
// have chosen anything.
// Which chooser is on the panel, or 0xFF for none. THE single answer: the
// swipe handler and swipe_test both ask this rather than each deciding.
uint8_t rpickModeNow() {
  switch (uiCurrentScreen()) {
    case SCR_REGION:  return RPICK_FOR_START;
    case SCR_DEXPICK: return RPICK_FOR_DEX;
    case SCR_GYMPICK: return RPICK_FOR_GYMS;
    default: return 0xFF;
  }
}

static bool rpickSwipe(int dir) {
  uint8_t mode = rpickModeNow();
  if (mode == 0xFF) return false;
  uint8_t pages = rpickPageCount(mode);
  int p = (int)rpickPage + (dir > 0 ? -1 : 1);
  if (p < 0) p = pages - 1;
  if (p >= pages) p = 0;
  rpickPage = (uint8_t)p;
  sfxPlay(SFX_TAP);
  return true;
}

static void renderRegionPick(uint8_t mode) {
  bool forGyms = (mode == RPICK_FOR_GYMS);
  uint8_t nreg = rpickRegions(mode);
  uint8_t pages = rpickPageCount(mode);
  if (rpickPage >= pages) rpickPage = 0;
  uint8_t first = (uint8_t)(rpickPage * RPICK_PER_PAGE);
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  char ttl[40];
  if (mode == RPICK_FOR_START) snprintf(ttl, sizeof(ttl), "%s", T(S_CHOOSE_REGION));
  else if (forGyms) snprintf(ttl, sizeof(ttl), "%s", T(S_GYMS));
  else snprintf(ttl, sizeof(ttl), T(S_POKEDEX_FMT),
                pet.dexCountIn(DEXV_COMPLETE, false, 1, DEX_COUNT), DEX_COUNT);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(ttl) * 6, 48);
  gfx->print(ttl);

  for (uint8_t row = 0; row < RPICK_PER_PAGE; row++) {
    uint8_t i = (uint8_t)(first + row);
    if (i >= nreg) break;
    int y = RPICK_Y(row);
    // The sprite pack is the gate. A region without it is drawn GREYED with a
    // reason rather than dropped from the list: hiding it would say "this
    // region does not exist" when what we mean is "download its pack".
    //
    // AND THAT NOW INCLUDES THE GYMS. This read `forGyms || regionAvailable(i)`,
    // so a ladder was always enterable whether its pack was on the card or not --
    // the rule was enforced for the Pokedex and the egg pool and not for its twin,
    // which is the shape CLAUDE.md keeps warning about. The cost was a gym leader
    // opening with a bare dex NUMBER where a creature should be, which trainers.h
    // calls out as the one thing that must not happen. Alola could already do it;
    // Galar and Paldea would have made it ordinary.
    //
    // It fails in the safe direction: gRegionArt defaults to ALL SET, so a board
    // with no card at all keeps every ladder exactly as before.
    bool open = regionAvailable(i);
    gfx->fillRoundRect(RPICK_X, y, RPICK_W, RPICK_H, 12, open ? UI_WHITE : UI_BG_DAY);
    gfx->drawRoundRect(RPICK_X, y, RPICK_W, RPICK_H, 12, open ? UI_INK : UI_TRACK);
    const char *nm = forGyms ? TRAINER_SETS[i].region : REGIONS[i].name;
    gfx->setTextColor(open ? UI_INK : UI_TRACK);
    gfx->setTextSize(3);
    gfx->setCursor(RPICK_X + 18, y + 12);
    gfx->print(nm);
    // At first boot there is no subtitle: naming the starter here would give
    // away the next screen, and the counts the other two modes show would all
    // read zero on a new save anyway.
    char sub[28];
    sub[0] = 0;
    if (!open)
      snprintf(sub, sizeof(sub), "%s", T(S_NEED_PACK));
    else if (mode == RPICK_FOR_GYMS)
      snprintf(sub, sizeof(sub), T(S_BADGES_FMT), pet.badgeCountIn(i, gymHard));
    else if (mode == RPICK_FOR_DEX)
      snprintf(sub, sizeof(sub), "%u/%u",
               pet.dexCountIn(DEXV_COMPLETE, false, REGIONS[i].lo, REGIONS[i].hi),
               (unsigned)(REGIONS[i].hi - REGIONS[i].lo + 1));
    if (sub[0]) {
      gfx->setTextColor(UI_INK_SOFT);
      gfx->setTextSize(2);
      gfx->setCursor(RPICK_X + RPICK_W - 18 - (int)strlen(sub) * 12, y + 22);
      gfx->print(sub);
    }
  }
  if (mode == RPICK_FOR_START) {          // the random starter from any region
    const char *rl = T(S_STARTER_RANDOM);
    gfx->fillRoundRect(LANBTN_X, LANBTN_Y, LANBTN_W, LANBTN_H, 11, UI_WHITE);
    gfx->drawRoundRect(LANBTN_X, LANBTN_Y, LANBTN_W, LANBTN_H, 11, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(rl) * 6, LANBTN_Y + 14);
    gfx->print(rl);
  }
  if (forGyms) {
    gfx->fillRoundRect(LANBTN_X, LANBTN_Y, LANBTN_W, LANBTN_H, 11, UI_BG_DAY);
    gfx->drawRoundRect(LANBTN_X, LANBTN_Y, LANBTN_W, LANBTN_H, 11, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(T(S_LAN)) * 6, LANBTN_Y + 14);
    gfx->print(T(S_LAN));
  }
  if (pages > 1) {                        // dots: which page of regions this is
    int total = pages * 16 - 8;
    for (uint8_t d = 0; d < pages; d++) {
      int cx = CX - total / 2 + d * 16;
      if (d == rpickPage) gfx->fillCircle(cx, RPICK_DOTS_Y, RPICK_DOT_R, UI_INK);
      else gfx->drawCircle(cx, RPICK_DOTS_Y, RPICK_DOT_R, UI_TRACK);
    }
  }
  if (mode != RPICK_FOR_START) {          // first boot has nowhere to go back to
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 392);
    gfx->print(T(S_BACK));
  }
  uiChrome();
  gfx->flush();
}

// Returns the region tapped, or -1. Takes the mode because the row on screen is
// an offset into the current page, not the region index -- and because a region
// whose sprite pack is missing must not be selectable, which is the whole point
// of the gate. The chooser still SHOWS it, greyed, saying why.
// Where a region-chooser row is, so a test can tap one without restating the
// geometry. Same reason uiMenuRowCenterY() and uiTrainRowTop() exist.
int uiRegionRowCenterY(int row) { return RPICK_Y(row) + RPICK_H / 2; }
int uiRegionRowX() { return RPICK_X + RPICK_W / 2; }
int uiRegionRowsPerPage() { return RPICK_PER_PAGE; }

int regionPickTap(int16_t x, int16_t y, uint8_t mode) {
  if (x < RPICK_X || x > RPICK_X + RPICK_W) return -1;
  uint8_t nreg = rpickRegions(mode);
  for (uint8_t row = 0; row < RPICK_PER_PAGE; row++) {
    uint8_t i = (uint8_t)(rpickPage * RPICK_PER_PAGE + row);
    if (i >= nreg) break;
    if (y >= RPICK_Y(row) && y <= RPICK_Y(row) + RPICK_H) {
      // Every mode, gyms included -- the draw path greys the row, so the tap path
      // has to refuse it or the two disagree and a greyed row still works.
      if (!regionAvailable(i)) {
        sfxPlay(SFX_DENY);      // locked: say no out loud rather than do nothing
        return -1;
      }
      return i;
    }
  }
  return -1;
}

// ---------- level-up learn prompt ----------
void renderLearn() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  uint8_t mv = pet.learnOffer();
  char head[40];
  const char *nm = pet.nick[0] ? pet.nick : DEX_TBL[pet.speciesId].name;
  snprintf(head, sizeof(head), T(S_LEARN_Q), nm);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(head) * 3, 48);
  gfx->print(head);
  gfx->setTextColor(DEX_TBL[pet.speciesId].accent);
  const char *move = moveName(mv);
  int moveSize = strlen(move) <= 17 ? 3 : 2;
  gfx->setTextSize(moveSize);
  gfx->setCursor(CX - (int)strlen(move) * moveSize * 3, moveSize == 3 ? 66 : 72);
  gfx->print(move);

  for (int i = 0; i < MOVE_SLOTS; i++) drawMoveRow(LEARN_ROW_Y(i), pet.moves[i], false, pet.speciesId);

  gfx->fillRoundRect(70, LEARN_SKIP_Y, 326, 44, 12, UI_TRACK);
  gfx->drawRoundRect(70, LEARN_SKIP_Y, 326, 44, 12, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_LEARN_SKIP)) * 6, LEARN_SKIP_Y + 14);
  gfx->print(T(S_LEARN_SKIP));
  gfx->flush();
}

// pagina 2: medallas con etiqueta descriptiva
void renderCardMedals() {
  int got = 0;
  for (int i = 0; i < MED_COUNT; i++)
    if (pet.hasMedal(1 << i)) got++;
  char head[20];
  snprintf(head, sizeof(head), T(S_MEDALS_FMT), got, MED_COUNT);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(head) * 9, 48);
  gfx->print(head);

  for (int i = 0; i < MED_COUNT; i++) {
    int x = 28 + (i % 2) * 206, y = 104 + (i / 2) * 54;
    bool g = pet.hasMedal(1 << i);
    gfx->fillRoundRect(x, y, 196, 44, 10, g ? UI_BAR_OK : UI_TRACK);
    if (g) {  // marca de conseguida
      gfx->fillCircle(x + 22, y + 22, 11, UI_BG_DAY);
      gfx->setTextColor(UI_BAR_OK);
      gfx->setTextSize(2);
      gfx->setCursor(x + 16, y + 13);
      gfx->print("v");
    }
    gfx->setTextColor(g ? UI_BG_DAY : 0x8410);
    gfx->setTextSize(2);
    gfx->setCursor(x + 44, y + 14);
    gfx->print(medalDesc(i));
  }
}

// pagina 3: progreso (nivel, evolucion, descuidos) — saca a la luz mecanicas
// que antes eran invisibles (cuanto falta para subir/evolucionar y por que)
void renderCardProgress() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_PROGRESS)) * 9, 44);
  gfx->print(T(S_PROGRESS));

  // nivel grande
  char lv[10];
  snprintf(lv, sizeof(lv), T(S_LVL_FMT), pet.level());
  gfx->setTextSize(5);
  gfx->setCursor(CX - strlen(lv) * 15, 86);
  gfx->print(lv);

  // barra de progreso al siguiente nivel (1 nivel = 60 min de juego; the
  // speed setting decides how many real minutes that is)
  uint8_t into = pet.ageMinutes % MINUTES_PER_LEVEL;
  int bx = 93, bw = 280, by = 158, bh = 22;
  gfx->fillRoundRect(bx, by, bw, bh, 6, UI_TRACK);
  int fw = (bw - 4) * into / MINUTES_PER_LEVEL;
  if (fw > 0) gfx->fillRoundRect(bx + 2, by + 2, fw, bh - 4, 5, UI_BAR_OK);
  char nx[26];
  snprintf(nx, sizeof(nx), T(S_NEXT_LVL_FMT),
           (unsigned)pet.realMinutesToNextLevel(), pet.level() + 1);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(nx) * 6, by + 32);
  gfx->print(nx);

  // estado de evolucion
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setCursor(CX - strlen(T(S_EVO_LABEL)) * 6, 230);
  gfx->print(T(S_EVO_LABEL));
  char evoBuf[28];
  const char *evo;
  uint16_t evoCol = UI_INK;
  if (d.evolvesTo == 0) {
    evo = T(S_FINAL_FORM);
  } else {
    // The SAME sum canEvolveNow() uses -- including the day owed for retiring
    // the previous creature early. A card that left evoPenalty() out would
    // promise an evolution that then does not happen.
    int needed = d.evolveLevel + pet.careMistakes + pet.evoPenalty();
    if (pet.level() >= needed) {
      if (pet.lowestStat() >= 40) { evo = T(S_EVO_READY); evoCol = UI_BAR_OK; }
      else { evo = T(S_EVO_BLOCKED); evoCol = UI_BAR_BAD; }
    } else {
      snprintf(evoBuf, sizeof(evoBuf), T(S_EVO_IN_FMT), needed - pet.level());
      evo = evoBuf;
    }
  }
  gfx->setTextColor(evoCol);
  gfx->setCursor(CX - strlen(evo) * 6, 256);
  gfx->print(evo);

  // the day inherited from an early retire, said out loud -- otherwise this
  // creature simply evolves late and the player has no way to know why
  if (pet.evoPenalty()) {
    gfx->setTextSize(1);
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setCursor(CX - (int)strlen(T(S_EVO_SLOW)) * 3, 286);
    gfx->print(T(S_EVO_SLOW));
    gfx->setTextSize(2);
  }

  // descuidos (retrasan la evolucion)
  char ms[24];
  snprintf(ms, sizeof(ms), T(S_MISTAKES_FMT), pet.careMistakes);
  gfx->setTextColor(pet.careMistakes > 0 ? UI_BAR_BAD : UI_INK);
  gfx->setCursor(CX - strlen(ms) * 6, 312);
  gfx->print(ms);
}

void renderCard() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  if (cardPage == 0) renderCardProfile();
  else if (cardPage == 1) renderCardStats();
  else if (cardPage == 2) renderCardMoves();
  else renderCardProgress();

  // indicador de paginas + ayuda
  for (int i = 0; i < CARD_PAGES; i++) {
    int dx = CX - (CARD_PAGES - 1) * 13 + i * 26;
    if (i == cardPage) gfx->fillCircle(dx, 374, 5, UI_INK);
    else gfx->drawCircle(dx, 374, 4, UI_INK);
  }
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 398);
  gfx->print(T(S_BACK));
  uiChrome();
  gfx->flush();
}

// ---------- menu overlay ----------

// Row labels are built fresh each frame because two of them carry live counts.
static void menuRowLabel(int i, char *out, size_t n) {
  switch (i) {
    case MENU_STATS: snprintf(out, n, "%s", T(S_STATS)); break;
    case MENU_BAG:   snprintf(out, n, "%s", T(S_BAG)); break;
    case MENU_DEX:   snprintf(out, n, T(S_POKEDEX_FMT),
                              pet.dexCountIn(DEXV_COMPLETE, false, 1, DEX_COUNT), DEX_COUNT); break;
    case MENU_JUKE:     snprintf(out, n, "%s", T(S_JUKEBOX)); break;
    case MENU_SETTINGS: snprintf(out, n, "%s", T(S_SETTINGS)); break;
    case MENU_RETIRE:   snprintf(out, n, "%s", T(S_RETIRE)); break;
    default:            snprintf(out, n, "%s", T(S_CLOSE)); break;
  }
}

int uiMenuRowCenterY(int i) { return MENU_ROW_Y(i) + MENU_ROW_H / 2; }

void drawMenu() {
  // dim the game behind the panel so the overlay reads as modal, and so it is
  // obvious that tapping the darkened area is a way out
  for (int y = 0; y < 466; y += 2)
    gfx->drawFastHLine(0, y, 466, gNight ? 0x0000 : 0x2104);

  gfx->fillRoundRect(MENU_X, MENU_Y, MENU_W, MENU_H, 18, UI_WHITE);
  gfx->drawRoundRect(MENU_X, MENU_Y, MENU_W, MENU_H, 18, UI_INK);

  for (int i = 0; i < MENU_ROWS; i++) {
    int y = MENU_ROW_Y(i);
    bool close = (i == MENU_CLOSE);
    bool dead = (i == MENU_RETIRE && !pet.canRetireNow());   // an egg or a companion
    bool retire = (i == MENU_RETIRE && !dead);
    gfx->fillRoundRect(MENU_X + 12, y, MENU_W - 24, MENU_ROW_H, 12,
                       retire ? UI_BAR_BAD : close || dead ? UI_TRACK : UI_BG_DAY);
    gfx->drawRoundRect(MENU_X + 12, y, MENU_W - 24, MENU_ROW_H, 12, UI_INK);
    char lbl[28];
    menuRowLabel(i, lbl, sizeof(lbl));
    gfx->setTextColor(retire ? UI_WHITE : UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(lbl) * 6, y + MENU_ROW_H / 2 - 8);
    gfx->print(lbl);
  }
}

// ---------- jukebox ----------

static const uint8_t JUKE_ORDER[] = {
  TUNE_TITLE, TUNE_PALLET, TUNE_ROUTE1, TUNE_CENTER, TUNE_LAVENDER,
  TUNE_WILD, TUNE_TRN, TUNE_GYM, TUNE_WIN,
};
static_assert(sizeof(JUKE_ORDER) == TUNE_COUNT, "every song needs a place in JUKE_ORDER");
static_assert(S_TUNE_ROUTE1 - S_TUNE_GYM + 1 == TUNE_COUNT,
              "S_TUNE_* names must run in TUNE_* order");

const char *tuneName(uint8_t t) { return T((StrId)(S_TUNE_GYM + (t < TUNE_COUNT ? t : 0))); }

static void jukeHint(uint8_t t, char *out, size_t n) {
  const TuneRule &r = tuneRule(t);
  switch (r.kind) {
    case TRULE_BADGES:
      if (r.n == 1) snprintf(out, n, "%s", T(S_TRULE_BADGE));
      else snprintf(out, n, T(S_TRULE_BADGES_FMT), r.n);
      break;
    case TRULE_HATCH:   snprintf(out, n, "%s", T(S_TRULE_HATCH)); break;
    case TRULE_GOODBYE: snprintf(out, n, "%s", T(S_TRULE_GOODBYE)); break;
    case TRULE_STREAK:  snprintf(out, n, T(S_TRULE_STREAK_FMT), r.n); break;
    case TRULE_CAUGHT:  snprintf(out, n, "%s", T(S_TRULE_CAUGHT)); break;
    default:            snprintf(out, n, "?"); break;
  }
}

void jukeRowRect(int i, int *x, int *y, int *w, int *h) {
  int ry = JUKE_ROW_Y(i);
  int half = uiSafeHalfWidthFor(ry, ry + JUKE_ROW_H) - 18;
  if (x) *x = CX - half;
  if (y) *y = ry;
  if (w) *w = half * 2;
  if (h) *h = JUKE_ROW_H;
}

void jukeNavRect(int which, int *x, int *y, int *w, int *h) {
  int half = uiSafeHalfWidthFor(JUKE_NAV_Y, JUKE_NAV_Y + JUKE_NAV_H) - 20;
  if (which < 0) {
    if (x) *x = CX - half;
    if (w) *w = 54;
  } else if (which > 0) {
    if (x) *x = CX + half - 54;
    if (w) *w = 54;
  } else {
    if (x) *x = CX - half + 54 + 8;
    if (w) *w = half * 2 - 2 * (54 + 8);
  }
  if (y) *y = JUKE_NAV_Y;
  if (h) *h = JUKE_NAV_H;
}

void openJukebox() {
  pet.checkTunes(false);
  jukeOpen = true;
  jukePage = 0;
}

bool jukeAudible() {
  return audioCurrentTrack() != 0xFF && audioEnabled() && audioVolume() > 0;
}

void jukeSwipe(int dir) {
  if (dir < 0 && jukePage + 1 < JUKE_PAGES) { jukePage++; sfxPlay(SFX_TAP); }
  else if (dir > 0 && jukePage > 0) { jukePage--; sfxPlay(SFX_TAP); }
  else sfxPlay(SFX_DENY);
}

void jukeTap(int16_t x, int16_t y) {
  for (int which = -1; which <= 1; which++) {
    int nx, ny, nw, nh;
    jukeNavRect(which, &nx, &ny, &nw, &nh);
    if (x >= nx && x <= nx + nw && y >= ny && y <= ny + nh) {
      if (which < 0) { if (jukePage > 0) { jukePage--; sfxPlay(SFX_TAP); } else sfxPlay(SFX_DENY); }
      else if (which > 0) { if (jukePage + 1 < JUKE_PAGES) { jukePage++; sfxPlay(SFX_TAP); } else sfxPlay(SFX_DENY); }
      else { jukeOpen = false; sfxPlay(SFX_TAP); }
      return;
    }
  }
  for (uint8_t i = 0; i < JUKE_PER_PAGE; i++) {
    uint8_t idx = jukePage * JUKE_PER_PAGE + i;
    if (idx >= TUNE_COUNT) break;
    int rx, ry, rw, rh;
    jukeRowRect(i, &rx, &ry, &rw, &rh);
    if (x < rx || x > rx + rw || y < ry || y > ry + rh) continue;
    uint8_t t = JUKE_ORDER[idx];
    if (!pet.tuneUnlocked(t)) { sfxPlay(SFX_DENY); return; }
    if (audioCurrentTrack() == t) {
      audioMusic(MUS_NONE);
      sfxPlay(SFX_TAP);
    } else {
      audioPlayTrack(t);
    }
    return;
  }
}

static void jukeIcon(int cx, int cy, bool playing, uint16_t col) {
  if (playing) gfx->fillRect(cx - 7, cy - 7, 14, 14, col);
  else gfx->fillTriangle(cx - 6, cy - 9, cx - 6, cy + 9, cx + 9, cy, col);
}

void renderJukebox() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)strlen(T(S_JUKEBOX)) * 9, 30);
  gfx->print(T(S_JUKEBOX));

  const bool muted = !audioEnabled() || audioVolume() == 0;
  char sub[36];
  if (muted) snprintf(sub, sizeof(sub), "%s", T(S_JUKE_SOUND_OFF));
  else snprintf(sub, sizeof(sub), "%u/%u", (unsigned)(jukePage + 1), (unsigned)JUKE_PAGES);
  gfx->setTextColor(muted ? UI_BAR_BAD : UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(sub) * 6, 60);
  gfx->print(sub);

  uint8_t on = audioCurrentTrack();
  for (uint8_t i = 0; i < JUKE_PER_PAGE; i++) {
    uint8_t idx = jukePage * JUKE_PER_PAGE + i;
    if (idx >= TUNE_COUNT) break;
    uint8_t t = JUKE_ORDER[idx];
    int rx, ry, rw, rh;
    jukeRowRect(i, &rx, &ry, &rw, &rh);
    bool open_ = pet.tuneUnlocked(t);
    bool playing = (on == t);
    gfx->fillRoundRect(rx, ry, rw, rh, 12,
                       playing ? UI_BAR_OK : open_ ? UI_WHITE : UI_TRACK);
    gfx->drawRoundRect(rx, ry, rw, rh, 12, UI_INK);
    if (open_) {
      uint16_t ink = playing ? UI_WHITE : UI_INK;
      jukeIcon(rx + 24, ry + rh / 2, playing, ink);
      const char *nm = tuneName(t);
      gfx->setTextColor(ink);
      gfx->setTextSize(2);
      gfx->setCursor(rx + 46, ry + rh / 2 - 8);
      gfx->print(nm);
    } else {
      char hint[48];
      jukeHint(t, hint, sizeof(hint));
      gfx->setTextColor(UI_INK_SOFT);
      gfx->setTextSize(1);
      gfx->setCursor(rx + 18, ry + 10);
      gfx->print("???");
      gfx->setTextColor(UI_INK);
      gfx->setCursor(rx + 18, ry + 28);
      gfx->print(hint);
    }
  }

  for (int which = -1; which <= 1; which++) {
    int nx, ny, nw, nh;
    jukeNavRect(which, &nx, &ny, &nw, &nh);
    const bool live = (which == 0) || (which < 0 ? jukePage > 0 : jukePage + 1 < JUKE_PAGES);
    gfx->fillRoundRect(nx, ny, nw, nh, 10, live ? UI_WHITE : UI_TRACK);
    gfx->drawRoundRect(nx, ny, nw, nh, 10, UI_INK);
    gfx->setTextColor(live ? UI_INK : 0x8410);
    if (which == 0) {
      const char *bl = T(S_CLOSE);
      gfx->setTextSize(2);
      gfx->setCursor(nx + (nw - (int)strlen(bl) * 12) / 2, ny + nh / 2 - 8);
      gfx->print(bl);
    } else {
      gfx->setTextSize(3);
      gfx->setCursor(nx + nw / 2 - 9, ny + nh / 2 - 11);
      gfx->print(which < 0 ? "<" : ">");
    }
  }
  uiChrome();
  gfx->flush();
}

// ---------- who you are raising ----------

// Exchange the creature on the main screen with a stored one.
//
// A TRUE exchange: the live pet takes the slot the newcomer vacates, so this
// needs no free slot and nothing can be lost. Both records carry their full
// care state, so swapping away and back returns the creature exactly as it was
// rather than handing back a blanked copy -- which is the whole reason
// PartyMon grew a care block.
//
// An EGG is the one asymmetric case: it has nothing to bank, so the slot simply
// empties. That is what the old frozen BRING BACK did, and it is the only way
// this can cost you anything.
// JOURNALLED, because the exchange spans two checkpoint records and cannot be one
// commit. Party::focusBegin() carries the essay; the shape here is:
//
//   1. write the intent, holding BOTH creatures        (one small record)
//   2. put the outgoing creature in the slot           (the pair record)
//   3. make the incoming creature the live one         (the pet record)
//   4. retract the intent
//
// A cut anywhere in 2..3 is repaired at boot by focusRecover(), which forces the
// end state rather than guessing which half landed. A cut before 2 leaves the
// game exactly as it was and the journal is replayed onto an unchanged state,
// which lands in the same place.
static bool focusMonValid(const PartyMon &m) {
  if (m.empty() || m.dex > DEX_COUNT || !memchr(m.nick, '\0', sizeof(m.nick))) return false;
  for (uint8_t move : m.moves) if (move >= MOVE_COUNT) return false;
  return true;
}

bool focusSwap(uint8_t slot) {
  if (slot >= PARTY_SLOTS || !party.writable()) return false;
  uint8_t pendingSlot; PartyMon pendingOut, pendingIn; bool pendingEgg; uint32_t pendingGen;
  if (party.focusPending(&pendingSlot, &pendingOut, &pendingEgg, &pendingIn, &pendingGen))
    return false;
  if (!party.writable()) return false;
  PartyMon incoming = party.slots[slot];   // by value: the slot is about to change
  if (!focusMonValid(incoming)) return false;
  const bool wasEgg = pet.isEgg();
  const PartyMon outgoing = wasEgg ? PartyMon() : pet.toPartyMon();
  if (!party.focusBegin(slot, outgoing, wasEgg, incoming, pet.petGeneration()))
    return false;
  focusBlocked = true;
  if (!(wasEgg ? party.releaseAt(slot) : party.replaceAt(slot, outgoing))) return false;
  const uint32_t gen = pet.petGeneration();
  pet.switchTo(incoming);
  if (pet.petGeneration() == gen) {
    // Keep the journal authoritative; normal gameplay must not save over this
    // half-finished exchange before recovery completes it.
    return false;
  }
  const bool acknowledged = party.focusEnd();
  focusBlocked = false;
  return acknowledged;
}

// Finishes a swap that lost power part way through. Called once at boot, AFTER
// pet.begin() -- it needs the pet checkpoint's generation loaded to tell an
// unfinished swap from a stale journal.
void focusRecover() {
  uint8_t slot = 0;
  PartyMon outgoing, incoming;
  bool wasEgg = false;
  uint32_t armedGen = 0;
  if (!party.focusPending(&slot, &outgoing, &wasEgg, &incoming, &armedGen)) {
    focusBlocked = false;
    return;
  }
  focusBlocked = true;
  if (slot >= PARTY_SLOTS || !focusMonValid(incoming) ||
      (!wasEgg && !focusMonValid(outgoing))) {
    static bool reported = false;
    if (!reported) Serial.println("save: invalid focus intent; recovery required, NOT discarding");
    reported = true;
    return;
  }

  // The pet record's generation is the identity PartyMon lacks. switchTo() saves
  // once, so armedGen + 1 means the pet half committed; anything above that means
  // the game saved again afterwards and this journal is merely stale. Replaying
  // either would undo real play.
  if (pet.petGeneration() > armedGen) {
    // A prior pet commit proves the party half landed first. Leave the journal
    // in place if storage is unavailable rather than acknowledging uncertainty.
    if (party.writable() && party.saveHealthy()) party.focusEnd();
    focusBlocked = false;
    return;
  }
  Serial.printf("save: finishing an interrupted focus swap into slot %u\n",
                (unsigned)slot);
  if (!(wasEgg ? party.releaseAt(slot) : party.replaceAt(slot, outgoing))) return;
  const uint32_t gen = pet.petGeneration();
  pet.switchTo(incoming);
  if (pet.petGeneration() == gen) return;
  party.focusEnd();
  focusBlocked = false;
}

// ---------- the bag ----------

// How many pages the field bag needs. One even when empty, so the screen can
// say so instead of drawing nothing and reading as a crash.
uint8_t bagPages() {
  uint8_t n = bag.distinctCount();
  return n ? (uint8_t)((n + BAG_PER_PAGE - 1) / BAG_PER_PAGE) : 1;
}

// The battle bag shows 4 items a page -- the move grid's cells -- and holds up
// to 8 kinds (four balls, three potions, FULL HEAL). btlBagPage existed but
// nothing ever moved it, and the draw stopped after 4, so the 5th kind on was
// carried and could not be used in a fight. The rim pages it now.
uint8_t btlBagPages() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < bag.distinctCount(); i++)
    if (itemUsableInBattle(bag.keyAt(i))) n++;
  return n ? (uint8_t)((n + 3) / 4) : 1;
}

int uiBagRowCenterY(int i) { return BAG_ROW_Y(i) + 25; }

// A toast over the bag: what the last item did. Not a dialog -- using a vitamin
// is not a decision, and a confirmation for every tap would make training worse
// than the punching bag it competes with.
static char bagMsg[40] = "";
static uint32_t bagMsgUntil = 0;

static void bagSay(const char *fmt, const char *arg) {
  snprintf(bagMsg, sizeof(bagMsg), fmt, arg);
  bagMsgUntil = millis() + 2200;
}

// Applies a field item to the live pet. Returns false when the item was valid
// but had nothing to do -- a fully trained stat, say -- which the caller reports
// rather than silently consuming the item.
static bool bagUseOnPet(ItemKey k) {
  const ItemEntry &e = itemEntry(k);
  if (e.category != IC_TRAIN) return false;
  // The IV-bound ceiling applies here exactly as it does to the punching bag and
  // to a gym reward. An item that could push past trMaxFor() would make the IV
  // roll decorative, which is the one thing it must not be.
  uint8_t *cur;
  uint8_t cap;
  switch ((uint8_t)e.param) {
    case TRAIN_ATK: cur = &pet.trAtk; cap = pet.trMaxAtk(); break;
    case TRAIN_DEF: cur = &pet.trDef; cap = pet.trMaxDef(); break;
    default:        cur = &pet.trSpe; cap = pet.trMaxSpe(); break;
  }
  if (*cur >= cap) return false;
  uint16_t next = (uint16_t)*cur + e.amount;
  *cur = next > cap ? cap : (uint8_t)next;
  pet.saveNow();
  return true;
}

void renderBag() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(T(S_BAG)) * 6, 40);
  gfx->print(T(S_BAG));

  uint8_t n = bag.distinctCount();
  uint8_t pages = bagPages();
  if (bagPage >= pages) bagPage = 0;
  if (!n) {
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setCursor(CX - (int)strlen(T(S_BAG_EMPTY)) * 6, CY - 8);
    gfx->print(T(S_BAG_EMPTY));
  }
  for (uint8_t i = 0; i < BAG_PER_PAGE; i++) {
    uint8_t idx = (uint8_t)(bagPage * BAG_PER_PAGE + i);
    if (idx >= n) break;
    ItemKey k = bag.keyAt(idx);
    int y = BAG_ROW_Y(i);
    bool usable = itemUsableInField(k);
    gfx->fillRoundRect(76, y, 314, 50, 10, usable ? UI_WHITE : UI_TRACK);
    gfx->drawRoundRect(76, y, 314, 50, 10, UI_INK);
    gfx->setTextColor(usable ? UI_INK : 0x8410);
    gfx->setTextSize(2);
    gfx->setCursor(92, y + 17);
    gfx->print(itemEntry(k).name);
    char cnt[8];
    snprintf(cnt, sizeof(cnt), "x%u", bag.count(k));
    gfx->setCursor(374 - (int)strlen(cnt) * 12, y + 17);
    gfx->print(cnt);
  }
  // A page NUMBER, not a dot row: the catalogue is append-only and a dot per
  // page stopped fitting the round panel once the Pokedex reached 25 of them.
  if (pages > 1) {
    char pg[12];
    snprintf(pg, sizeof(pg), "%u/%u", bagPage + 1, pages);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(pg) * 6, 386);
    gfx->print(pg);
  }
  if (bagMsgUntil && millis() < bagMsgUntil) {
    gfx->fillRoundRect(66, CY - 26, 334, 52, 12, UI_WHITE);
    gfx->drawRoundRect(66, CY - 26, 334, 52, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - (int)strlen(bagMsg) * 6, CY - 8);
    gfx->print(bagMsg);
  }
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(T(S_BACK)) * 6, 414);
  gfx->print(T(S_BACK));
  uiChrome();
  gfx->flush();
}

void bagTap(int16_t x, int16_t y) {
  uint8_t n = bag.distinctCount();
  for (uint8_t i = 0; i < BAG_PER_PAGE; i++) {
    uint8_t idx = (uint8_t)(bagPage * BAG_PER_PAGE + i);
    if (idx >= n) break;
    int ry = BAG_ROW_Y(i);
    if (x < 76 || x > 390 || y < ry || y > ry + 50) continue;
    ItemKey k = bag.keyAt(idx);
    if (!itemUsableInField(k) || pet.isEgg()) { sfxPlay(SFX_DENY); return; }
    if (!bagUseOnPet(k)) { sfxPlay(SFX_DENY); bagSay("%s", T(S_ITEM_NOUSE)); return; }
    bag.consume(k);
    sfxPlay(SFX_TAP);
    bagSay(T(S_ITEM_USED), itemEntry(k).name);
    return;
  }
  if (y > 380) bagOpen = false;   // only the bottom BACK band, not a missed row (#24)
}

// ---------- wild encounters ----------

// Rolls a whole wild individual and opens the fight.
//
// The species, the level band, the tier and the shiny roll are all decided
// HERE and remembered, because a capture has to hand back the creature that was
// actually fought. Re-rolling any of it at capture time would mean the thing
// you caught was not the thing on screen.
bool startWildBattle(uint8_t region, bool hard) {
  if (pet.isEgg() || pet.ceremony != CER_NONE) return false;
  if (region >= REGION_COUNT || !regionAvailable(region)) return false;
  // The level FIRST, so the species can be one that exists at it: an evolved
  // form below the level it evolves at (a level 2 INFERNAPE) is not offered.
  uint8_t lo = wildLevelMin(pet.level(), hard);
  uint8_t hi = wildLevelMax(pet.level(), hard);
  const uint8_t lvl = (uint8_t)(lo + random(hi - lo + 1));
  uint8_t tier = wildTierForRoll((uint8_t)random(100));
  int16_t dex = wildPickSpecies(region, tier, (uint32_t)random(65536) * 31u + random(31), lvl);
  // A tier can be empty: only some packs installed, or -- for the evolved tier
  // below a region's first evolution level (7 in Kanto) -- nothing evolved can
  // exist yet. Fall back to a common one rather than refusing the encounter,
  // since "nothing happened" reads as a broken button. Rare and legendary
  // rolls are never moved: base forms exist at every level.
  if (!dex) dex = wildPickSpecies(region, R_COMUN, random(65536), lvl);
  if (!dex) return false;

  wildDex = dex;
  wildLvl = lvl;
  for (int i = 0; i < 4; i++) wildIv[i] = (uint8_t)(8 + random(24));
  // Bonus 0: nothing in this build earns one yet, so every wild shiny is the
  // flat 1/4096. The parameter stays because it is where a farewell reward
  // would land, and wild_test already pins the curve it would follow.
  wildShiny = wildShinyForRoll((uint32_t)random(WILD_SHINY_SCALE), 0);
  wildApplyShiny(wildShiny, wildIv[0], wildIv[1], wildIv[2], wildIv[3]);
  wildCaught = false;
  wildDropN = 0;

  buildSquad(0, TRAINER_TEAM_MAX, squadMask);
  if (!btlSquadN) return false;

  // Built through Pet like every other opponent, so it uses the same stat
  // formula and the same learnset-driven moveset the player's creature does.
  Pet foe;
  foe.dbgHatchAs(dex, wildShiny);
  foe.ivAtk = wildIv[0]; foe.ivDef = wildIv[1];
  foe.ivSpe = wildIv[2]; foe.ivHp = wildIv[3];
  foe.ageMinutes = (uint32_t)(wildLvl ? wildLvl - 1 : 0) * MINUTES_PER_LEVEL;
  foe.relearnFromLevel();
  combatantFromPet(btlFoe, foe);
  btlFoe.shiny = wildShiny;

  btlWild = true;
  btlLink = false;
  btlTrainer = -1;
  btlHard = hard;
  btlFoeAt = 0;
  btlFoeSquadN = 0;
  btlMsgCount = 0;
  btlOver = false;
  btlWon = false;
  btlMenu = 0;
  btlBagPage = 0;
  btlWinUntil = 0;
  btlSwapWho = -1;
  btlFaintUntil[0] = btlFaintUntil[1] = 0;
  btlEnterUntil[0] = btlEnterUntil[1] = 0;
  btlHpShown[0] = btlYou.maxHp;
  btlHpShown[1] = btlFoe.maxHp;
  btlSyncSprite(0, btlYou);
  btlSyncSprite(1, btlFoe);
  audioMusic(MUS_BATTLE);
  audioCry(btlFoe.dex);
  btlLungeUntil[0] = btlLungeUntil[1] = 0;
  btlHitUntil[0] = btlHitUntil[1] = 0;
  battleOpen = true;
  btlSay(T(S_WILD_MET), DEX_TBL[dex].name);
  return true;
}

// The wild creature as a storable record. It goes through the same PartyMon the
// party and box already hold, so a caught creature is a first-class member the
// moment it lands: it can be swapped in, raised, evolved and battled with.
PartyMon wildToPartyMon() {
  PartyMon m;
  m.dex = wildDex;
  m.level = wildLvl;
  m.ivAtk = wildIv[0]; m.ivDef = wildIv[1];
  m.ivSpe = wildIv[2]; m.ivHp = wildIv[3];
  m.shiny = wildShiny ? 1 : 0;
  for (int i = 0; i < MOVE_SLOTS; i++) m.moves[i] = btlFoe.moves[i];
  // A caught creature has real care state from the start rather than being a
  // v0 record, so switching to it does not reset a creature that was never
  // banked in the first place. Its age IS its level: nothing raised it.
  m.stateVersion = PARTYMON_STATE_VERSION;
  m.ageMinutes = (uint32_t)(wildLvl ? wildLvl - 1 : 0) * MINUTES_PER_LEVEL;
  m.realAgeMin = 0;            // its real age is counted from the capture
  m.lastLearnLevel = wildLvl;   // do not replay gates it already passed
  // Caught already at/past the active farewell level: nobody raised it there,
  // so its farewell -- and the blessed egg that comes with it -- is not on offer.
  if (wildLvl >= gFarewellLevel) m.berryKnown |= PM_FAREWELL;
  m.setOrigin(ORIGIN_WILD);     // a capture, and so is everything it evolves into
  return m;
}

// ---------- training submenu (5th icon) ----------

// Bars here show progress toward the IV-capped ceiling, not a raw stat: 100%
// means this individual cannot train the stat any higher, which is the whole
// point of trMaxFor() gating training by IV.
static uint8_t trainPct(uint8_t cur, uint8_t cap) {
  return cap ? (uint8_t)((uint16_t)cur * 100 / cap) : 0;
}

void renderTrain() {
  for (int y = 0; y < 466; y += 2)
    gfx->drawFastHLine(0, y, 466, gNight ? 0x0000 : 0x2104);

  gfx->fillRoundRect(TRAIN_X, TRAIN_Y, TRAIN_W, TRAIN_H, 18, UI_WHITE);
  gfx->drawRoundRect(TRAIN_X, TRAIN_Y, TRAIN_W, TRAIN_H, 18, UI_INK);

  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(T(S_TRAIN)) * 6, TRAIN_Y + 20);
  gfx->print(T(S_TRAIN));

  const char *lbl[TRAIN_ROWS] = { T(S_TR_ATK), T(S_TR_SPE), T(S_TR_DEF), T(S_MEMO) };
  uint8_t cur[TRAIN_ROWS] = { pet.trAtk, pet.trSpe, pet.trDef, 0 };
  uint8_t cap[TRAIN_ROWS] = { pet.trMaxAtk(), pet.trMaxSpe(), pet.trMaxDef(), 0 };

  for (int i = 0; i < TRAIN_ROWS; i++) {
    int y = TRAIN_ROW_Y(i);
    gfx->fillRoundRect(TRAIN_X + 18, y, TRAIN_W - 36, TRAIN_ROW_H, 12, UI_BG_DAY);
    gfx->drawRoundRect(TRAIN_X + 18, y, TRAIN_W - 36, TRAIN_ROW_H, 12, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(TRAIN_X + 32, y + 8);
    gfx->print(lbl[i]);

    // The MEMORY row has no bar, because it trains no stat -- it pays in joy and
    // bond. Drawing a progress bar for it would promise a ceiling to fill, and
    // there is none; the record goes there instead, which is what it does have.
    if (i == TRAIN_ROW_MEMO) {
      char r[22];
      snprintf(r, sizeof(r), T(S_REC_FMT), pet.quizHi);
      gfx->setTextColor(UI_INK_SOFT);
      gfx->setTextSize(1);
      gfx->setCursor(TRAIN_X + 32, y + 30);
      gfx->print(r);
      continue;
    }
    uint8_t pct = trainPct(cur[i], cap[i]);
    int bx = TRAIN_X + 32, bw = TRAIN_W - 64, bh = 10, by = y + 30;
    gfx->fillRoundRect(bx, by, bw, bh, 4, UI_TRACK);
    int fw = (bw - 4) * pct / 100;
    if (fw > 0)
      gfx->fillRoundRect(bx + 2, by + 2, fw, bh - 4, 3, pct >= 100 ? UI_BAR_OK : UI_BAR_WARN);
  }

  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(T(S_TR_DEF_HINT)) * 3, TRAIN_Y + TRAIN_H - 22);
  gfx->print(T(S_TR_DEF_HINT));
  gfx->flush();   // without this the panel never updates and the screen freezes
}

// ---------- the box ----------
// Storage past the six that fight. A creature is moved by picking a party slot
// and then a box slot, which swaps them -- so one gesture covers deposit,
// withdraw and exchange rather than needing three.

// Should box slot `a` be drawn AFTER slot `b` under the current order?
//
// Empty slots always last, whatever the key: a grid with holes punched through it
// is harder to read than a full one, and the empty cells are only there to be
// swap targets anyway.
static bool boxSortAfter(uint16_t a, uint16_t b) {
  const PartyMon &ma = party.box[a], &mb = party.box[b];
  if (ma.empty() != mb.empty()) return ma.empty();
  if (ma.empty()) return false;                  // both empty: keep slot order
  if (boxSort == BSORT_DEX) {
    if (ma.dex != mb.dex) return ma.dex > mb.dex;
  } else if (boxSort == BSORT_LEVEL) {
    if (ma.level != mb.level) return ma.level < mb.level;   // strongest first
  }
  return false;      // equal keys: leave them in slot order, so this is STABLE
}

// Which REAL box slot a visual position shows.
//
// A VIEW PERMUTATION, NEVER A DATA ONE, and that is not a style preference.
// party.box[] is half of a CRC-checked checkpoint written as one record with the
// party; reordering the array would rewrite the largest blob in the store every
// time somebody glanced at the box, and boxDetail/boxSel/boxSwapFrom all carry
// REAL indices into it, so a sort landing mid-gesture would retarget a pending
// swap onto a different creature. Cache the sorted LOOK until a committed
// mutation or load changes the collection revision, not once per drawn cell.
//
// box_test asserts this is a BIJECTION for every mode, because the one bug class
// that matters here is two visual cells resolving to the same real slot -- which
// is how a creature gets overwritten.
uint16_t boxSlotFor(uint16_t visual) {
  if (visual >= BOX_SLOTS) return 0;
  if (boxSort == BSORT_SLOT || !party.box.data()) return visual;
  static uint16_t order[BOX_SLOTS];
  static uint32_t revision = 0;
  static uint8_t mode = BSORT_COUNT;
  if (mode != boxSort || revision != party.revision()) {
    for (uint16_t i = 0; i < BOX_SLOTS; i++) order[i] = i;
    std::sort(order, order + BOX_SLOTS, [](uint16_t a, uint16_t b) {
      if (boxSortAfter(a, b)) return false;
      if (boxSortAfter(b, a)) return true;
      return a < b;  // deterministic slot order for equal keys, including empties
    });
    mode = boxSort;
    revision = party.revision();
  }
  return order[visual];
}

const char *boxSortName() {
  switch (boxSort) {
    case BSORT_DEX:   return T(S_SORT_DEX);
    case BSORT_LEVEL: return T(S_SORT_LVL);
    default: return T(S_SORT_SLOT);
  }
}

bool boxHubHold(int16_t x, int16_t y) {
  if (uiCurrentScreen() != SCR_BOX || boxDetail || boxJumpOpen ||
      movePickOpen || releaseConfirm || !partyHubAt(x, y)) return false;
  boxJumpOpen = true;
  sfxPlay(SFX_TAP);
  return true;
}

void boxJumpRect(int dir, int *x, int *y, int *w, int *h) {
  if (x) *x = CX - 110;
  if (y) *y = dir < 0 ? 130 : 242;
  if (w) *w = 220;
  if (h) *h = UI_TAP_FINGER;
}

void renderBox() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  char head[32];
  snprintf(head, sizeof(head), T(S_BOX_FMT), party.boxCount(), BOX_SLOTS);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(head) * 6, 40);
  gfx->print(head);
  drawSaveWarning();
  if (boxJumpOpen) {
    char pg[12];
    snprintf(pg, sizeof(pg), "%u/%u", (unsigned)(boxPage + 1),
             (unsigned)(BOX_SLOTS / BOX_PER_PAGE));
    gfx->setCursor(CX - (int)strlen(pg) * 6, 86);
    gfx->print(pg);
    for (int dir = -1; dir <= 1; dir += 2) {
      int x, y, w, h;
      boxJumpRect(dir, &x, &y, &w, &h);
      gfx->fillRoundRect(x, y, w, h, 16, UI_TRACK);
      gfx->setCursor(CX - 18, y + h / 2 - 8);
      gfx->print(dir < 0 ? "-10" : "+10");
    }
    gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 392);
    gfx->print(T(S_BACK));
    gfx->flush();
    return;
  }
  if (boxSwapFrom) {
    const PartyMon &p = party.slots[boxSwapFrom - 1];
    char sub[40];
    snprintf(sub, sizeof(sub), T(S_BOX_SWAP),
             p.empty() ? "-" : (p.nick[0] ? p.nick : DEX_TBL[p.dex].name));
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)strlen(sub) * 3, PSLOT_PROMPT_Y);
    gfx->print(sub);
  }
  for (uint8_t i = 0; party.box.data() && i < BOX_PER_PAGE; i++) {
    uint16_t visual = (uint16_t)boxPage * BOX_PER_PAGE + i;
    if (visual >= BOX_SLOTS) break;
    drawPartySlot(i, party.box[boxSlotFor(visual)]);
  }
  {
    // The hub carries the page AND the order, and tapping it cycles the order --
    // see boxTap(). It is the easiest place to hit on a round panel, and it used
    // to answer a tap by closing the box, which is what BACK and a swipe already
    // do twice over.
    char pg[12];
    snprintf(pg, sizeof(pg), "%u/%u", (unsigned)(boxPage + 1),
             (unsigned)(BOX_SLOTS / BOX_PER_PAGE));
    drawPartyHub(pg, boxSortName(), boxSwapFrom != 0);
  }
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(1);
  gfx->setCursor(CX - 27, 375);
  gfx->print("1s: +/-10");
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 402);
  gfx->print(T(S_BACK));
  uiChrome();
  gfx->flush();
}

void boxTap(int16_t x, int16_t y) {
  if (!party.box.data()) { boxOpen = false; boxDetail = 0; sfxPlay(SFX_DENY); return; }
  if (boxJumpOpen) {
    for (int dir = -1; dir <= 1; dir += 2) {
      int bx, by, w, h;
      boxJumpRect(dir, &bx, &by, &w, &h);
      if (x < bx || x >= bx + w || y < by || y >= by + h) continue;
      int page = (int)boxPage + dir * 10;
      const int last = BOX_SLOTS / BOX_PER_PAGE - 1;
      boxPage = (uint8_t)(page < 0 ? 0 : page > last ? last : page);
      sfxPlay(SFX_TAP);
      return;
    }
    boxJumpOpen = false;
    return;
  }
  // The sheet is checked first and is modal in the same way the party's is.
  if (boxDetail) {
    if (releaseConfirm) { monSheetConfirmTap(x, y, true); return; }
    if (monSheetBtn(x, y, true)) {          // TO PARTY
      int free = party.firstFree();
      if (free < 0) {
        boxSel = boxDetail;
        boxDetail = 0;
        boxOpen = false;
        sfxPlay(SFX_TAP);
        return;
      }
      if (!party.swapPartyBox((uint8_t)free, boxDetail - 1)) { sfxPlay(SFX_DENY); return; }
      boxDetail = 0;
      boxSel = 0;
      sfxPlay(SFX_MEDAL);
      return;
    }
    if (monSheetBtn(x, y, false)) {         // RELEASE -- ask first, always
      releaseConfirm = true;
      sfxPlay(SFX_TAP);
      return;
    }
    if (y > 380) {                          // only the bottom BACK band (#24)
      boxDetail = 0;
      releaseConfirm = false;
      sfxPlay(SFX_TAP);
    }
    return;
  }
  // The hub cycles the order. Checked BEFORE the slots: the hub sits in the
  // middle of the ring and does not overlap them, but ordering it first keeps the
  // rule "the thing drawn on top answers first" true here as everywhere else.
  if (partyHubAt(x, y)) {
    boxSort = (uint8_t)((boxSort + 1) % BSORT_COUNT);
    // A pending swap is armed against a REAL slot, so re-ordering the view does
    // not invalidate it -- boxSlotFor() is a permutation of the same slots. The
    // open sheet is closed anyway, because it was opened from a visual position
    // the player can no longer see.
    boxDetail = 0;
    releaseConfirm = false;
    sfxPlay(SFX_TAP);
    return;
  }
  for (uint8_t i = 0; i < BOX_PER_PAGE; i++) {
    uint16_t visual = (uint16_t)boxPage * BOX_PER_PAGE + i;
    if (visual >= BOX_SLOTS) break;
    uint16_t idx = boxSlotFor(visual);
    if (partySlotAt(x, y) != (int)i) continue;
    if (boxSwapFrom) {           // a party slot is waiting: complete the trade
      if (party.slots[boxSwapFrom - 1].empty() && party.box[idx].empty()) {
        sfxPlay(SFX_DENY);
        return;
      }
      if (!party.swapPartyBox(boxSwapFrom - 1, idx)) { sfxPlay(SFX_DENY); return; }
      boxSwapFrom = 0;
      boxSel = 0;
      sfxPlay(SFX_MEDAL);
      return;
    }
    // Otherwise open its SHEET. It used to go straight to the party, which is a
    // lot to happen from one tap and left nowhere to put RELEASE; the sheet
    // offers TO PARTY explicitly and shows what you are about to move.
    if (party.box[idx].empty()) { sfxPlay(SFX_DENY); return; }
    boxDetail = idx + 1;
    releaseConfirm = false;
    sfxPlay(SFX_TAP);
    return;
  }
  // Only the bottom BACK band closes: a slightly missed circle used to close
  // the box and lose your page (issue #24). Swipe down also goes back.
  if (y > 380) {
    boxOpen = false;
    boxSwapFrom = 0;
    boxDetail = 0;
    releaseConfirm = false;
  }
}

// ---------- party ----------

void drawPartySlot(int i, const PartyMon &m) {
  int cx, cy;
  partySlotPos(i, &cx, &cy);
  gfx->fillCircle(cx, cy, PSLOT_R, m.empty() ? UI_TRACK : UI_WHITE);
  gfx->drawCircle(cx, cy, PSLOT_R, UI_INK);
  gfx->drawCircle(cx, cy, PSLOT_R - 1, UI_INK);
  if (m.empty()) {
    gfx->setTextColor(0x8410);
    gfx->setTextSize(2);
    gfx->setCursor(cx - (int)strlen(T(S_PARTY_EMPTY)) * 6, cy - 8);
    gfx->print(T(S_PARTY_EMPTY));
    return;
  }
  const uint8_t *th = thumbs.get(m.dex);
  if (th) drawThumb(th, cx - 24, cy - 44, 1, false);
  const DexEntry &d = DEX_TBL[m.dex];
  const char *nm = m.nick[0] ? m.nick : d.name;
  gfx->setTextColor(d.accent);
  gfx->setTextSize(1);
  gfx->setCursor(cx - (int)strlen(nm) * 3, cy + 8);
  gfx->print(nm);
  if (m.shiny) {
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setCursor(cx + (int)strlen(nm) * 3 + 3, cy + 8);
    gfx->print("*");
  }
  char lv[12];
  snprintf(lv, sizeof(lv), T(S_LVL_FMT), (unsigned)m.level);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(cx - (int)strlen(lv) * 6, cy + 20);
  gfx->print(lv);
}

// The hub in the middle of the ring. On the party it is the BOX button; in the
// box it is the page. Either way the centre of a round panel is the easiest
// place on it to hit, so the primary action belongs there.
void drawPartyHub(const char *top, const char *bot, bool armed) {
  gfx->fillCircle(CX, PSLOT_CY, PSLOT_HUB_R, armed ? UI_BAR_WARN : UI_BG_DAY);
  gfx->drawCircle(CX, PSLOT_CY, PSLOT_HUB_R, UI_INK);
  gfx->drawCircle(CX, PSLOT_CY, PSLOT_HUB_R - 1, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(top) * 6, PSLOT_CY - 14);
  gfx->print(top);
  gfx->setTextSize(1);
  gfx->setCursor(CX - (int)strlen(bot) * 3, PSLOT_CY + 10);
  gfx->print(bot);
}

void renderParty() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);

  char head[24];
  snprintf(head, sizeof(head), T(S_PARTY_FMT), party.count());
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - (int)strlen(head) * 9, 42);
  gfx->print(head);

  // the box lives behind the hub in the middle of the ring; it also shows how
  // full it is, so the player knows there is anything in there without opening
  if (!partyPick) {
    char bl[24];
    snprintf(bl, sizeof(bl), "%u/%u", party.boxCount(), BOX_SLOTS);
    drawPartyHub(bl, T(S_BOX_BTN), boxSwapFrom != 0);
  }

  if (boxSel && party.box.data() && boxSel <= BOX_SLOTS) {
    const PartyMon &b = party.box[boxSel - 1];
    char sw[44];
    snprintf(sw, sizeof(sw), T(S_BOX_SWAP),
             b.nick[0] ? b.nick : DEX_TBL[b.dex].name);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)strlen(sw) * 3, PSLOT_PROMPT_Y);
    gfx->print(sw);
  }

  // when a newcomer is waiting, say so instead of the usual hint. The urgency
  // is carried by the red CLOSE button below, not by tinting the sentence:
  // UI_BAR_BAD as small text on cream is 3.2:1.
  if (partyPick) {
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(CX - (int)strlen(T(S_PARTY_FULL)) * 3, PSLOT_PROMPT_Y);
    gfx->print(T(S_PARTY_FULL));
  }

  for (int i = 0; i < PARTY_SLOTS; i++) drawPartySlot(i, party.slots[i]);

  // exit: an explicit button, always in the same place
  const char *ex = partyPick ? T(S_PARTY_LETGO) : T(S_CLOSE);
  gfx->fillRoundRect(PARTYCLOSE_X, PARTYCLOSE_Y, PARTYCLOSE_W, PARTYCLOSE_H, 12,
                     partyPick ? UI_BAR_BAD : UI_TRACK);
  gfx->drawRoundRect(PARTYCLOSE_X, PARTYCLOSE_Y, PARTYCLOSE_W, PARTYCLOSE_H, 12, UI_INK);
  gfx->setTextColor(partyPick ? UI_WHITE : UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(ex) * 6, PARTYCLOSE_Y + 14);
  gfx->print(ex);
  uiChrome();
  gfx->flush();
}

// ---------- teclado para renombrar ----------

static const char KB_KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-";  // 28 + DEL + OK = 30
#define KB_COLS 6
#define KB_X 40
#define KB_Y 150
#define KB_W 64
#define KB_H 52

// The keyboard is shared, so it has to be told what it is naming. It used to
// hardcode pet.rename() on commit, which is why a second caller needed this.
void openKeyboardFor(uint8_t target) {
  kbTarget = target;
  kbOpen = true;
  const char *cur = (target == KB_TRAINER) ? pet.trainerName : pet.nick;
  strncpy(nameBuf, cur, sizeof(nameBuf) - 1);
  nameBuf[sizeof(nameBuf) - 1] = 0;
  nameLen = strlen(nameBuf);
}
void openKeyboard() { openKeyboardFor(KB_PET); }

void renderKeyboard() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_NAME)) * 6, 56);
  gfx->print(T(S_NAME));
  // buffer actual
  gfx->fillRoundRect(83, 84, 300, 40, 8, UI_WHITE);
  gfx->drawRoundRect(83, 84, 300, 40, 8, UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(95, 94);
  gfx->print(nameLen ? nameBuf : "_");

  for (int i = 0; i < 30; i++) {
    int x = KB_X + (i % KB_COLS) * KB_W, y = KB_Y + (i / KB_COLS) * KB_H;
    bool special = (i >= 28);
    gfx->fillRoundRect(x, y, KB_W - 6, KB_H - 6, 6, special ? UI_BAR_WARN : UI_WHITE);
    gfx->drawRoundRect(x, y, KB_W - 6, KB_H - 6, 6, UI_INK);
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    if (i < 28) {
      gfx->setCursor(x + KB_W / 2 - 9, y + KB_H / 2 - 10);
      gfx->print(KB_KEYS[i]);
    } else {
      const char *lab = (i == 28) ? "<-" : "OK";
      gfx->setCursor(x + KB_W / 2 - 15, y + KB_H / 2 - 10);
      gfx->print(lab);
    }
  }
  gfx->flush();
}

void keyboardTap(int16_t x, int16_t y) {
  int col = (x - KB_X) / KB_W, row = (y - KB_Y) / KB_H;
  if (col < 0 || col >= KB_COLS || row < 0 || row >= 5) return;
  int i = row * KB_COLS + col;
  if (i >= 30) return;
  if (i == 28) {  // borrar
    if (nameLen) nameBuf[--nameLen] = 0;
  } else if (i == 29) {  // OK
    if (kbTarget == KB_TRAINER) pet.renameTrainer(nameBuf);
    else pet.rename(nameBuf);
    kbOpen = false;
  } else if (nameLen < sizeof(nameBuf) - 1) {
    nameBuf[nameLen++] = KB_KEYS[i];
    nameBuf[nameLen] = 0;
  }
}

// ---------- galeria pokedex ----------

#define GAL_X 73
#define GAL_Y 84
#define GAL_CELL 80

// dibuja una miniatura centrada en su celda; sil=true la pinta en tinta
void drawThumb(const uint8_t *b, int x, int y, int s, bool sil) {
  uint8_t w = b[0], h = b[1], n = b[2];
  const uint8_t *pal = b + 3;
  const uint8_t *d = pal + n * 2;
  int ox = x + (GAL_CELL - w * s) / 2;
  int oy = y + (GAL_CELL - h * s) / 2;
  for (int r = 0; r < h; r++) {
    const uint8_t *row = d + r * w;
    forRuns(w, [&](int c) { return indexedKey(row[c], sil); },
            [&](int c0, int len, int k) {
              uint16_t col = sil ? INK_K : (uint16_t)(pal[k * 2] | (pal[k * 2 + 1] << 8));
              gfx->fillRect(ox + c0 * s, oy + r * s, len * s, s, col);
            });
  }
}

// ---------------------------------------------------------------------------
// The filter, as four small functions. THE draw path and THE tap path both go
// through galleryAt(), which is the whole point: with a filter on, the grid is no
// longer contiguous, so the cell arithmetic that used to be repeated in both
// places would put a tap on a different creature than the one drawn.

// ALL, HATCHED and CAUGHT draw the whole region classic-style (members in colour,
// missing entries as silhouettes) so gaps in the hatched/caught dex are visible.
// SHINY stays compact so shinies don't hide across 65 pages of silhouettes.
bool galleryPass(int16_t dex) {
  if (galleryFilter == GFILT_SHINY) return pet.inDex(DEXV_COMPLETE, true, dex);
  (void)dex;
  return true;
}

uint16_t galleryCount() {
  uint16_t n = 0;
  for (int16_t d = GAL_LO; d <= GAL_HI && d <= DEX_COUNT; d++)
    if (galleryPass(d)) n++;
  return n;
}

// The dex number at a visual grid position, or 0 past the end.
int16_t galleryAt(uint16_t visual) {
  uint16_t n = 0;
  for (int16_t d = GAL_LO; d <= GAL_HI && d <= DEX_COUNT; d++) {
    if (!galleryPass(d)) continue;
    if (n == visual) return d;
    n++;
  }
  return 0;
}

// At least one page even when the filter matches nothing, so the screen can SAY
// so rather than drawing an empty grid that reads as a crash.
uint8_t galleryPages() {
  const uint16_t n = galleryCount();
  const uint8_t p = (uint8_t)((n + GAL_PER_PAGE - 1) / GAL_PER_PAGE);
  return p ? p : 1;
}

static const StrId GAL_VIEW_LABEL[GAL_VIEWS] = {
  S_FILT_ALL, S_FILT_HATCHED, S_FILT_CAUGHT, S_FILT_SHINY
};

const char *galleryFilterName() {
  return T(GAL_VIEW_LABEL[galleryFilter % GAL_VIEWS]);
}

static int galleryViewW(uint8_t i) {
  const int w = (int)strlen(T(GAL_VIEW_LABEL[i % GAL_VIEWS])) * 6 + 10;
  return w < 56 ? 56 : w;
}

void galleryViewRect(uint8_t i, int *x, int *w) {
  int total = (GAL_VIEWS - 1) * GAL_VBTN_GAP;
  for (uint8_t k = 0; k < GAL_VIEWS; k++) total += galleryViewW(k);
  int at = CX - total / 2;
  for (uint8_t k = 0; k < i; k++) at += galleryViewW(k) + GAL_VBTN_GAP;
  *x = at; *w = galleryViewW(i);
}

int8_t galleryViewAt(int16_t x, int16_t y) {
  if (y < GAL_VBTN_Y - 2 || y > GAL_VBTN_Y + GAL_VBTN_H + 1) return -1;
  for (uint8_t i = 0; i < GAL_VIEWS; i++) {
    int bx, bw; galleryViewRect(i, &bx, &bw);
    if (x >= bx - 2 && x < bx + bw + 2) return (int8_t)i;
  }
  return -1;
}

// Cycling the filter can leave the page past the end of a shorter result, and a
// page number beyond the count draws nothing at all. One place fixes it, called
// by everything that changes either the filter or the region.
void galleryClampPage() {
  const uint8_t n = galleryPages();
  if (galleryPage >= n) galleryPage = n - 1;
}

void renderGallery() {
  if (galleryDetail) {  // vista detalle: se redibuja siempre (animada)
    gfx->fillScreen(RGB565_BLACK);
    gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
    const DexEntry &d = DEX_TBL[galleryDetail];
    bool reg = galleryMember(galleryDetail);
    bool sh = reg && pet.inDex(gvWhich(), true, galleryDetail);
    char head[24];
    snprintf(head, sizeof(head), "N.%03d %s%s", galleryDetail,
             sh ? "*" : "", reg ? d.name : "???");
    gfx->setTextColor(reg ? d.accent : UI_INK);
    int glen = strlen(head);
    int gts = (glen <= 13) ? 3 : 2;  // auto-encoge nombres largos (no caben a t3)
    gfx->setTextSize(gts);
    gfx->setCursor(CX - glen * (gts == 3 ? 9 : 6), gts == 3 ? 56 : 60);
    gfx->print(head);
    if (galleryPmd.loaded) {
      // animado y a color si esta registrado; silueta estatica si no (estilo "?")
      drawPmdActM(galleryPmd, PMD_IDLE, CX, 300, reg ? millis() : 0, true, !reg, 6);
    } else {
      const uint8_t *t = thumbs.get(galleryDetail);
      if (t) drawThumb(t, CX - GAL_CELL, 135, 4, !reg);
    }
    gfx->setTextColor(UI_INK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(T(S_DETAIL_BACK)) * 6, 408);
    gfx->print(T(S_DETAIL_BACK));
    uiChrome();
    gfx->flush();
    return;
  }

  if (!galleryDirty) return;  // la rejilla es estatica
  galleryDirty = false;

  gfx->fillScreen(RGB565_BLACK);
  gfx->fillCircle(CX, CY, 231, UI_BG_DAY);
  // the region's own name and its own tally for the active Pokedex view
  char head[32];
  const RegionInfo &grg = REGIONS[galleryRegion % GAL_REGIONS];
  snprintf(head, sizeof(head), "%s %u/%u", grg.name,
           pet.dexCountIn(gvWhich(), gvShiny(), grg.lo, grg.hi), (unsigned)GAL_SPAN);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(head) * 9, 36);
  gfx->print(head);
  // Four view buttons under the tally: ALL / HATCHED / CAUGHT / SHINY.
  // The chosen one is filled so a view left on is unmistakable.
  for (uint8_t i = 0; i < GAL_VIEWS; i++) {
    int bx, bw; galleryViewRect(i, &bx, &bw);
    const bool on = galleryFilter == i;
    gfx->fillRoundRect(bx, GAL_VBTN_Y, bw, GAL_VBTN_H, 6, on ? UI_INK : UI_WHITE);
    gfx->drawRoundRect(bx, GAL_VBTN_Y, bw, GAL_VBTN_H, 6, UI_INK);
    const char *l = T(GAL_VIEW_LABEL[i]);
    gfx->setTextColor(on ? UI_WHITE : UI_INK);
    gfx->setTextSize(1);
    gfx->setCursor(bx + bw / 2 - (int)strlen(l) * 3, GAL_VBTN_Y + GAL_VBTN_H / 2 - 4);
    gfx->print(l);
  }

  if (galleryCount() == 0) {
    // SAY SO. An empty grid is indistinguishable from a crash, and the filter
    // that caused it is one tap away.
    gfx->setTextColor(UI_INK_SOFT);
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(T(S_FILT_NONE)) * 6, 220);
    gfx->print(T(S_FILT_NONE));
  }
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      const uint16_t visual = (uint16_t)galleryPage * GAL_PER_PAGE + r * 4 + c;
      const int16_t dex = galleryAt(visual);
      if (!dex) break;                 // past the end of the filtered list
      int x = GAL_X + c * GAL_CELL, y = GAL_Y + r * GAL_CELL;
      const uint8_t *t = thumbs.get(dex);
      if (t) {
        const bool member = galleryMember(dex);
        drawThumb(t, x, y, 2, !member);
        if (member && pet.inDex(gvWhich(), true, dex)) {
          gfx->setTextColor(UI_BAR_WARN);
          gfx->setTextSize(2);
          gfx->setCursor(x + 62, y + 4);
          gfx->print("*");
        }
      } else {
        char num[6];
        snprintf(num, sizeof(num), "%d", dex);
        gfx->setTextColor(UI_INK_SOFT);
        gfx->setTextSize(2);
        gfx->setCursor(x + 24, y + 32);
        gfx->print(num);
      }
    }
  }
  // A page number, not a row of dots. 25 dots do not fit across the bottom of
  // a round panel -- the chord at that height is only ~228 px -- and counting
  // them to find where you are is worse than reading the number.
  char pg[12];
  snprintf(pg, sizeof(pg), "%d/%d", galleryPage + 1, (int)galleryPages());
  gfx->setTextColor(UI_INK_SOFT);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(pg) * 6, 410);
  gfx->print(pg);
  uiChrome();
  gfx->flush();
}

void galleryTap(int16_t x, int16_t y) {
  if (galleryDetail) {  // volver a la rejilla
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    return;
  }
  // The view buttons, BEFORE the exit band: the title above them still exits.
  const int8_t v = galleryViewAt(x, y);
  if (v >= 0) {
    galleryFilter = (uint8_t)v;
    galleryClampPage();
    galleryDirty = true;
    sfxPlay(SFX_TAP);
    return;
  }
  // Dead space does nothing. The title used to exit on tap, which a thumb aiming
  // for the view buttons below it hit by accident (issue #24). Swipe down goes back.
  if (y < GAL_Y) return;
  int c = (x - GAL_X) / GAL_CELL, r = (y - GAL_Y) / GAL_CELL;
  if (c < 0 || c > 3 || r < 0 || r > 3) return;
  // The SAME function the grid was drawn from, so a tap cannot land on a
  // different creature than the one under the finger.
  const int16_t dex = galleryAt((uint16_t)galleryPage * GAL_PER_PAGE + r * 4 + c);
  if (!dex) return;
  galleryDetail = dex;
  const bool member = galleryMember(dex);
  galleryPmd.load(dex, member && pet.inDex(gvWhich(), true, dex));
  // Only for an entry the player has actually filled in. Playing the cry of a
  // silhouette would hand over the one thing the "???" is withholding.
  if (member) audioCry(dex);
}

void drawBattery() {
  int pc = batPercent();
  if (pc < 0) return;  // sin bateria conectada
  int x = CX - 14, y = 12, w = 24, h = 11;
  bool charging = batCharging();
  uint16_t col = charging ? UI_BAR_OK
                 : (pc >= 40) ? inkColor()
                 : (pc >= 15) ? UI_BAR_WARN
                              : UI_BAR_BAD;
  gfx->drawRoundRect(x, y, w, h, 2, col);
  gfx->fillRect(x + w, y + 3, 3, 5, col);  // borne
  if (charging) {
    // rayo de carga (zigzag) en vez de la barra de nivel
    uint16_t bolt = C565(0xff, 0xd9, 0x4a);
    int bx = x + w / 2;
    gfx->fillTriangle(bx + 3, y + 1, bx - 4, y + 6, bx + 1, y + 6, bolt);
    gfx->fillTriangle(bx - 1, y + 5, bx + 4, y + 5, bx - 3, y + 10, bolt);
  } else {
    int fw = (w - 4) * pc / 100;
    if (fw > 0) gfx->fillRect(x + 2, y + 2, fw, h - 4, col);
  }
}

// Drawn beside the battery when Pet::saveHealthy() goes false (NVS would not
// open, or it has refused three writes in a row) OR when gNvsLow is set (the
// partition itself is close to full).
//
// The low-space case matters just as much: a full partition is erased WHOLE by
// the Arduino core on the NEXT BOOT (see nvsinfo.cpp), so a save can still be
// succeeding right up until a restart wipes every badge and every save on the
// device. `nvsLowOnSpace()` existed only in the serial HEALTH log before this,
// which is exactly how a player found out: by getting wiped, with no earlier
// sign on the one screen they were actually looking at.
//
// Deliberately not a dialog. The condition persists, so a modal would be
// dismissed once and then forgotten, and it is not actionable in the moment --
// what the player needs is to know before they put in another week, so they can
// EXPORT. The firmware could not say this at all before: the only symptom was a
// line on a serial port no player has attached.
void drawSaveWarning() {
  if (pet.saveHealthy() && party.saveHealthy() && !gNvsLow && !focusBlocked) return;
  const int x = CX - 40, y = 11, w = 13, h = 13;
  gfx->fillTriangle(x + w / 2, y, x, y + h, x + w, y + h, UI_BAR_BAD);
  gfx->fillRect(x + w / 2, y + 5, 2, 4, RGB565_BLACK);   // the "!"
  gfx->fillRect(x + w / 2, y + 10, 2, 2, RGB565_BLACK);
}

void drawHeader(const char *name, uint16_t nameColor, const char *msg) {
  drawBattery();
  drawSaveWarning();
  gfx->setTextColor(nameColor);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(name) * 9, HEADER_NAME_Y);
  gfx->print(name);
  gfx->setTextColor(inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(msg) * 6, 90);
  gfx->print(msg);
}

// animacion de la ceremonia (10s): despedida = reverencia con corazones y se
// aleja caminando; escapada = se asusta y sale corriendo. Sustituye al idle.
void drawCeremony() {
  if (!pmd.loaded) { drawPet(); return; }  // respaldo si no hay sprite PMD
  uint32_t now = millis();
  float t = pet.ceremonyT();               // 0..1 a lo largo de los 10s
  bool panic = (pet.ceremony == CER_RUNAWAY);
  int x = CX, y = PET_GROUND;
  uint8_t act = PMD_IDLE;

  if (panic) {
    // final triste: penumbra azulada + lluvia
    for (int i = 0; i < 46; i++) {
      int rx = (i * 47 + now / 3) % 466;
      int ry = (i * 91 + now / 2) % 470;
      gfx->drawLine(rx, ry, rx - 3, ry + 12, C565(0x6a, 0x84, 0xb0));
    }
    bool fade = false;
    if (t < 0.30f) {                       // cabizbajo, temblando
      act = pmd.has(PMD_HURT) ? PMD_HURT : PMD_IDLE;
      x = CX + (int)(4 * sinf(now * 0.04f));
    } else {                               // se aleja despacio y se desvanece
      act = pmd.has(PMD_WALKL) ? PMD_WALKL : PMD_IDLE;
      x = CX - (int)(((t - 0.30f) / 0.70f) * (CX + 120));
      fade = (t > 0.6f) && ((now / 160) % 2 == 0);  // parpadea hacia la silueta
    }
    drawPmdAct(act, x, y, now, true, fade, 5);  // fade=silueta: se difumina al irse
    // lagrima cayendo del bicho
    if (t < 0.55f) {
      int ty = y - 150 + (int)((now / 6) % 40);
      gfx->fillRect(x + 6, ty, 3, 6, C565(0x9a, 0xc4, 0xe8));
    }
    return;
  }

  // despedida epica: halo dorado pulsante + chispas y corazones que ascienden
  int gcy = PET_GROUND - 96;
  for (int k = 0; k < 4; k++) {
    int r = 60 + k * 34 + (int)(10 * sinf(now * 0.02f));
    gfx->drawCircle(CX, gcy, r, C565(0xff, 0xdf, 0x8a));
  }
  for (int i = 0; i < 16; i++) {
    int px = (i * 71 + 28) % 466;
    int py = 410 - (int)((now / 8 + i * 70) % 360);   // suben y reaparecen abajo
    if (py < 30) continue;
    if (i % 4 == 0) drawMap(SPR_HEART, 32, px - 8, py - 8, 1, false);  // corazoncito
    else gfx->fillRect(px, py, 4, 4, (i % 2) ? C565(0xff, 0xe7, 0x9f) : C565(0xff, 0x9a, 0xc0));
  }

  if (t < 0.45f) {                         // reverencia / pose de despedida
    act = pmd.has(PMD_POSE) ? PMD_POSE : (pmd.has(PMD_NOD) ? PMD_NOD : PMD_IDLE);
  } else {                                 // se aleja por la derecha
    act = pmd.has(PMD_WALKR) ? PMD_WALKR : PMD_IDLE;
    x = CX + (int)(((t - 0.45f) / 0.55f) * (CX + 140));
  }
  drawPmdAct(act, x, y, now, true, false, 5);
  if (pet.showHeart())                     // corazon grande siguiendo al bicho
    drawMap(SPR_HEART, 32, x + 50, y - 190, 2, false);
}

// dialogo de decision (2 botones apilados): evolucionar/mantener o despedirse/quedaros
// The two confirm buttons' hit areas, so a test can hold them to UI_TAP_MIN and
// prove they do not overlap without restating the numbers.
void uiConfirmRects(int *b1Top, int *b1Bot, int *b2Top, int *b2Bot) {
  *b1Top = CONFIRM_B1_Y;
  *b1Bot = CONFIRM_B1_Y + CONFIRM_BTN_H;
  *b2Top = CONFIRM_B2_Y;
  *b2Bot = CONFIRM_B2_Y + CONFIRM_BTN_H;
}

// Draws the panel and its two buttons. sub1/sub2 are optional lines between the
// question and the buttons -- what the choice COSTS, which for anything
// irreversible has to be on screen before the tap, not after it.
void drawConfirmPanel(const char *q, const char *sub1, const char *sub2,
                      uint16_t subCol, const char *o1, uint16_t c1, uint16_t t1,
                      const char *o2, uint16_t c2, uint16_t t2) {
  gfx->fillRoundRect(CONFIRM_X, CONFIRM_Y, CONFIRM_W, CONFIRM_H, 16, UI_WHITE);
  gfx->drawRoundRect(CONFIRM_X, CONFIRM_Y, CONFIRM_W, CONFIRM_H, 16, UI_INK);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(q) * 6, 176);
  gfx->print(q);
  if (sub1 || sub2) {
    gfx->setTextSize(1);
    gfx->setTextColor(subCol);
    if (sub1) {
      gfx->setCursor(CX - (int)strlen(sub1) * 3, sub2 ? 188 : 194);
      gfx->print(sub1);
    }
    if (sub2) {
      gfx->setCursor(CX - (int)strlen(sub2) * 3, 197);
      gfx->print(sub2);
    }
    gfx->setTextSize(2);
    gfx->setTextColor(UI_INK);
  }
  gfx->fillRoundRect(CONFIRM_BTN_X, CONFIRM_B1_Y, CONFIRM_BTN_W, CONFIRM_BTN_H, 12, c1);
  gfx->setTextColor(t1);
  gfx->setCursor(CX - (int)strlen(o1) * 6, CONFIRM_B1_Y + 18);
  gfx->print(o1);
  gfx->fillRoundRect(CONFIRM_BTN_X, CONFIRM_B2_Y, CONFIRM_BTN_W, CONFIRM_BTN_H, 12, c2);
  gfx->setTextColor(t2);
  gfx->setCursor(CX - (int)strlen(o2) * 6, CONFIRM_B2_Y + 18);
  gfx->print(o2);
}

void drawChoiceDialog() {
  const char *q, *o1, *o2;
  const char *sub1 = nullptr, *sub2 = nullptr;
  uint16_t c1, c2, t1, t2;
  if (choiceKind == 1) {  // evolucion
    q = T(S_EVO_Q); o1 = T(S_EVO_TAP); o2 = T(S_EVO_KEEP);
    c1 = UI_BAR_BAD; t1 = UI_WHITE; c2 = UI_TRACK; t2 = UI_INK;
  } else if (choiceKind == 3) {   // retirada a peticion
    q = T(S_RETIRE_Q); o1 = T(S_FAR_GO); o2 = T(S_FAR_STAY);
    c1 = UI_BAR_WARN; t1 = UI_INK; c2 = UI_BAR_OK; t2 = UI_WHITE;
    // The price, spelled out, and only when there is one: retiring a creature
    // that has already earned its farewell costs nothing and must not claim to.
    // An EARLY one now costs TWO things and says both -- the creature is not
    // banked at all, which is the half a player would not otherwise discover
    // until the party screen came up empty.
    if (!pet.retireIsFree()) { sub1 = T(S_RETIRE_COST); sub2 = T(S_RETIRE_GONE); }
  } else {                // despedida
    q = T(S_FAR_Q); o1 = T(S_FAR_GO); o2 = T(S_FAR_STAY);
    c1 = UI_BAR_WARN; t1 = UI_INK; c2 = UI_BAR_OK; t2 = UI_WHITE;
  }
  drawConfirmPanel(q, sub1, sub2, UI_BAR_BAD, o1, c1, t1, o2, c2, t2);
}

// boton-CTA rojo y grande para evolucionar (pulsa para llamar la atencion)
void drawEvolveButton() {
  uint32_t now = millis();
  int p = (int)(5 * sinf(now * 0.006f));  // late: -5..5
  int x = EVO_BTN_X - p, y = EVO_BTN_Y - p, w = EVO_BTN_W + 2 * p, h = EVO_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 18, UI_BAR_BAD);
  gfx->drawRoundRect(x, y, w, h, 18, UI_WHITE);
  gfx->drawRoundRect(x + 2, y + 2, w - 4, h - 4, 16, UI_WHITE);
  gfx->setTextColor(UI_WHITE);
  gfx->setTextSize(3);
  const char *t = T(S_EVO_TAP);
  gfx->setCursor(CX - (int)strlen(t) * 9, y + h / 2 - 11);
  gfx->print(t);
}

// boton-CTA dorado de despedida: "<nombre> quiere decirte algo..."
void drawFarewellButton() {
  uint32_t now = millis();
  int p = (int)(4 * sinf(now * 0.005f));
  int x = FAR_BTN_X - p, y = FAR_BTN_Y - p, w = FAR_BTN_W + 2 * p, h = FAR_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 16, UI_BAR_WARN);
  gfx->drawRoundRect(x, y, w, h, 16, UI_INK);
  char buf[52];
  const char *nm = pet.nick[0] ? pet.nick : DEX_TBL[pet.speciesId].name;
  snprintf(buf, sizeof(buf), T(S_FAREWELL_BTN), nm);
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(buf) * 6, y + h / 2 - 8);
  gfx->print(buf);
}

// boton-CTA sombrio de escapada por abandono: "<nombre> se siente abandonado..."
// (final triste: azul-gris oscuro, latido lento y apagado)
void drawRunawayButton() {
  uint32_t now = millis();
  int p = (int)(3 * sinf(now * 0.003f));
  int x = FAR_BTN_X - p, y = FAR_BTN_Y - p, w = FAR_BTN_W + 2 * p, h = FAR_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 16, C565(0x3a, 0x44, 0x5a));
  gfx->drawRoundRect(x, y, w, h, 16, C565(0x70, 0x80, 0x98));
  char buf[52];
  const char *nm = pet.nick[0] ? pet.nick : DEX_TBL[pet.speciesId].name;
  snprintf(buf, sizeof(buf), T(S_RUNAWAY_BTN), nm);
  gfx->setTextColor(C565(0xc8, 0xd2, 0xe0));
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(buf) * 6, y + h / 2 - 8);
  gfx->print(buf);
}

// animacion epica de evolucion: halo radial + rayos giratorios + parpadeo del
// sprite acelerando + chispas que salen disparadas + fogonazo final
void drawEvolveFX(uint32_t now) {
  float t = pet.evolveT();          // 0..1
  int cx = CX, cy = PET_GROUND - 96;

  // halo radial que crece y pulsa
  int halo = 36 + (int)(t * 150) + (int)(8 * sinf(now * 0.02f));
  for (int k = 0; k < 4; k++) {
    int r = halo - k * 7;
    if (r > 0) gfx->drawCircle(cx, cy, r, UI_WHITE);
  }
  // rayos giratorios desde el centro del bicho
  float base = now * 0.004f;
  for (int i = 0; i < 12; i++) {
    float a = base + i * (float)(PI / 6);
    int len = 90 + (int)(70 * (0.5f + 0.5f * sinf(now * 0.012f + i)));
    gfx->drawLine(cx, cy, cx + (int)(cosf(a) * len), cy + (int)(sinf(a) * len), UI_WHITE);
  }
  // parpadeo entre la forma ANTERIOR y la NUEVA (siluetas), acelerando; al
  // final (t>0.9) se queda fija en la nueva para el fogonazo de revelado
  int period = 60 + (int)(220 * (1.0f - t));
  bool showOld = t < 0.9f && evoPmd.loaded && ((now / period) % 2) == 0;
  if (showOld) drawPmdActM(evoPmd, PMD_IDLE, cx, PET_GROUND, 0, true, true, 5);
  else drawPmdAct(PMD_IDLE, cx, PET_GROUND, 0, true, true, 5);
  // chispas que salen disparadas
  for (int i = 0; i < 10; i++) {
    float a = i * (float)(PI / 5) + t * 4.0f;
    int d = (int)((now / 14 + i * 33) % 200);
    int sx = cx + (int)(cosf(a) * d), sy = cy + (int)(sinf(a) * d);
    gfx->fillRect(sx - 2, sy - 2, 5, 5, (i & 1) ? C565(0xff, 0xe0, 0x70) : UI_WHITE);
  }
  // fogonazo final antes de revelar la forma nueva
  if (t > 0.9f) gfx->fillCircle(cx, cy, (int)(300 * (t - 0.9f) / 0.1f), UI_WHITE);
}

void drawPet() {
  if (pmd.loaded) {
    drawPetPMD();
    return;
  }
  if (mon.loaded) {
    drawPetSD();
    return;
  }
  int fi = flashIdxForDex(pet.speciesId);
  if (fi < 0) {
    // sin SD y sin sprite de flash: aviso claro de que faltan sprites
    gfx->setTextColor(inkColor());
    gfx->setTextSize(6);
    gfx->setCursor(CX - 18, PET_CY - 80);
    gfx->print("?");
    gfx->setTextSize(2);
    const char *l1 = T(S_NO_SPRITES);
    gfx->setCursor(CX - (int)strlen(l1) * 6, PET_CY - 4);
    gfx->print(l1);
    const char *l2 = T(S_LOAD_SPRITES);
    gfx->setCursor(CX - (int)strlen(l2) * 6, PET_CY + 20);
    gfx->print(l2);
    return;
  }
  const Species &sp = SPECIES[fi];
  int s = sp.scale;
  int x = CX - 16 * s;
  int y = PET_CY - 16 * s;

  // animacion de evolucion: alterna la silueta de la forma anterior y la nueva
  if (pet.evolving()) {
    bool flash = (millis() / 300) % 2;
    int16_t showDex = (flash && pet.prevSpeciesId >= 0) ? pet.prevSpeciesId : pet.speciesId;
    int sfi = flashIdxForDex(showDex);
    if (sfi >= 0) {
      const Species &show = SPECIES[sfi];
      drawMap(show.sprite, SPRITE_H, CX - 16 * show.scale, PET_CY - 16 * show.scale, show.scale, flash);
    }
    return;
  }

  PetMood m = pet.mood();
  if (m == MOOD_HAPPY && (millis() / 500) % 2) y -= 6;  // saltito

  drawMap(sp.sprite, SPRITE_H, x, y, s, false);

  // expresiones superpuestas usando las anclas de la especie
  bool blink = (millis() % 3500 < 300);
  if (m == MOOD_SLEEPING || blink) {
    overlayEye(sp, x, y, s, sp.eyeColL);
    overlayEye(sp, x, y, s, sp.eyeColR);
  }
  if (m == MOOD_EATING) overlayMouth(sp, x, y, s, true);
  else if (m == MOOD_SAD) overlayMouth(sp, x, y, s, false);

  if (pet.showHeart()) drawMap(SPR_HEART, 32, x + 20 * s, y - 2 * s, 2, false);
}

// ---------- escena de bano ----------

void startBath() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony || bathUntil) return;
  bathUntil = millis() + 3000;
  bathPending = true;
  int cx = (int)beh.x;
  for (auto &b : bubbles) {
    b.x = cx - 70 + random(140);
    b.y = PET_GROUND - random(150);
    b.r = 8 + random(16);
    b.ph = random(64);
  }
}

void drawBath() {
  uint32_t now = millis();
  if (now > bathUntil) {
    bathUntil = 0;
    if (bathPending) {
      bathPending = false;
      pet.clean();
      // pose de alegria al quedar limpio
      if (pmd.has(PMD_POSE)) {
        beh.mode = 2;
        beh.act = PMD_POSE;
        beh.t0 = now;
        beh.until = now + pmdActTotalMs(pmd.acts[PMD_POSE]) * 2;
      }
    }
    return;
  }
  uint32_t left = bathUntil - now;
  if (left > 800) {
    // espuma: pompas meciendose y subiendo poco a poco
    float t = now / 220.0f;
    for (auto &b : bubbles) {
      int bx = b.x + (int)(sinf(t + b.ph) * 6);
      int by = b.y - (int)((3000 - left) / 90);
      gfx->fillCircle(bx, by, b.r, UI_WHITE);
      gfx->drawCircle(bx, by, b.r, 0x7E3D);
      gfx->fillCircle(bx - b.r / 3, by - b.r / 3, b.r / 4, UI_BG_DAY);
    }
  } else {
    // las pompas revientan: destellos
    for (int i = 0; i < 8; i++) {
      auto &b = bubbles[i];
      int sx = b.x + (i % 3) * 6 - 6, sy = b.y - 18;
      uint16_t col = (i % 2) ? UI_BAR_WARN : UI_WHITE;
      gfx->fillRect(sx - 6, sy - 1, 13, 3, col);
      gfx->fillRect(sx - 1, sy - 6, 3, 13, col);
    }
  }
}

// ---------- mascota PMD: comportamiento ----------

uint32_t pmdActTotalMs(const PmdAct &a) {
  uint32_t t = 0;
  for (uint8_t i = 0; i < a.frames; i++) t += a.ms[i];
  return t ? t : 100;
}

uint8_t pmdFrameAt(const PmdAct &a, uint32_t t, bool loop) {
  uint32_t total = pmdActTotalMs(a);
  if (!loop && t >= total) return a.frames - 1;
  t %= total;
  uint8_t i = 0;
  while (t >= a.ms[i]) {
    t -= a.ms[i];
    i = (i + 1) % a.frames;
  }
  return i;
}

// dibuja una accion anclada por la base (centro-x, suelo) y devuelve su escala
// dibuja una accion de un PmdMon concreto (m); drawPmdAct usa el global pmd
void drawPmdActZ(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil,
                 uint8_t maxS, uint16_t zoom100, uint16_t maxPx) {
  const PmdAct &a = m.acts[actId];
  if (!a.frames) return;
  uint8_t sBase = m.acts[PMD_IDLE].h ? 170 / m.acts[PMD_IDLE].h : 5;
  if (sBase < 2) sBase = 2;
  if (sBase > maxS) sBase = maxS;
  uint8_t s = sBase;
  while (s > 2 && a.h * s > 250) s--;  // acciones con frame grande (ataque)
  uint32_t sc = (uint32_t)s * zoom100;
  if (zoom100 > 100 && a.h * sc > (uint32_t)maxPx * 100) sc = (uint32_t)maxPx * 100 / a.h;
  uint8_t fi = pmdFrameAt(a, t, loop);
  const uint8_t *fr = a.data + (uint32_t)fi * a.w * a.h;
  int x0 = cx - (int)(a.w * sc / 200), y0 = groundY - (int)((a.base ? a.base : a.h) * sc / 100);
  for (int r = 0; r < a.h; r++) {
    const uint8_t *row = fr + r * a.w;
    int ya = y0 + (int)(r * sc / 100), yb = y0 + (int)((r + 1) * sc / 100);
    forRuns(a.w, [&](int c) { return indexedKey(row[c], sil); },
            [&](int c0, int len, int k) {
              int xa = x0 + (int)(c0 * sc / 100), xb = x0 + (int)((c0 + len) * sc / 100);
              gfx->fillRect(xa, ya, xb - xa, yb - ya, sil ? INK_K : m.pal[k]);
            });
  }
}
void drawPmdActM(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  drawPmdActZ(m, actId, cx, groundY, t, loop, sil, maxS, 100, 0);
}
void drawPmdAct(uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  drawPmdActM(pmd, actId, cx, groundY, t, loop, sil, maxS);
}

// elige el siguiente capricho del bicho cuando esta contento
void behNext() {
  uint32_t now = millis();
  beh.t0 = now;
  int r = random(100);
  if (r < 35 && (pmd.has(PMD_WALKL) || pmd.has(PMD_WALKR))) {
    beh.mode = 1;  // paseo
    beh.targetX = 150 + random(176);
    beh.until = now + 15000;
  } else if (r < 60) {
    // gesto aleatorio entre los disponibles
    // (Hop fuera: salta demasiado alto; Sit fuera: mira hacia atras)
    static const uint8_t flair[] = { PMD_POSE, PMD_NOD, PMD_BREATH };
    uint8_t pick[3], n = 0;
    for (uint8_t f : flair)
      if (pmd.has(f)) pick[n++] = f;
    if (n) {
      beh.mode = 2;
      beh.act = pick[random(n)];
      beh.until = now + pmdActTotalMs(pmd.acts[beh.act]);
      return;
    }
    beh.mode = 0;
    beh.until = now + 2000 + random(3000);
  } else {
    beh.mode = 0;  // mirar al frente
    beh.until = now + 2000 + random(3000);
  }
}

void drawPetPMD() {
  uint32_t now = millis();

  if (pet.evolving()) {
    drawEvolveFX(now);
    return;
  }
  if (evoPmd.loaded) evoPmd.unload();  // termino la evolucion: libera la forma anterior

  PetMood m = pet.mood();
  uint8_t act;
  bool loop = true;
  if (m == MOOD_SLEEPING && pmd.has(PMD_SLEEP)) {
    act = PMD_SLEEP;
    beh.mode = 0;
  } else if (m == MOOD_EATING && pmd.has(PMD_EAT)) {
    act = PMD_EAT;
    beh.t0 = 0;
  } else if (m == MOOD_SAD && pmd.has(PMD_HURT)) {
    act = PMD_HURT;
  } else if (pet.musicOn) {
    // dancing to the jukebox: hops in place on the beat
    act = pmd.has(PMD_HOP) ? PMD_HOP : pmd.has(PMD_NOD) ? PMD_NOD : PMD_IDLE;
    int bob = (int)(fabsf(sinf(now * 3.14159f / JUKE_BEAT_MS)) * 10);
    drawPmdAct(act, (int)beh.x, PET_GROUND - bob, now, true, false, 5);
    drawMusicNotes(now);
    if (pet.showHeart()) drawMap(SPR_HEART, 32, (int)beh.x + 50, PET_GROUND - 190, 2, false);
    return;
  } else {
    // contento: el planificador decide (idle / paseo / gesto)
    if (now > beh.until) behNext();
    if (beh.mode == 1) {
      float d = beh.targetX - beh.x;
      if (fabsf(d) < 4) {
        behNext();
        act = PMD_IDLE;
      } else {
        beh.x += (d > 0 ? 3.0f : -3.0f);
        act = (d > 0) ? PMD_WALKR : PMD_WALKL;
      }
    } else {
      act = (beh.mode == 2) ? beh.act : PMD_IDLE;
      loop = false;
    }
    if (!pmd.has(act)) act = PMD_IDLE;
  }

  drawPmdAct(act, (int)beh.x, PET_GROUND, now - beh.t0, loop || act == PMD_IDLE, false, 5);

  if (pet.showHeart()) drawMap(SPR_HEART, 32, (int)beh.x + 50, PET_GROUND - 190, 2, false);
}

static void drawQuaver(int x, int y, uint16_t col) {
  gfx->fillCircle(x, y, 5, col);
  gfx->fillRect(x + 3, y - 18, 3, 18, col);
  gfx->fillTriangle(x + 6, y - 18, x + 6, y - 9, x + 13, y - 11, col);
}
void drawMusicNotes(uint32_t now) {
  for (int k = 0; k < 3; k++) {
    float p = ((now + k * 600) % 1800) / 1800.0f;
    if (p > 0.9f) continue;
    int side = (k & 1) ? -1 : 1;
    int x = (int)beh.x + side * (46 + (int)(p * 24)) + (int)(sinf(p * 9.0f) * 5);
    int y = PET_GROUND - 115 - (int)(p * 75);
    drawQuaver(x, y, inkColor());
  }
}

// sprite animado desde la SD: zoom entero por pixel, frames a su ritmo
void drawPetSD() {
  int s = mon.scale;
  int w = mon.w * s, h = mon.h * s;
  int x = CX - w / 2;
  int y = PET_CY - h / 2;

  bool sil = false;
  if (pet.evolving()) {
    sil = (millis() / 300) % 2;
  } else if (pet.mood() == MOOD_HAPPY && (millis() / 500) % 2) {
    y -= 6;  // saltito
  }

  uint16_t fm = mon.frameMs ? mon.frameMs : 100;
  uint16_t fi = pet.sleeping ? 0 : (millis() / fm) % mon.frames;
  const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
  for (int r = 0; r < mon.h; r++) {
    const uint8_t *row = fr + r * mon.w;
    forRuns(mon.w, [&](int c) { return indexedKey(row[c], sil); },
            [&](int c0, int len, int k) { gfx->fillRect(x + c0 * s, y + r * s, len * s, s, sil ? INK_K : mon.pal[k]); });
  }

  // emotes en vez de expresiones (los sprites importados no tienen anclas)
  if (pet.showHeart()) drawMap(SPR_HEART, 32, x + w - 30, y - 50, 2, false);
}

// ojo cerrado: borra el ojo 3x4 y dibuja el parpado
void overlayEye(const Species &sp, int x, int y, int s, int col) {
  gfx->fillRect(x + col * s, y + sp.eyeRow * s, 3 * s, 4 * s, sp.bodyColor);
  gfx->fillRect(x + col * s, y + (sp.eyeRow + 2) * s, 3 * s, s, INK_K);
}

// borra la sonrisa base y pinta boca abierta (comer) o ceno (triste)
void overlayMouth(const Species &sp, int x, int y, int s, bool open) {
  int mc = sp.mouthCol, mr = sp.mouthRow;
  gfx->fillRect(x + (mc - 3) * s, y + mr * s, 7 * s, 2 * s, sp.bodyColor);
  if (open) {
    gfx->fillRect(x + (mc - 2) * s, y + mr * s, 5 * s, 2 * s, INK_K);
  } else {
    gfx->fillRect(x + (mc - 2) * s, y + mr * s, 5 * s, s, INK_K);
    gfx->fillRect(x + (mc - 3) * s, y + (mr + 1) * s, s, s, INK_K);
    gfx->fillRect(x + (mc + 3) * s, y + (mr + 1) * s, s, s, INK_K);
  }
}

void drawPoops() {
  for (int i = 0; i < pet.poops; i++) {
    drawMap(SPR_POOP, 32, 36 + i * 46, 244, 2, false);
  }
}

// The four care stats, on the panel's RIM.
//
// They were 2x2 bars across y 318..361 -- four 100 px tracks laid over the
// widest part of the circle, which is the one place a round panel has room to
// spare, while the home row below them had to make do with 60 px icons. An arc
// lives where no rectangular layout can put anything at all.
//
// Colour still means SEVERITY, exactly as the bars did; the label says which
// stat it is. The gap at 12 o'clock is for the name, and the arcs stop short of
// +-102 degrees so the ends stay clear of the poop row on the left.
#define CARE_R 216        // the track's radius
#define CARE_W 12         // and its thickness
#define CARE_LBL_R 190    // labels hug the track rather than floating mid-field
static const int16_t CARE_ARC[4][2] = {
  { -108, -82 }, { -78, -52 }, { 52, 78 }, { 82, 108 },
};
// One hue each. Four arcs in the same colour are four arcs you have to read the
// labels of; and the severity colours they used to share were 1.64:1 against
// their own groove, so the filled part barely differed from the empty part.
static const uint16_t CARE_COL[4] = {
  UI_CARE_FOOD, UI_CARE_JOY, UI_CARE_ENE, UI_CARE_HYG,
};
#define CARE_LOW 25       // below this the LABEL carries the warning

int uiCareArcTop(int i) {
  if (i < 0 || i >= 4) return -1;
  int top = 466;
  for (int a = CARE_ARC[i][0]; a <= CARE_ARC[i][1]; a++) {
    float rad = ((float)a - 90.0f) * 0.01745329f;
    int y = CY + (int)(sinf(rad) * CARE_R) - CARE_W / 2;
    if (y < top) top = y;
  }
  return top;
}

int uiHeaderNameBottom() { return HEADER_NAME_Y + HEADER_NAME_H; }

static void drawCareArc(int i, const char *label, uint8_t val) {
  int a0 = CARE_ARC[i][0], a1 = CARE_ARC[i][1];
  uiArc(CX, CY, CARE_R, a0, a1, CARE_W, UI_TRACK);
  if (val) uiArc(CX, CY, CARE_R, a0, a0 + (a1 - a0) * val / 100, CARE_W, CARE_COL[i]);
  float mid = ((float)(a0 + a1) / 2.0f - 90.0f) * 0.01745329f;
  int lx = CX + (int)(cosf(mid) * CARE_LBL_R);
  int ly = CY + (int)(sinf(mid) * CARE_LBL_R);
  gfx->setTextColor(val < CARE_LOW ? UI_BAR_BAD : inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(lx - (int)strlen(label) * 6, ly - 8);
  gfx->print(label);
}

void drawBars() {
  drawCareArc(0, T(S_BAR_FOOD), pet.fullness);
  drawCareArc(1, T(S_BAR_JOY), pet.joy);
  drawCareArc(2, T(S_BAR_ENE), pet.energy);
  drawCareArc(3, T(S_BAR_HYG), pet.hygiene);
}

void drawButtons() {
  // Soft pastel tile per icon (indexed by BTN_*), a darker ring of the same hue
  // and a drop shadow, so the row reads as raised buttons (from crenberries428/TamaPoke, MIT).
  const uint16_t tint[BTN_COUNT]   = { C565(255, 230, 224), C565(255, 244, 204),
                                       C565(220, 238, 255), C565(226, 232, 248) };
  const uint16_t accent[BTN_COUNT] = { C565(232, 110, 96), C565(232, 180, 60),
                                       C565(90, 160, 220), C565(110, 128, 190) };
  for (int i = 0; i < BTN_COUNT; i++) {
    bool off = uiButtonDisabled(i);   // durmiendo solo funciona LUZ
    int bx = buttons[i].cx - BTN_HALF, by = buttons[i].cy - BTN_HALF;
    int bs = 2 * BTN_HALF;
    if (!pet.sleeping) {
      gfx->fillRoundRect(bx + 1, by + 4, bs, bs, 16, gNight ? C565(8, 10, 20) : C565(150, 144, 128));
      gfx->fillRoundRect(bx, by, bs, bs, 16, tint[i]);
      gfx->fillRoundRect(bx + 4, by + 3, bs - 8, bs / 2 - 4, 12, UI_WHITE);   // gloss
      gfx->drawRoundRect(bx + 1, by + 1, bs - 2, bs - 2, 15, accent[i]);
    }
    gfx->drawRoundRect(bx, by, bs, bs, 16, inkColor());
    if (!off) drawMap(buttons[i].icon, 16, buttons[i].cx - 24, buttons[i].cy - 24, 3, false);
  }
}

// The food, held in front of the creature and eaten in EAT_BITES bites. Each
// bite takes a round chunk out of the icon, the food jolts and a few crumbs
// fly; the last bite takes what is left. Progress comes from the pet's own
// eat timer, so the animation can never outlive or undershoot the eating mood.
// (From crenberries428/TamaPoke, MIT.)
#define EAT_BITES 4
void drawEatFood() {
  if (!pet.eating()) return;
  static const char *const *const ICONS[4] = { SPR_ICON_FOOD, SPR_ICON_BERRY_B,
                                               SPR_ICON_BERRY_G, SPR_ICON_CANDY };
  const char *const *ic = ICONS[eatItem < 4 ? eatItem : 0];
  uint32_t el = EAT_ANIM_MS - pet.eatLeftMs();
  // bite k lands at BITE_AT[k] ms and the food jolts for 160 ms after it
  static const uint16_t BITE_AT[EAT_BITES] = { 350, 900, 1450, 2000 };
  // chunk taken by each bite: centre and radius, in icon pixels
  static const int8_t BITE_X[EAT_BITES] = { 13, 3, 12, 8 };
  static const int8_t BITE_Y[EAT_BITES] = { 5, 7, 12, 8 };
  static const uint8_t BITE_R[EAT_BITES] = { 5, 5, 6, 10 };
  uint8_t bites = 0;
  int jolt = 0;
  for (uint8_t k = 0; k < EAT_BITES; k++) {
    if (el >= BITE_AT[k]) {
      bites = k + 1;
      uint32_t since = el - BITE_AT[k];
      if (since < 160) jolt = (since < 80) ? 3 : 1;
    }
  }
  if (bites >= EAT_BITES) return;   // the last bite finished it
  int cx = pmd.loaded ? (int)beh.x : CX;
  const int S = 4, x0 = cx - 8 * S, y0 = PET_GROUND - 88 + jolt;
  for (int r = 0; r < 16; r++)
    for (int c = 0; c < 16; c++) {
      char ch = ic[r][c];
      if (ch == '.') continue;
      bool gone = false;
      for (uint8_t k = 0; k < bites && !gone; k++) {
        int dx = c - BITE_X[k], dy = r - BITE_Y[k];
        gone = dx * dx + dy * dy <= BITE_R[k] * BITE_R[k];
      }
      if (!gone) gfx->fillRect(x0 + c * S, y0 + r * S, S, S, spriteColor(ch));
    }
  // crumbs: a short spray from the bite that just landed
  for (uint8_t k = 0; k < bites; k++) {
    uint32_t since = el - BITE_AT[k];
    if (since >= 380) continue;
    int bx = x0 + BITE_X[k] * S, by = y0 + BITE_Y[k] * S;
    for (int i = 0; i < 3; i++) {
      int dx = (i - 1) * (6 + (int)(since / 14)) + (BITE_X[k] > 8 ? 6 : -6);
      int dy = -4 + (int)(since * since / 3600) - i * 3;
      gfx->fillRect(bx + dx, by + dy, 3, 3, spriteColor(ic[8][8] == '.' ? 'w' : ic[8][8]));
    }
  }
}

const char *eggMsg() {
  switch (pet.eggCracks()) {
    case 0: return T(S_EGG_TOUCH);
    case 1: return T(S_EGG_MOVES);
    default: return T(S_EGG_ALMOST);
  }
}

const char *statusMsg() {
  if (pet.evolving()) return T(S_EVOLVING);
  if (bathUntil) return "Splish splash!";  // onomatopeya universal
  if (pet.sleeping) return "Zzz...";
  if (pet.eating()) return T(S_EATING);
  if (pet.tuneToast()) {
    static char tuneBuf[40];
    snprintf(tuneBuf, sizeof(tuneBuf), T(S_NEW_TUNE_FMT), tuneName(pet.tuneToastId));
    return tuneBuf;
  }
  if (gStepCounterEnabled && pet.stepToast()) {
    static char stepBuf[24];
    snprintf(stepBuf, sizeof(stepBuf), T(S_STEP_GOAL_FMT), pet.stepToastSteps);
    return stepBuf;
  }
  if (pet.showHeart()) return T(S_LIKES);
  if (pet.fullness < 25) return T(S_HUNGRY);
  if (pet.hygiene < 25) return T(S_NEEDS_BATH);
  if (pet.energy < 25) return T(S_EXHAUSTED);
  if (pet.joy < 25) return T(S_SAD);
  if (pet.weight > 60) return T(S_CHUBBY);
  if (pet.shiny && pet.realAgeMin < 15) return T(S_IS_SHINY);   // its first 15 real minutes
  return T(S_HAPPY);
}

// dibuja un mapa de n x n pixeles escalado; silhouette=true lo pinta en tinta
// An 8bpp indexed avatar, same shape as the badge art: 0xFF is transparent.
void drawAvatar(uint8_t which, int x, int y, int s) {
  const AvatarArt &a = AVATARS[which % AVATAR_COUNT];
  for (int r = 0; r < AVATAR_PX; r++) {
    const uint8_t *row = a.idx + r * AVATAR_PX;
    forRuns(AVATAR_PX, [&](int c) { return indexedKey(row[c], false); },
            [&](int c0, int len, int k) { gfx->fillRect(x + c0 * s, y + r * s, len * s, s, a.pal[k]); });
  }
}

void drawMap(const char *const *map, int n, int x, int y, int s, bool silhouette) {
  for (int r = 0; r < n; r++) {
    const char *row = map[r];
    forRuns(n, [&](int c) { return charKey(row[c], silhouette); },
            [&](int c0, int len, int k) { gfx->fillRect(x + c0 * s, y + r * s, len * s, s, silhouette ? INK_K : spriteColor((char)k)); });
  }
}
