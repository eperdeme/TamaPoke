#pragma once
#include <stdint.h>
#include "dex.h"

// The gym ladders: 8 leaders, the Elite 4 and the Champion for each region,
// with their real teams and levels -- Kanto from FireRed/LeafGreen, Johto from
// Gold/Silver/Crystal, Hoenn from Ruby/Sapphire/Emerald.
//
// HAND-AUTHORED -- not generated. The levels are the real ones and they happen
// to fit this game's curve almost exactly: level is age at 1/hour, a pet retires
// at 73 after three days and caps at 100, so Brock at 12-14 is an afternoon and
// Lance at 54-60 is a well-raised creature. Nothing was rescaled.
//
// There is deliberately no gating. A leader always brings its whole team and you
// bring whoever you have, so attrition is the difficulty: one strong creature
// can sweep Brock but will not survive five of Lance's in a row.

// FOUR ROSTER SLOTS ARE DELIBERATE SUBSTITUTIONS, not transcription errors.
// SpriteCollab has no art for these, so as written they put a bare dex NUMBER on
// screen where a leader's creature should be -- and three of them LEAD, which is
// the first thing you see in the fight:
//
//   ELESA   ZEBSTRIKA  523 -> GALVANTULA 596   (Bug/Electric, still fast)
//   MARLON  CARRACOSTA 565 -> SEISMITOAD 537   (bulky Water)
//   MARSHAL THROH      538 -> HARIYAMA   297   (bulky pure Fighting; see below)
//   MALVA   PYROAR     668 -> DELPHOX    655   (fast special Fire, Kalos)
//
// Each stand-in keeps the leader's specialty type, sits near the original's
// base-stat total and comes from the same region as the ladder, so the fight
// still reads as that region's. Levels are unchanged.
//
// MARSHAL IS THE ONE EXCEPTION, and deliberately. Unova has no pure Fighting
// left once Conkeldurr, Sawk and Mienshao are already on his team and Throh has
// no art: what remains is Gurdurr (BST 405, and Marshal already carries its own
// evolution), the 580-BST musketeer legendaries, or Scrafty -- which GRIMSLEY
// already leads with, so the Fighting specialist would inherit the Dark
// specialist's signature. Hariyama is Hoenn but is what Throh actually was: a
// slow, enormously bulky pure-Fighting grappler, HP 144 to Throh's 120.
//
// This REVERSES the earlier decision to keep art-less mons and let them draw as
// numbers. That was defensible while it affected mid-team slots; a gym leader
// leading with a number is not. roster_test now fails if any team contains a
// species from noart.h, so a future generation cannot reintroduce this quietly.
// It does mean Unova no longer matches B2W2 exactly -- and verify_rosters.py
// cannot catch that, since there is no Gen 5 decomp to diff against.

#define TRAINER_TEAM_MAX 6
// The ladders are levelled to this game's curve, where 100 is the ceiling.
#define MAX_TRAINER_LEVEL 100

struct TrainerMon {
  uint16_t dex;      // NOT uint8_t: Hoenn runs to 386
  uint8_t level;
};

struct Trainer {
  const char *name;
  const char *place;   // gym town, or the Elite 4 room
  uint8_t type;        // the type they specialise in, for the UI accent
  uint8_t count;
  TrainerMon team[TRAINER_TEAM_MAX];
};

// index 0-7 are the badges, 8-11 the Elite 4, 12 the Champion
#define TRAINER_COUNT 13
#define TRAINER_GYMS 8
#define TRAINER_ELITE4 4

