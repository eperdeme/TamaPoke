// Real bag/ball/result handlers and collection writes, not pre-filled box slots.
// Host NVS has no physical headroom model; the native storage suite covers that.
#include "Arduino.h"
#include "Preferences.h"
#include "battle.h"
#include "inventory.h"
#include "nvsinfo.h"
#include "party.h"
#include "pet.h"
#include "wild.h"
#include <algorithm>
#include <array>
#include <limits.h>
#include <stdexcept>
#include <string>
#include <tuple>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

uint32_t g_seed = 0xCA7C1000;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
void FakeESP::restart() { fprintf(stderr, "FAIL capture: unexpected firmware reset\n"); std::_Exit(85); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

void setup();
void loop();
void onTap(int16_t x, int16_t y);
bool startWildBattle(uint8_t region, bool hard);
int btlCellIndexAt(int16_t x, int16_t y);
extern Pet pet;
extern bool battleOpen, btlWild, btlOver, btlWon, wildCaught;
extern bool partyPick, handoverStored, focusBlocked, gNvsLow;
extern uint8_t btlMenu, btlMsgCount, wildLvl, wildIv[4], wildDropN;
extern ItemKey wildDrops[4];
extern int16_t wildDex;
extern bool wildShiny;
extern Combatant btlFoe;
extern uint16_t squadMask;

constexpr unsigned CAPTURES = 1000;
// Keep headroom so a Master Ball drop cannot hide a missing consumption at the stack cap.
constexpr uint8_t FIXTURE_BALLS = BAG_STACK_MAX - 1;
static_assert(PARTY_SLOTS + BOX_SLOTS >= CAPTURES, "capture regression needs room for 1000 creatures");

static void require(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}

static bool sameMon(const PartyMon &a, const PartyMon &b) {
  return std::tie(a.dex, a.level, a.medals, a.ivAtk, a.ivDef, a.ivSpe, a.ivHp,
                  a.trAtk, a.trDef, a.trSpe, a.shiny, a.stateVersion, a.fullness,
                  a.joy, a.energy, a.hygiene, a.poops, a.weight, a.bond, a.berryKnown,
                  a.careMistakes, a.evoDeclinedLv, a.lastLearnLevel, a.ageMinutes) ==
         std::tie(b.dex, b.level, b.medals, b.ivAtk, b.ivDef, b.ivSpe, b.ivHp,
                  b.trAtk, b.trDef, b.trSpe, b.shiny, b.stateVersion, b.fullness,
                  b.joy, b.energy, b.hygiene, b.poops, b.weight, b.bond, b.berryKnown,
                  b.careMistakes, b.evoDeclinedLv, b.lastLearnLevel, b.ageMinutes) &&
         std::equal(std::begin(a.nick), std::end(a.nick), std::begin(b.nick)) &&
         std::equal(std::begin(a.moves), std::end(a.moves), std::begin(b.moves));
}

struct Player {
  std::array<uint16_t, GYM_REGIONS> easy{}, hard{};
  std::array<uint8_t, sizeof(pet.dexReg)> dex{}, shiny{};
  std::array<int16_t, REGION_COUNT> eggs{};
  std::array<char, sizeof(pet.trainerName)> trainer{};
  uint16_t medals = 0, streak = 0, bestStreak = 0, milestone = 0;
  uint16_t gameHi = 0, strHi = 0, spdHi = 0, memoHi = 0;
  uint32_t careDay = 0;
  uint8_t avatar = 0, region = 0;
};

static Player playerOf(const Pet &p) {
  Player result;
  for (uint8_t region = 0; region < GYM_REGIONS; region++) {
    result.easy[region] = p.badgeMask(region, false);
    result.hard[region] = p.badgeMask(region, true);
  }
  std::copy(std::begin(p.dexReg), std::end(p.dexReg), result.dex.begin());
  std::copy(std::begin(p.dexShinyReg), std::end(p.dexShinyReg), result.shiny.begin());
  std::copy(std::begin(p.eggByRegion), std::end(p.eggByRegion), result.eggs.begin());
  std::copy(std::begin(p.trainerName), std::end(p.trainerName), result.trainer.begin());
  result.medals = p.totalMedals;
  result.streak = p.streak; result.bestStreak = p.bestStreak;
  result.milestone = p.lastMilestone; result.careDay = p.lastCareDay;
  result.gameHi = p.gameHi; result.strHi = p.strHi;
  result.spdHi = p.spdHi; result.memoHi = p.memoHi;
  result.avatar = p.avatar; result.region = p.region;
  return result;
}

static bool samePlayer(const Player &a, const Player &b) {
  return a.easy == b.easy && a.hard == b.hard && a.dex == b.dex &&
         a.shiny == b.shiny && a.eggs == b.eggs && a.trainer == b.trainer &&
         std::tie(a.medals, a.streak, a.bestStreak, a.milestone, a.careDay,
                  a.gameHi, a.strHi, a.spdHi, a.memoHi, a.avatar, a.region) ==
         std::tie(b.medals, b.streak, b.bestStreak, b.milestone, b.careDay,
                  b.gameHi, b.strHi, b.spdHi, b.memoHi, b.avatar, b.region);
}

struct Expected {
  unsigned completed = 0;
  PartyMon original;
  Player player;
  std::array<PartyMon, CAPTURES> caught{};
  std::array<uint8_t, ITEM_COUNT> items{};
};
static Expected expected;

static void healthy() {
  require(pet.saveHealthy() && !pet.savePending() && party.saveHealthy() && party.writable(),
          "capture left saving unhealthy or pending");
  require(pet.endedKind == CER_NONE && !partyPick && !handoverStored && !focusBlocked,
          "capture left an unresolved creature handover");
  require(!gNvsLow && !nvsLowOnSpace(), "host reported a modeled low-space warning");
}

static void compareParty(const Party &actual, bool durable) {
  const unsigned population = actual.count() + actual.boxCount();
  require(population == expected.completed, durable
      ? "capture did not increment durable population"
      : "capture did not increment live population");
  require(actual.count() == std::min(expected.completed, unsigned(PARTY_SLOTS)),
          "capture did not fill party before box");
  for (unsigned index = 0; index < PARTY_SLOTS + BOX_SLOTS; index++) {
    const PartyMon &mon = index < PARTY_SLOTS
        ? actual.slots[index] : actual.box[index - PARTY_SLOTS];
    if (index < expected.completed)
      require(sameMon(mon, expected.caught[index]),
              std::string(durable ? "durable" : "live") +
              " capture record differs at index " + std::to_string(index));
    else require(mon.empty(), "capture overwrote an unused collection slot");
  }
}

static void compareBaseline(const Pet &actual) {
  require(sameMon(actual.toPartyMon(), expected.original),
          "capture changed the original active creature's identity, moves or care");
  require(samePlayer(playerOf(actual), expected.player), "capture changed original player progress");
  require(!actual.sleeping && !actual.frozen && !actual.awaitingStarter() &&
          actual.ceremony == CER_NONE && actual.endedKind == CER_NONE,
          "capture changed the original active creature's lifecycle");
}

static void compareInventory(const Inventory &actual) {
  for (ItemKey key = 1; key < ITEM_COUNT; key++)
    require(actual.count(key) == expected.items[key],
            "capture ball consumption or intentional drop differs for item " + std::to_string(key));
}

static void verify() {
  healthy();
  Party durable;
  durable.begin();
  require(durable.saveHealthy() && durable.writable(), "captured collection could not be reloaded");
  compareParty(durable, true);
  compareParty(party, false);
  compareBaseline(pet);
  compareInventory(bag);
  Inventory savedBag;
  savedBag.begin();
  compareInventory(savedBag);
}

struct Point { int16_t x = 0, y = 0; };
static std::array<Point, 4> cellCenters() {
  std::array<Point, 4> result;
  for (int cell = 0; cell < 4; cell++) {
    int left = 466, top = 466, right = -1, bottom = -1;
    for (int16_t y = 0; y < 466; y++)
      for (int16_t x = 0; x < 466; x++)
        if (btlCellIndexAt(x, y) == cell) {
          left = std::min(left, int(x)); right = std::max(right, int(x));
          top = std::min(top, int(y)); bottom = std::max(bottom, int(y));
        }
    require(right >= left && bottom >= top, "battle action has no real hit target");
    result[cell] = {int16_t((left + right) / 2), int16_t((top + bottom) / 2)};
    require(btlCellIndexAt(result[cell].x, result[cell].y) == cell, "battle target center misses its action");
  }
  return result;
}

static void tap(const Point &point) {
  onTap(point.x, point.y);
  loop();
}

static PartyMon encounter(unsigned index) {
  PartyMon mon;
  mon.dex = int16_t((index * 337u) % DEX_COUNT + 1);
  while (!speciesShowable(mon.dex)) mon.dex = int16_t(mon.dex % DEX_COUNT + 1);
  mon.level = 1 + (index * 37u) % MAX_LEVEL;
  mon.ivAtk = 8 + index % 24; mon.ivDef = 8 + (index * 7u) % 24;
  mon.ivSpe = 8 + (index * 11u) % 24; mon.ivHp = 8 + (index * 17u) % 24;
  mon.shiny = index % 9 == 0;
  wildApplyShiny(mon.shiny, mon.ivAtk, mon.ivDef, mon.ivSpe, mon.ivHp);
  mon.stateVersion = 1;
  mon.ageMinutes = uint32_t(mon.level - 1) * MINUTES_PER_LEVEL;
  if (mon.ageMinutes >= FAREWELL_AGE_MIN) mon.berryKnown |= PM_FAREWELL;
  mon.lastLearnLevel = mon.level;
  Pet foe;
  foe.dbgHatchAs(mon.dex, mon.shiny);
  foe.ageMinutes = mon.ageMinutes;
  foe.relearnFromLevel();
  std::copy(std::begin(foe.moves), std::end(foe.moves), std::begin(mon.moves));
  return mon;
}

static void capture(const std::array<Point, 4> &cells) {
  const unsigned index = expected.completed + 1;
  if (!bag.has(IT_MASTERBALL)) {
    require(bag.add(IT_MASTERBALL, FIXTURE_BALLS) == FIXTURE_BALLS, "cannot replenish fixture balls");
    expected.items[IT_MASTERBALL] = FIXTURE_BALLS;
  }
  require(bag.count(IT_MASTERBALL) < BAG_STACK_MAX, "ball fixture cannot observe consumption at stack cap");
  require(!battleOpen && !pet.hasLearnOffer(), "capture did not start from an idle real UI");
  squadMask = 1;
  require(startWildBattle(REGION_ALL, index % 2), "real wild encounter did not start");
  require(battleOpen && btlWild && !btlOver, "wild encounter did not open a real battle");

  // Only the encounter's individual is a fixture. The bag, catch, placement,
  // settlement and persistence below all run through their real callers.
  PartyMon mon = encounter(index);
  wildDex = mon.dex; wildLvl = mon.level; wildShiny = mon.shiny;
  wildIv[0] = mon.ivAtk; wildIv[1] = mon.ivDef;
  wildIv[2] = mon.ivSpe; wildIv[3] = mon.ivHp;
  combatantFromParty(btlFoe, mon);
  for (unsigned step = 0; btlMsgCount && step < 32; step++) tap(cells[0]);
  require(!btlMsgCount && battleOpen, "wild encounter narration did not reach its action menu");
  tap(cells[1]);
  require(btlMenu == 3, "real action handler did not open the capture bag");
  int master = -1, shown = 0;
  for (uint8_t at = 0; at < bag.distinctCount(); at++) {
    ItemKey key = bag.keyAt(at);
    if (!itemUsableInBattle(key)) continue;
    if (key == IT_MASTERBALL) master = shown;
    shown++;
  }
  require(master >= 0 && master < int(cells.size()), "fixture ball is not on the real bag page");
  tap(cells[master]);
  require(wildCaught && btlOver && btlWon, "real ball handler did not complete its guaranteed capture");
  expected.items[IT_MASTERBALL]--;
  require(wildDropN > 0, "capture did not run its real reward settlement");
  for (uint8_t drop = 0; drop < wildDropN; drop++) {
    ItemKey key = wildDrops[drop];
    require(itemValid(key), "capture settlement returned an invalid item");
    if (expected.items[key] < BAG_STACK_MAX) expected.items[key]++;
  }
  expected.caught[expected.completed++] = mon;
  verify();
  for (unsigned step = 0; battleOpen && step < 32; step++) tap(cells[0]);
  require(!battleOpen && !btlWild && !btlMsgCount, "real capture result handler did not return to idle");
  healthy();
  if (index == PARTY_SLOTS + 1)
    printf("MILESTONE party-full -> first boxed capture\n");
  for (unsigned boxIndex : {255u, 256u, 511u, 512u})
    if (index == PARTY_SLOTS + boxIndex + 1)
      printf("MILESTONE exact durable box index=%u captures=%u\n", boxIndex, index);
}

static void fixture() {
  for (size_t byte = 0; byte < sizeof(pet.dexReg); byte++) {
    pet.dexReg[byte] = uint8_t(0x55 + byte);
    pet.dexShinyReg[byte] = uint8_t(0xAA - byte);
  }
  pet.dbgHatchAs(9, true);
  pet.ageMinutes = 42 * MINUTES_PER_LEVEL;
  pet.ivAtk = 27; pet.ivDef = 19; pet.ivSpe = 23; pet.ivHp = 31;
  pet.trAtk = 13; pet.trDef = 17; pet.trSpe = 21;
  pet.fullness = 87; pet.joy = 76; pet.energy = 93; pet.hygiene = 88;
  pet.poops = 1; pet.weight = 17; pet.bond = 42; pet.berryKnown = true;
  pet.careMistakes = 2;
  pet.medals = 0x15; pet.totalMedals = 23;
  pet.streak = 3; pet.bestStreak = 7; pet.lastMilestone = 2; pet.lastCareDay = 20300;
  pet.gameHi = 17; pet.strHi = 29; pet.spdHi = 31; pet.memoHi = 11;
  pet.avatar = 3; pet.region = 0;
  strcpy(pet.nick, "ORIGINAL"); strcpy(pet.trainerName, "COLLECTOR");
  for (uint8_t region = 0; region < GYM_REGIONS; region++) {
    if (!region) { pet.badges = 0x85; pet.badgesHard = 0x12; }
    else {
      pet.badgesX[region - 1] = uint16_t(1u << region);
      pet.badgesHardX[region - 1] = uint16_t(3u << region);
    }
  }
  pet.relearnFromLevel();
  while (pet.hasLearnOffer()) pet.declineLearn();
  pet.lastLearnLevel = pet.level();
  pet.saveNow();
  require(bag.add(IT_MASTERBALL, FIXTURE_BALLS) == FIXTURE_BALLS, "cannot seed fixture balls");
  expected.original = pet.toPartyMon();
  expected.player = playerOf(pet);
  for (ItemKey key = 1; key < ITEM_COUNT; key++) expected.items[key] = bag.count(key);
  loop();
  verify();
}

static void expectations(bool write) {
  FILE *stream = fopen("capture.expected", write ? "wb" : "rb");
  require(stream != nullptr, "missing capture reboot expectations");
  bool complete = write ? fwrite(&expected, sizeof(expected), 1, stream) == 1
                        : fread(&expected, sizeof(expected), 1, stream) == 1;
  require(fclose(stream) == 0 && complete, "truncated capture reboot expectations");
  require(expected.completed <= CAPTURES, "invalid capture reboot count");
}

static bool checkpoint(unsigned completed) {
  return completed == PARTY_SLOTS || completed == 256 || completed == 512 || completed == CAPTURES;
}

static int scenario(const char *program, bool reload) {
  if (reload) {
    expectations(false);
    nvsLoad("capture.nvs");
    require(!nvs().empty(), "missing persisted capture NVS image");
  }
  setup();
  if (reload) {
    verify();
    Pet persisted;
    persisted.begin();
    compareBaseline(persisted);
    require(checkpoint(expected.completed), "unexpected cold restart checkpoint");
    printf("MILESTONE cold-reload captures=%u exact-records=%u party=%u box=%u\n",
           expected.completed, expected.completed, party.count(), party.boxCount());
  } else fixture();

  const auto cells = cellCenters();
  while (expected.completed < CAPTURES) {
    capture(cells);
    if (checkpoint(expected.completed)) {
      expectations(true);
      nvsSave("capture.nvs");
      printf("MILESTONE cold-restart captures=%u\n", expected.completed);
      fflush(stdout);
      execl(program, program, "--reload", (char *)nullptr);
      throw std::runtime_error("could not cold restart capture process");
    }
  }
  FILE *done = fopen("capture.done", "wb");
  require(done != nullptr, "cannot record 1000-capture completion");
  require(fputs("1000 captures reloaded\n", done) >= 0 && fclose(done) == 0,
          "cannot commit 1000-capture completion");
  printf("PASS actual captures persisted: 1000; original active/player preserved; all moves/care exact\n");
  printf("PASS host save-health/pending/warning checks (physical NVS headroom is not modeled)\n");
  return 0;
}

int main(int argc, char **argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);
  try {
    char program[PATH_MAX];
    require(realpath(argv[0], program) != nullptr, "cannot resolve capture executable");
    if (argc == 2) {
      require(!strcmp(argv[1], "--run") || !strcmp(argv[1], "--reload"), "unknown capture mode");
      return scenario(program, !strcmp(argv[1], "--reload"));
    }
    require(argc == 1, "usage: capture_test");
    const std::string directory = ".capture-regression-" + std::to_string(getpid());
    require(mkdir(directory.c_str(), 0700) == 0, "cannot create isolated capture save directory");
    pid_t child = fork();
    if (child == 0) {
      alarm(120);
      if (chdir(directory.c_str())) std::_Exit(87);
      execl(program, program, "--run", (char *)nullptr);
      std::_Exit(87);
    }
    int outcome = 0;
    bool waited = child > 0 && waitpid(child, &outcome, 0) == child;
    FILE *done = fopen((directory + "/capture.done").c_str(), "rb");
    bool completed = done != nullptr;
    if (done) fclose(done);
    for (const char *file : {"capture.expected", "capture.nvs", "capture.done"})
      unlink((directory + "/" + file).c_str());
    rmdir(directory.c_str());
    require(waited && WIFEXITED(outcome) && WEXITSTATUS(outcome) == 0 && completed,
            "isolated capture workflow failed, timed out, reset, or missed its final cold reload");
    printf("PASS 1000 real UI captures -> four exec cold reloads\n");
    return 0;
  } catch (const std::exception &failure) {
    fprintf(stderr, "FAIL capture: %s\n", failure.what());
    return 1;
  }
}