static const Trainer TRAINERS_KANTO[TRAINER_COUNT] = {
  { "BROCK",    "PEWTER",    T_ROCK,     2, { {74,12},{95,14} } },
  { "MISTY",    "CERULEAN",  T_WATER,    2, { {120,18},{121,21} } },
  { "LT SURGE", "VERMILION", T_ELECTRIC, 3, { {100,21},{25,18},{26,24} } },
  { "ERIKA",    "CELADON",   T_GRASS,    3, { {71,29},{114,24},{45,29} } },
  { "KOGA",     "FUCHSIA",   T_POISON,   4, { {109,37},{89,39},{109,37},{110,43} } },
  { "SABRINA",  "SAFFRON",   T_PSYCHIC,  4, { {64,38},{122,37},{49,38},{65,43} } },
  { "BLAINE",   "CINNABAR",  T_FIRE,     4, { {58,42},{77,40},{78,42},{59,47} } },
  { "GIOVANNI", "VIRIDIAN",  T_GROUND,   5, { {111,45},{51,42},{31,44},{34,45},{112,50} } },
  { "LORELEI",  "ELITE 4",   T_ICE,      5, { {87,52},{91,51},{80,52},{124,54},{131,54} } },
  { "BRUNO",    "ELITE 4",   T_FIGHTING, 5, { {95,51},{107,53},{106,53},{95,54},{68,56} } },
  { "AGATHA",   "ELITE 4",   T_GHOST,    5, { {94,54},{42,54},{93,53},{24,56},{94,58} } },
  { "LANCE",    "ELITE 4",   T_DRAGON,   5, { {130,56},{148,54},{148,54},{142,58},{149,60} } },
  { "RIVAL",    "CHAMPION",  T_NORMAL,   6, { {18,61},{65,59},{112,61},{59,63},{103,61},{9,65} } },
};


// Johto: Gold/Silver/Crystal. The levels run lower than Kanto's early on and
// the Elite 4 sits at 40-50, which lands a Johto run comfortably inside a
// three-day life the same way Kanto's does.
static const Trainer TRAINERS_JOHTO[TRAINER_COUNT] = {
  { "FALKNER",  "VIOLET",     T_FLYING,   2, { {16,7},{17,9} } },
  { "BUGSY",    "AZALEA",     T_BUG,      3, { {11,14},{14,14},{123,16} } },
  { "WHITNEY",  "GOLDENROD",  T_NORMAL,   2, { {35,18},{241,20} } },
  { "MORTY",    "ECRUTEAK",   T_GHOST,    4, { {92,21},{93,21},{94,25},{93,23} } },
  { "CHUCK",    "CIANWOOD",   T_FIGHTING, 2, { {57,27},{62,30} } },
  { "JASMINE",  "OLIVINE",    T_STEEL,    3, { {81,30},{81,30},{208,35} } },
  { "PRYCE",    "MAHOGANY",   T_ICE,      3, { {86,27},{87,29},{221,31} } },
  { "CLAIR",    "BLACKTHORN", T_DRAGON,   4, { {148,37},{148,37},{148,37},{230,40} } },
  { "WILL",     "ELITE 4",    T_PSYCHIC,  5, { {178,40},{124,41},{103,41},{80,41},{178,42} } },
  { "KOGA",     "ELITE 4",    T_POISON,   5, { {168,40},{49,41},{205,43},{89,42},{169,44} } },
  { "BRUNO",    "ELITE 4",    T_FIGHTING, 5, { {237,42},{106,42},{107,42},{95,43},{68,46} } },
  { "KAREN",    "ELITE 4",    T_DARK,     5, { {197,42},{45,42},{94,45},{198,44},{229,47} } },
  { "LANCE",    "CHAMPION",   T_DRAGON,   6, { {130,44},{149,47},{149,47},{142,46},{6,46},{149,50} } },
};

// Hoenn: EMERALD, consistently. That choice follows from Juan being the eighth
// leader -- in Ruby/Sapphire that seat is Wallace's and Steven is the champion,
// while in Emerald Juan takes the gym and Wallace the title. Mixing the two
// would have given a ladder that exists in neither game. Emerald's Steven is a
// post-game rematch at level 77 and is deliberately not here.
static const Trainer TRAINERS_HOENN[TRAINER_COUNT] = {
  { "ROXANNE",  "RUSTBORO",   T_ROCK,     3, { {74,12},{74,12},{299,15} } },
  { "BRAWLY",   "DEWFORD",    T_FIGHTING, 3, { {66,16},{307,16},{296,19} } },
  { "WATTSON",  "MAUVILLE",   T_ELECTRIC, 4, { {100,20},{309,20},{82,22},{310,24} } },
  { "FLANNERY", "LAVARIDGE",  T_FIRE,     4, { {322,24},{218,24},{323,26},{324,29} } },
  { "NORMAN",   "PETALBURG",  T_NORMAL,   4, { {327,27},{288,27},{264,29},{289,31} } },
  { "WINONA",   "FORTREE",    T_FLYING,   5, { {333,29},{357,29},{279,30},{227,31},{334,33} } },
  { "TATE",     "MOSSDEEP",   T_PSYCHIC,  4, { {344,41},{178,41},{337,42},{338,42} } },
  { "JUAN",     "SOOTOPOLIS", T_WATER,    5, { {370,41},{340,41},{364,43},{342,43},{230,46} } },
  { "SIDNEY",   "ELITE 4",    T_DARK,     5, { {262,46},{275,48},{332,46},{342,48},{359,49} } },
  { "PHOEBE",   "ELITE 4",    T_GHOST,    5, { {356,48},{354,49},{302,50},{354,49},{356,51} } },
  { "GLACIA",   "ELITE 4",    T_ICE,      5, { {364,50},{362,50},{364,52},{362,52},{365,53} } },
  { "DRAKE",    "ELITE 4",    T_DRAGON,   5, { {372,52},{334,54},{230,53},{330,53},{373,55} } },
  { "WALLACE",  "CHAMPION",   T_WATER,    6, { {321,57},{73,55},{272,56},{340,56},{130,56},{350,58} } },
};

// One ladder per region, in the same order as REGIONS in dex.h.
struct TrainerSet {
  const Trainer *list;
  const char *region;
};
// SINNOH -- PLATINUM, and the order matters: Fantina is the THIRD gym here,
// where Diamond/Pearl put her fifth. Taken from pret/pokeplatinum's own
// res/trainers/data/*.json (rematches are separate files there, so unlike
// Crystal and Emerald there is no "first party" ambiguity to get wrong).
//
// The level ramp is what pins the order: 14/22/26/32/37/41/44/50 is monotonic
// only with Fantina third, and roster_test fails a leader 8+ levels below the
// previous -- the D/P order trips it (Fantina 26 straight after Wake 37).
static const Trainer TRAINERS_SINNOH[TRAINER_COUNT] = {
  { "ROARK",   "OREBURGH",  T_ROCK,     3, { {74,12},{95,12},{408,14} } },
  { "GARDENIA","ETERNA",    T_GRASS,    3, { {387,20},{421,20},{407,22} } },
  { "FANTINA", "HEARTHOME", T_GHOST,    3, { {355,24},{93,24},{429,26} } },
  { "MAYLENE", "VEILSTONE", T_FIGHTING, 3, { {307,28},{67,29},{448,32} } },
  { "WAKE",    "PASTORIA",  T_WATER,    3, { {130,33},{195,34},{419,37} } },
  { "BYRON",   "CANALAVE",  T_STEEL,    3, { {82,37},{208,38},{411,41} } },
  { "CANDICE", "SNOWPOINT", T_ICE,      4, { {215,40},{221,40},{460,42},{478,44} } },
  { "VOLKNER", "SUNYSHORE", T_ELECTRIC, 4, { {135,46},{26,46},{405,48},{466,50} } },
  { "AARON",   "ELITE 4",   T_BUG,      5, { {469,49},{212,49},{416,50},{214,51},{452,53} } },
  { "BERTHA",  "ELITE 4",   T_GROUND,   5, { {340,50},{472,53},{450,52},{76,52},{464,55} } },
  { "FLINT",   "ELITE 4",   T_FIRE,     5, { {229,52},{136,55},{78,53},{392,55},{467,57} } },
  { "LUCIAN",  "ELITE 4",   T_PSYCHIC,  5, { {122,53},{196,55},{437,54},{65,56},{475,59} } },
  { "CYNTHIA", "CHAMPION",  T_DRAGON,   6, { {442,58},{407,58},{468,60},{448,60},{350,58},{445,62} } },
};

// UNOVA -- BLACK 2 / WHITE 2, and *** NOT VERIFIED AGAINST A DISASSEMBLY ***.
//
// THIS ONE IS DIFFERENT FROM THE OTHER FOUR. Kanto, Johto, Hoenn and Sinnoh are
// checked by tools/verify_rosters.py against pokered/pokecrystal/pokeemerald/
// pokeplatinum -- the games' own tables. pret has no Gen 5 disassembly (their
// DS work stops at Platinum) and no machine-readable substitute was found, so
// this ladder is written from knowledge. That is EXACTLY how Johto and Hoenn
// were first written, and verify_rosters.py later found ten errors in them,
// including two trainers carrying the wrong game's team entirely. Treat every
// level here as approximate until a source exists.
//
// B2W2 rather than Black/White because BW has two AMBIGUOUS leaders: the
// Striaton trio (which of Cilan/Chili/Cress you face depends on your starter)
// and Drayden-or-Iris (version). B2W2 has neither, and a single fixed ladder
// cannot represent a choice.
//
// Zebstrika (523), Throh (538) and Carracosta (565) have no sprite upstream and
// will draw as dex numbers. Kept faithful rather than substituted -- see
// noart.h; they are barred from the EGG POOL, not from existing.
static const Trainer TRAINERS_UNOVA[TRAINER_COUNT] = {
  { "CHEREN",  "ASPERTIA",  T_NORMAL,   2, { {504,11},{506,13} } },
  { "ROXIE",   "VIRBANK",   T_POISON,   2, { {109,16},{544,18} } },
  { "BURGH",   "CASTELIA",  T_BUG,      3, { {541,21},{557,21},{542,23} } },
  { "ELESA",   "NIMBASA",   T_ELECTRIC, 3, { {587,25},{180,25},{596,27} } },  // 523 ZEBSTRIKA: no art
  { "CLAY",    "DRIFTVEIL", T_GROUND,   3, { {552,29},{28,29},{530,31} } },
  { "SKYLA",   "MISTRALTON",T_FLYING,   3, { {528,33},{227,33},{581,35} } },
  { "DRAYDEN", "OPELUCID",  T_DRAGON,   3, { {621,43},{330,43},{612,46} } },
  { "MARLON",  "HUMILAU",   T_WATER,    3, { {537,49},{321,49},{593,51} } },  // 565 CARRACOSTA: no art
  { "SHAUNTAL","ELITE 4",   T_GHOST,    4, { {563,56},{426,56},{623,56},{609,58} } },
  { "GRIMSLEY","ELITE 4",   T_DARK,     4, { {510,56},{560,56},{553,56},{625,58} } },
  { "CAITLIN", "ELITE 4",   T_PSYCHIC,  4, { {518,56},{561,56},{579,56},{576,58} } },
  { "MARSHAL", "ELITE 4",   T_FIGHTING, 4, { {297,56},{539,56},{534,56},{620,58} } },  // 538 THROH: no art
  { "IRIS",    "CHAMPION",  T_DRAGON,   6, { {635,59},{621,57},{306,57},{567,57},{131,57},{612,59} } },
};

// KALOS -- X / Y, and *** NOT VERIFIED AGAINST A DISASSEMBLY ***, for the same
// reason Unova is not: there is no Gen 6 decomp. pret's work stops at the DS
// generation, and Gen 6 is 3DS. Checked by hand against the games as recalled,
// which is precisely the standard that produced ten errors in Johto and Hoenn.
//
// Kalos is the LAST generation whose sprites are 100% complete upstream, which
// is why it was chosen over Alola/Galar/Paldea -- see check_sprites.py.
static const Trainer TRAINERS_KALOS[TRAINER_COUNT] = {
  { "VIOLA",    "SANTALUNE", T_BUG,      2, { {283,10},{666,12} } },
  { "GRANT",    "CYLLAGE",   T_ROCK,     2, { {698,25},{696,25} } },
  { "KORRINA",  "SHALOUR",   T_FIGHTING, 3, { {619,29},{67,28},{701,32} } },
  { "RAMOS",    "COUMARINE", T_GRASS,    3, { {189,30},{70,31},{673,34} } },
  { "CLEMONT",  "LUMIOSE",   T_ELECTRIC, 3, { {587,35},{82,35},{695,37} } },
  { "VALERIE",  "LAVERRE",   T_FAIRY,    3, { {303,38},{122,39},{700,42} } },
  { "OLYMPIA",  "ANISTAR",   T_PSYCHIC,  3, { {561,44},{199,45},{678,48} } },
  { "WULFRIC",  "SNOWBELLE", T_ICE,      3, { {460,56},{615,55},{713,59} } },
  { "MALVA",    "ELITE 4",   T_FIRE,     4, { {655,63},{324,63},{609,63},{663,65} } },  // 668 PYROAR: no art
  { "SIEBOLD",  "ELITE 4",   T_WATER,    4, { {693,63},{130,63},{121,63},{689,65} } },
  { "WIKSTROM", "ELITE 4",   T_STEEL,    4, { {707,63},{476,63},{212,63},{681,65} } },
  { "DRASNA",   "ELITE 4",   T_DRAGON,   4, { {691,63},{621,63},{334,63},{715,65} } },
  { "DIANTHA",  "CHAMPION",  T_FAIRY,    6, { {701,64},{697,65},{699,65},{711,65},{706,66},{282,68} } },
};

// ALOLA -- SUN / MOON, and *** NOT VERIFIED AGAINST A DISASSEMBLY ***: there is
// no Gen 7 decomp either.
//
// ALOLA HAS NO GYMS. It has seven trial captains and four island kahunas, so the
// eight "gym" slots are six captains plus the two kahunas the Elite Four does
// not need, in ISLAND ORDER -- Melemele, Akala, Ula'ula, Poni -- which is also
// the game's own difficulty order. That leaves Hala, Olivia, Acerola and Kahili
// free to be the real Sun/Moon Elite Four, with no trainer appearing twice.
//
// Alola has no badge art anywhere (SteGriff stops at Unova; the Kalos sheet is
// one artist's). BADGE_REGIONS stays 6 while GYM_REGIONS becomes 7, and
// badgeArtFor() returns nullptr for Alola so the win screen and player card draw
// the plain type-coloured medal instead of borrowing another region's badges.
static const Trainer TRAINERS_ALOLA[TRAINER_COUNT] = {
  { "ILIMA",    "MELEMELE",  T_NORMAL,   2, { {734,10},{731,11} } },
  { "LANA",     "AKALA",     T_WATER,    2, { {746,18},{752,20} } },
  { "KIAWE",    "AKALA",     T_FIRE,     2, { {757,22},{776,24} } },
  { "MALLOW",   "AKALA",     T_GRASS,    3, { {753,24},{762,24},{754,26} } },
  { "SOPHOCLES","ULAULA",    T_ELECTRIC, 3, { {737,30},{777,31},{738,33} } },
  { "MINA",     "PONI",      T_FAIRY,    3, { {743,38},{210,38},{764,40} } },
  { "NANU",     "ULAULA",    T_DARK,     3, { {302,42},{552,42},{53,44} } },
  { "HAPU",     "PONI",      T_GROUND,   4, { {623,47},{423,47},{330,49},{750,51} } },
  { "HALA",     "ELITE 4",   T_FIGHTING, 4, { {297,54},{740,54},{57,54},{760,56} } },
  { "OLIVIA",   "ELITE 4",   T_ROCK,     4, { {348,54},{346,54},{526,54},{745,56} } },
  { "ACEROLA",  "ELITE 4",   T_GHOST,    4, { {426,54},{770,54},{478,54},{781,56} } },
  { "KAHILI",   "ELITE 4",   T_FLYING,   4, { {628,54},{701,54},{733,54},{630,56} } },
  { "KUKUI",    "CHAMPION",  T_NORMAL,   6, { {745,57},{38,56},{628,56},{462,56},{143,56},{727,58} } },
};

// GALAR -- SWORD, and *** NOT VERIFIED AGAINST A DISASSEMBLY ***: pret's work
// stops at Platinum, so there is no Gen 8 decomp and nothing for
// verify_rosters.py to diff this against. Written from knowledge, which is
// exactly the standard that produced ten errors in Johto and Hoenn before a
// source existed for them. Treat every level here as approximate.
//
// SWORD RATHER THAN SHIELD, because Galar has TWO version-exclusive gym slots --
// Bea/Allister at Stow-on-Side and Gordie/Melony at Circhester -- and a single
// fixed ladder cannot represent a choice. Same reasoning that picked B2W2 over
// Black/White for Unova. Sword gives Bea (Fighting) and Gordie (Rock).
//
// GALAR HAS NO ELITE FOUR. It has the Champion Cup, a tournament, so the four
// seats are filled by the Champion-rank trainers you actually face on the way to
// Leon: Marnie and Bede, plus the two leaders Sword does not use -- Allister and
// Melony. No trainer appears twice, and the types stay distinct. Same approach as
// Alola, which has no gyms either.
//
// ONE SUBSTITUTION, and it is recorded rather than silent:
//   GORDIE  COALOSSAL 839 -> DREDNAW 834   (Rock, Galar, no sprite upstream)
// Drednaw is Water/Rock rather than pure Rock, but it is Galar's own, it is the
// right power level, and Stonjourner already holds the pure-Rock ace slot.
// noart.h bars nine Galar species in total and roster_test fails the build if any
// of them reaches a team, so this cannot regress quietly.
static const Trainer TRAINERS_GALAR[TRAINER_COUNT] = {
  { "MILO",     "TURFFIELD",  T_GRASS,    2, { {829,19},{830,20} } },
  { "NESSA",    "HULBURY",    T_WATER,    3, { {118,22},{846,23},{834,24} } },
  { "KABU",     "MOTOSTOKE",  T_FIRE,     3, { {38,25},{59,26},{851,27} } },
  { "BEA",      "STOW-ON-SIDE",T_FIGHTING,4, { {237,34},{675,35},{68,35},{865,36} } },
  { "OPAL",     "BALLONLEA",  T_FAIRY,    4, { {110,36},{303,37},{468,37},{869,38} } },
  { "GORDIE",   "CIRCHESTER", T_ROCK,     4, { {689,40},{213,40},{834,41},{874,42} } },  // 839 COALOSSAL: no art
  { "PIERS",    "SPIKEMUTH",  T_DARK,     4, { {560,44},{687,44},{435,45},{862,46} } },
  { "RAIHAN",   "HAMMERLOCKE",T_DRAGON,   4, { {844,46},{526,47},{330,47},{884,48} } },
  { "MARNIE",   "ELITE 4",    T_DARK,     4, { {510,47},{454,47},{877,48},{861,49} } },
  { "ALLISTER", "ELITE 4",    T_GHOST,    4, { {562,48},{778,48},{94,49},{864,50} } },
  { "MELONY",   "ELITE 4",    T_ICE,      4, { {873,48},{555,49},{131,49},{875,50} } },
  { "BEDE",     "ELITE 4",    T_FAIRY,    4, { {700,49},{282,50},{78,50},{858,51} } },
  { "LEON",     "CHAMPION",   T_FIRE,     6, { {681,52},{612,53},{537,53},{464,54},{887,55},{6,56} } },
};

// PALDEA -- SCARLET / VIOLET, and *** NOT VERIFIED AGAINST A DISASSEMBLY ***,
// for the same reason as Galar: there is no Gen 9 decomp.
//
// LARRY IS THE PROBLEM CASE, and canon causes it: he is both the Normal gym
// leader at Medali AND the Flying member of the Elite Four. A ladder is walked in
// order, so the same name twice reads as a bug rather than as a joke. He keeps
// the GYM seat, and the fourth Elite Four chair goes to PENNY -- a genuinely
// Champion-rank trainer in the same game, with a team of her own that overlaps
// nobody else's. Every trainer here appears exactly once.
//
// ONE SUBSTITUTION:
//   TULIP   ESPATHRA 956 -> ARMAROUGE 936   (Psychic, Paldea, no sprite upstream)
// Espathra is also Geeta's lead in canon; hers falls back to FARIGIRAF 981, which
// is Paldea's other Psychic with art. That does mean Farigiraf appears on both
// Tulip's team and Geeta's -- deliberate, and no worse than Kanto, where Koga
// fields two Weezing and Bruno two Machoke.
//
// noart.h bars TWENTY Paldea species, by far the most of any region, which is why
// several obvious picks are absent.
static const Trainer TRAINERS_PALDEA[TRAINER_COUNT] = {
  { "KATY",     "CORTONDO",   T_BUG,      3, { {917,14},{919,14},{216,15} } },
  { "BRASSIUS", "ARTAZON",    T_GRASS,    3, { {548,16},{928,16},{185,17} } },
  { "IONO",     "LEVINCIA",   T_ELECTRIC, 4, { {940,23},{404,23},{429,24},{939,24} } },
  { "KOFU",     "CASCARRAFA", T_WATER,    3, { {961,29},{740,29},{976,30} } },
  { "LARRY",    "MEDALI",     T_NORMAL,   3, { {775,35},{398,35},{982,36} } },
  { "RYME",     "MONTENEVERA",T_GHOST,    4, { {354,41},{778,41},{849,41},{972,42} } },
  { "TULIP",    "ALFORNADA",  T_PSYCHIC,  4, { {671,44},{282,44},{936,44},{981,45} } },  // 956 ESPATHRA: no art
  { "GRUSHA",   "GLASEADO",   T_ICE,      4, { {873,47},{614,47},{334,47},{975,48} } },
  { "RIKA",     "ELITE 4",    T_GROUND,   5, { {340,57},{323,57},{232,57},{51,57},{980,58} } },
  { "POPPY",    "ELITE 4",    T_STEEL,    5, { {879,58},{437,58},{823,58},{462,58},{959,59} } },
  { "HASSEL",   "ELITE 4",    T_DRAGON,   5, { {715,59},{691,59},{612,59},{841,59},{998,60} } },
  { "PENNY",    "ELITE 4",    T_DARK,     5, { {134,60},{135,60},{136,60},{700,60},{197,61} } },
  { "GEETA",    "CHAMPION",   T_NORMAL,   6, { {981,61},{713,61},{976,62},{673,62},{970,62},{983,63} } },
};

// NINE LADDERS. The badge masks in pet.h are sized from this
// (badgesX[GYM_REGIONS - 1]), and growing them is SAFE on an existing save: the
// array length travels in the player checkpoint's header, so a stored six-region
// array lands in the front of the new eight-region one and Galar and Paldea start
// empty. Each mask is a uint16_t with only TRAINER_COUNT (13) of its 16 bits used,
// so nothing overflows either.
//
// BADGE_REGIONS STAYS 6. The upstream badge sheet stops at Unova and the Kalos
// one is a single artist's, so Alola, Galar and Paldea have no badge art at all --
// badgeArtFor() returns nullptr for them and both the win screen and the player
// card draw a plain type-coloured medal instead. That is deliberate: an honest
// blank beats borrowing another region's badges, which would claim you had won
// something you had not.
#define GYM_REGIONS 9
static const TrainerSet TRAINER_SETS[GYM_REGIONS] = {
  { TRAINERS_KANTO,  "KANTO" },
  { TRAINERS_JOHTO,  "JOHTO" },
  { TRAINERS_HOENN,  "HOENN" },
  { TRAINERS_SINNOH, "SINNOH" },
  { TRAINERS_UNOVA,  "UNOVA" },
  { TRAINERS_KALOS,  "KALOS" },
  { TRAINERS_ALOLA,  "ALOLA" },
  { TRAINERS_GALAR,  "GALAR" },
  { TRAINERS_PALDEA, "PALDEA" },
};

// Hard mode reruns the same ladder with perfect IVs and a smarter AI, so the
// teams need no second table -- only the difficulty flag changes.
#define HARD_IV 31
#define EASY_IV 16
