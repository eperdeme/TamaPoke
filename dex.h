#pragma once
#include <stdint.h>

// GENERADO por tools/gen_dex.py desde tools/dex_data.py - no editar

#define DEX_COUNT 1025
#define DEX_EEVEE 133
// EEVEE's branches. DexEntry holds ONE evolvesTo (134), so the other
// seven cannot live in the table. Generated from EEVEE_BRANCHES in
// dex_data.py, which is also what marks all eight evolution-only.
#define EEVEE_EVO_COUNT 8
static const int16_t EEVEE_EVOS[EEVEE_EVO_COUNT] = { 134, 135, 136, 196, 197, 470, 471, 700 };

// The 18 current types. See tools/dex_types.py for why this game uses the
// modern chart rather than the Gen 1 one.
enum PkType : uint8_t {
  T_NORMAL, T_FIRE, T_WATER, T_ELECTRIC, T_GRASS, T_ICE, T_FIGHTING, T_POISON, T_GROUND, T_FLYING, T_PSYCHIC, T_BUG, T_ROCK, T_GHOST, T_DRAGON, T_DARK, T_STEEL, T_FAIRY,
  T_NONE = 255
};
#define TYPE_COUNT 18

// Type chart in TENTHS (0 immune, 5 not-very, 10 neutral, 20 super).
// Multiplying the two defender columns gives a percentage directly:
// 20*20 = 400 (4x), 10*10 = 100 (1x), 5*10 = 50 (0.5x).
static const uint8_t TYPE_FX[TYPE_COUNT][TYPE_COUNT] = {
  { 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,  5,  0, 10, 10,  5, 10 },  // normal
  { 10,  5,  5, 10, 20, 20, 10, 10, 10, 10, 10, 20,  5, 10,  5, 10, 20, 10 },  // fire
  { 10, 20,  5, 10,  5, 10, 10, 10, 20, 10, 10, 10, 20, 10,  5, 10, 10, 10 },  // water
  { 10, 10, 20,  5,  5, 10, 10, 10,  0, 20, 10, 10, 10, 10,  5, 10, 10, 10 },  // electric
  { 10,  5, 20, 10,  5, 10, 10,  5, 20,  5, 10,  5, 20, 10,  5, 10,  5, 10 },  // grass
  { 10,  5,  5, 10, 20,  5, 10, 10, 20, 20, 10, 10, 10, 10, 20, 10,  5, 10 },  // ice
  { 20, 10, 10, 10, 10, 20, 10,  5, 10,  5,  5,  5, 20,  0, 10, 20, 20,  5 },  // fighting
  { 10, 10, 10, 10, 20, 10, 10,  5,  5, 10, 10, 10,  5,  5, 10, 10,  0, 20 },  // poison
  { 10, 20, 10, 20,  5, 10, 10, 20, 10,  0, 10,  5, 20, 10, 10, 10, 20, 10 },  // ground
  { 10, 10, 10,  5, 20, 10, 20, 10, 10, 10, 10, 20,  5, 10, 10, 10,  5, 10 },  // flying
  { 10, 10, 10, 10, 10, 10, 20, 20, 10, 10,  5, 10, 10, 10, 10,  0,  5, 10 },  // psychic
  { 10,  5, 10, 10, 20, 10,  5,  5, 10,  5, 20, 10, 10,  5, 10, 20,  5,  5 },  // bug
  { 10, 20, 10, 10, 10, 20,  5, 10,  5, 20, 10, 20, 10, 10, 10, 10,  5, 10 },  // rock
  {  0, 10, 10, 10, 10, 10, 10, 10, 10, 10, 20, 10, 10, 20, 10,  5, 10, 10 },  // ghost
  { 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 20, 10,  5,  0 },  // dragon
  { 10, 10, 10, 10, 10, 10,  5, 10, 10, 10, 20, 10, 10, 20, 10,  5, 10,  5 },  // dark
  { 10,  5,  5,  5, 10, 20, 10, 10, 10, 10, 10, 10, 20, 10, 10, 10,  5, 20 },  // steel
  { 10,  5, 10, 10, 10, 10, 20,  5, 10, 10, 10, 10, 10, 10, 20, 20,  5, 10 },  // fairy
};

// rareza: 0 = solo por evolucion, 1 = comun, 2 = raro, 3 = legendario
enum : uint8_t { R_EVO = 0, R_COMUN, R_RARO, R_LEGENDARIO };

struct DexEntry {
  const char *name;
  uint16_t evolvesTo;   // numero de dex, 0 = forma final (>255 con gen 2/3)
  uint8_t evolveLevel;
  uint8_t rarity;       // sale de huevo si > 0
  uint16_t accent;      // color RGB565 del tipo para la UI
  uint8_t bHp, bAtk, bDef, bSpe;  // base stats actuales (PokeAPI), no los de gen 1
  uint8_t bSpA, bSpD;   // ataque/defensa especial: el reparto fisico-especial
                        // vive en la especie, el individuo solo tira 4 IV
  uint8_t biome;        // 0 pradera 1 playa 2 bosque 3 volcan 4 montana 5 nieve
  uint8_t type1, type2;  // current typing; type2 = T_NONE if single-typed
};

static const DexEntry DEX_TBL[DEX_COUNT + 1] = {
  { "?", 0, 0, 0, 0x2946, 50, 50, 50, 50, 50, 50, 0 },  // 0: sin usar
  { "BULBASAUR", 2, 16, R_COMUN, 0x3C49, 45, 49, 49, 45, 65, 65, 2, T_GRASS, T_POISON },  // 1 grass/poison
  { "IVYSAUR", 3, 32, R_EVO, 0x3C49, 60, 62, 63, 60, 80, 80, 2, T_GRASS, T_POISON },  // 2 grass/poison
  { "VENUSAUR", 0, 0, R_EVO, 0x3C49, 80, 82, 83, 80, 100, 100, 2, T_GRASS, T_POISON },  // 3 grass/poison
  { "CHARMANDER", 5, 16, R_COMUN, 0xEA87, 39, 52, 43, 65, 60, 50, 3, T_FIRE, T_NONE },  // 4 fire
  { "CHARMELEON", 6, 36, R_EVO, 0xEA87, 58, 64, 58, 80, 80, 65, 3, T_FIRE, T_NONE },  // 5 fire
  { "CHARIZARD", 0, 0, R_EVO, 0xEA87, 78, 84, 78, 100, 109, 85, 3, T_FIRE, T_FLYING },  // 6 fire/flying
  { "SQUIRTLE", 8, 16, R_COMUN, 0x4C98, 44, 48, 65, 43, 50, 64, 1, T_WATER, T_NONE },  // 7 water
  { "WARTORTLE", 9, 36, R_EVO, 0x4C98, 59, 63, 80, 58, 65, 80, 1, T_WATER, T_NONE },  // 8 water
  { "BLASTOISE", 0, 0, R_EVO, 0x4C98, 79, 83, 100, 78, 85, 105, 1, T_WATER, T_NONE },  // 9 water
  { "CATERPIE", 11, 7, R_COMUN, 0x7CC4, 45, 30, 35, 45, 20, 20, 2, T_BUG, T_NONE },  // 10 bug
  { "METAPOD", 12, 10, R_EVO, 0x7CC4, 50, 20, 55, 30, 25, 25, 2, T_BUG, T_NONE },  // 11 bug
  { "BUTTERFREE", 0, 0, R_EVO, 0x7CC4, 60, 45, 50, 70, 90, 80, 2, T_BUG, T_FLYING },  // 12 bug/flying
  { "WEEDLE", 14, 7, R_COMUN, 0x7CC4, 40, 35, 30, 50, 20, 20, 2, T_BUG, T_POISON },  // 13 bug/poison
  { "KAKUNA", 15, 10, R_EVO, 0x7CC4, 45, 25, 50, 35, 25, 25, 2, T_BUG, T_POISON },  // 14 bug/poison
  { "BEEDRILL", 0, 0, R_EVO, 0x7CC4, 65, 90, 40, 75, 45, 80, 2, T_BUG, T_POISON },  // 15 bug/poison
  { "PIDGEY", 17, 18, R_COMUN, 0x8C4D, 40, 45, 40, 56, 35, 35, 0, T_NORMAL, T_FLYING },  // 16 normal/flying
  { "PIDGEOTTO", 18, 36, R_EVO, 0x8C4D, 63, 60, 55, 71, 50, 50, 0, T_NORMAL, T_FLYING },  // 17 normal/flying
  { "PIDGEOT", 0, 0, R_EVO, 0x8C4D, 83, 80, 75, 101, 70, 70, 0, T_NORMAL, T_FLYING },  // 18 normal/flying
  { "RATTATA", 20, 20, R_COMUN, 0x8C4D, 30, 56, 35, 72, 25, 35, 0, T_NORMAL, T_NONE },  // 19 normal
  { "RATICATE", 0, 0, R_EVO, 0x8C4D, 55, 81, 60, 97, 50, 70, 0, T_NORMAL, T_NONE },  // 20 normal
  { "SPEAROW", 22, 20, R_COMUN, 0x8C4D, 40, 60, 30, 70, 31, 31, 0, T_NORMAL, T_FLYING },  // 21 normal/flying
  { "FEAROW", 0, 0, R_EVO, 0x8C4D, 65, 90, 65, 100, 61, 61, 0, T_NORMAL, T_FLYING },  // 22 normal/flying
  { "EKANS", 24, 22, R_COMUN, 0x8A73, 35, 60, 44, 55, 40, 54, 0, T_POISON, T_NONE },  // 23 poison
  { "ARBOK", 0, 0, R_EVO, 0x8A73, 60, 95, 69, 80, 65, 79, 0, T_POISON, T_NONE },  // 24 poison
  { "PIKACHU", 26, 30, R_EVO, 0xBCA1, 35, 55, 40, 90, 50, 50, 0, T_ELECTRIC, T_NONE },  // 25 electric
  { "RAICHU", 0, 0, R_EVO, 0xBCA1, 60, 90, 55, 110, 90, 80, 0, T_ELECTRIC, T_NONE },  // 26 electric
  { "SANDSHREW", 28, 22, R_COMUN, 0xB447, 50, 75, 85, 40, 20, 30, 4, T_GROUND, T_NONE },  // 27 ground
  { "SANDSLASH", 0, 0, R_EVO, 0xB447, 75, 100, 110, 65, 45, 55, 4, T_GROUND, T_NONE },  // 28 ground
  { "NIDORAN H", 30, 16, R_COMUN, 0x8A73, 55, 47, 52, 41, 40, 40, 0, T_POISON, T_NONE },  // 29 poison
  { "NIDORINA", 31, 30, R_EVO, 0x8A73, 70, 62, 67, 56, 55, 55, 0, T_POISON, T_NONE },  // 30 poison
  { "NIDOQUEEN", 0, 0, R_EVO, 0x8A73, 90, 92, 87, 76, 75, 85, 0, T_POISON, T_GROUND },  // 31 poison/ground
  { "NIDORAN M", 33, 16, R_COMUN, 0x8A73, 46, 57, 40, 50, 40, 40, 0, T_POISON, T_NONE },  // 32 poison
  { "NIDORINO", 34, 30, R_EVO, 0x8A73, 61, 72, 57, 65, 55, 55, 0, T_POISON, T_NONE },  // 33 poison
  { "NIDOKING", 0, 0, R_EVO, 0x8A73, 81, 102, 77, 85, 85, 75, 0, T_POISON, T_GROUND },  // 34 poison/ground
  { "CLEFAIRY", 36, 30, R_EVO, 0x8C4D, 70, 45, 48, 35, 60, 65, 0, T_FAIRY, T_NONE },  // 35 fairy
  { "CLEFABLE", 0, 0, R_EVO, 0x8C4D, 95, 70, 73, 60, 95, 90, 0, T_FAIRY, T_NONE },  // 36 fairy
  { "VULPIX", 38, 30, R_COMUN, 0xEA87, 38, 41, 40, 65, 50, 65, 3, T_FIRE, T_NONE },  // 37 fire
  { "NINETALES", 0, 0, R_EVO, 0xEA87, 73, 76, 75, 100, 81, 100, 3, T_FIRE, T_NONE },  // 38 fire
  { "JIGGLYPUFF", 40, 30, R_EVO, 0x8C4D, 115, 45, 20, 20, 45, 25, 0, T_NORMAL, T_FAIRY },  // 39 normal/fairy
  { "WIGGLYTUFF", 0, 0, R_EVO, 0x8C4D, 140, 70, 45, 45, 85, 50, 0, T_NORMAL, T_FAIRY },  // 40 normal/fairy
  { "ZUBAT", 42, 22, R_COMUN, 0x8A73, 40, 45, 35, 55, 30, 40, 0, T_POISON, T_FLYING },  // 41 poison/flying
  { "GOLBAT", 169, 25, R_EVO, 0x8A73, 75, 80, 70, 90, 65, 75, 0, T_POISON, T_FLYING },  // 42 poison/flying
  { "ODDISH", 44, 21, R_COMUN, 0x3C49, 45, 50, 55, 30, 75, 65, 2, T_GRASS, T_POISON },  // 43 grass/poison
  { "GLOOM", 45, 36, R_EVO, 0x3C49, 60, 65, 70, 40, 85, 75, 2, T_GRASS, T_POISON },  // 44 grass/poison
  { "VILEPLUME", 0, 0, R_EVO, 0x3C49, 75, 80, 85, 50, 110, 90, 2, T_GRASS, T_POISON },  // 45 grass/poison
  { "PARAS", 47, 24, R_COMUN, 0x7CC4, 35, 70, 55, 25, 45, 55, 2, T_BUG, T_GRASS },  // 46 bug/grass
  { "PARASECT", 0, 0, R_EVO, 0x7CC4, 60, 95, 80, 30, 60, 80, 2, T_BUG, T_GRASS },  // 47 bug/grass
  { "VENONAT", 49, 31, R_COMUN, 0x7CC4, 60, 55, 50, 45, 40, 55, 2, T_BUG, T_POISON },  // 48 bug/poison
  { "VENOMOTH", 0, 0, R_EVO, 0x7CC4, 70, 65, 60, 90, 90, 75, 2, T_BUG, T_POISON },  // 49 bug/poison
  { "DIGLETT", 51, 26, R_COMUN, 0xB447, 10, 55, 25, 95, 35, 45, 4, T_GROUND, T_NONE },  // 50 ground
  { "DUGTRIO", 0, 0, R_EVO, 0xB447, 35, 100, 50, 120, 50, 70, 4, T_GROUND, T_NONE },  // 51 ground
  { "MEOWTH", 53, 28, R_COMUN, 0x8C4D, 40, 45, 35, 90, 40, 40, 0, T_NORMAL, T_NONE },  // 52 normal
  { "PERSIAN", 0, 0, R_EVO, 0x8C4D, 65, 70, 60, 115, 65, 65, 0, T_NORMAL, T_NONE },  // 53 normal
  { "PSYDUCK", 55, 33, R_COMUN, 0x4C98, 50, 52, 48, 55, 65, 50, 1, T_WATER, T_NONE },  // 54 water
  { "GOLDUCK", 0, 0, R_EVO, 0x4C98, 80, 82, 78, 85, 95, 80, 1, T_WATER, T_NONE },  // 55 water
  { "MANKEY", 57, 28, R_COMUN, 0xA2A5, 40, 80, 35, 70, 35, 45, 0, T_FIGHTING, T_NONE },  // 56 fighting
  { "PRIMEAPE", 979, 30, R_EVO, 0xA2A5, 65, 105, 60, 95, 60, 70, 0, T_FIGHTING, T_NONE },  // 57 fighting
  { "GROWLITHE", 59, 30, R_RARO, 0xEA87, 55, 70, 45, 60, 70, 50, 3, T_FIRE, T_NONE },  // 58 fire
  { "ARCANINE", 0, 0, R_EVO, 0xEA87, 90, 110, 80, 95, 100, 80, 3, T_FIRE, T_NONE },  // 59 fire
  { "POLIWAG", 61, 25, R_COMUN, 0x4C98, 40, 50, 40, 90, 40, 40, 1, T_WATER, T_NONE },  // 60 water
  { "POLIWHIRL", 62, 36, R_EVO, 0x4C98, 65, 65, 65, 90, 50, 50, 1, T_WATER, T_NONE },  // 61 water
  { "POLIWRATH", 0, 0, R_EVO, 0x4C98, 90, 95, 95, 70, 70, 90, 1, T_WATER, T_FIGHTING },  // 62 water/fighting
  { "ABRA", 64, 16, R_COMUN, 0xD28F, 25, 20, 15, 90, 105, 55, 0, T_PSYCHIC, T_NONE },  // 63 psychic
  { "KADABRA", 65, 40, R_EVO, 0xD28F, 40, 35, 30, 105, 120, 70, 0, T_PSYCHIC, T_NONE },  // 64 psychic
  { "ALAKAZAM", 0, 0, R_EVO, 0xD28F, 55, 50, 45, 120, 135, 95, 0, T_PSYCHIC, T_NONE },  // 65 psychic
  { "MACHOP", 67, 28, R_COMUN, 0xA2A5, 70, 80, 50, 35, 35, 35, 0, T_FIGHTING, T_NONE },  // 66 fighting
  { "MACHOKE", 68, 40, R_EVO, 0xA2A5, 80, 100, 70, 45, 50, 60, 0, T_FIGHTING, T_NONE },  // 67 fighting
  { "MACHAMP", 0, 0, R_EVO, 0xA2A5, 90, 130, 80, 55, 65, 85, 0, T_FIGHTING, T_NONE },  // 68 fighting
  { "BELLSPROUT", 70, 21, R_COMUN, 0x3C49, 50, 75, 35, 40, 70, 30, 2, T_GRASS, T_POISON },  // 69 grass/poison
  { "WEEPINBELL", 71, 36, R_EVO, 0x3C49, 65, 90, 50, 55, 85, 45, 2, T_GRASS, T_POISON },  // 70 grass/poison
  { "VICTREEBEL", 0, 0, R_EVO, 0x3C49, 80, 105, 65, 70, 100, 70, 2, T_GRASS, T_POISON },  // 71 grass/poison
  { "TENTACOOL", 73, 30, R_COMUN, 0x4C98, 40, 40, 35, 70, 50, 100, 1, T_WATER, T_POISON },  // 72 water/poison
  { "TENTACRUEL", 0, 0, R_EVO, 0x4C98, 80, 70, 65, 100, 80, 120, 1, T_WATER, T_POISON },  // 73 water/poison
  { "GEODUDE", 75, 25, R_COMUN, 0x9407, 40, 80, 100, 20, 30, 30, 4, T_ROCK, T_GROUND },  // 74 rock/ground
  { "GRAVELER", 76, 40, R_EVO, 0x9407, 55, 95, 115, 35, 45, 45, 4, T_ROCK, T_GROUND },  // 75 rock/ground
  { "GOLEM", 0, 0, R_EVO, 0x9407, 80, 120, 130, 45, 55, 65, 4, T_ROCK, T_GROUND },  // 76 rock/ground
  { "PONYTA", 78, 40, R_RARO, 0xEA87, 50, 85, 55, 90, 65, 65, 3, T_FIRE, T_NONE },  // 77 fire
  { "RAPIDASH", 0, 0, R_EVO, 0xEA87, 65, 100, 70, 105, 80, 80, 3, T_FIRE, T_NONE },  // 78 fire
  { "SLOWPOKE", 80, 37, R_COMUN, 0x4C98, 90, 65, 65, 15, 40, 40, 1, T_WATER, T_PSYCHIC },  // 79 water/psychic
  { "SLOWBRO", 0, 0, R_EVO, 0x4C98, 95, 75, 110, 30, 100, 80, 1, T_WATER, T_PSYCHIC },  // 80 water/psychic
  { "MAGNEMITE", 82, 30, R_COMUN, 0xBCA1, 25, 35, 70, 45, 95, 55, 0, T_ELECTRIC, T_STEEL },  // 81 electric/steel
  { "MAGNETON", 462, 30, R_EVO, 0xBCA1, 50, 60, 95, 70, 120, 70, 0, T_ELECTRIC, T_STEEL },  // 82 electric/steel
  { "FARFETCHD", 865, 30, R_RARO, 0x8C4D, 52, 90, 55, 60, 58, 62, 0, T_NORMAL, T_FLYING },  // 83 normal/flying
  { "DODUO", 85, 31, R_COMUN, 0x8C4D, 35, 85, 45, 75, 35, 35, 0, T_NORMAL, T_FLYING },  // 84 normal/flying
  { "DODRIO", 0, 0, R_EVO, 0x8C4D, 60, 110, 70, 110, 60, 60, 0, T_NORMAL, T_FLYING },  // 85 normal/flying
  { "SEEL", 87, 34, R_COMUN, 0x4C98, 65, 45, 55, 45, 45, 70, 1, T_WATER, T_NONE },  // 86 water
  { "DEWGONG", 0, 0, R_EVO, 0x4C98, 90, 70, 80, 70, 70, 95, 1, T_WATER, T_ICE },  // 87 water/ice
  { "GRIMER", 89, 38, R_RARO, 0x8A73, 80, 80, 50, 25, 40, 50, 0, T_POISON, T_NONE },  // 88 poison
  { "MUK", 0, 0, R_EVO, 0x8A73, 105, 105, 75, 50, 65, 100, 0, T_POISON, T_NONE },  // 89 poison
  { "SHELLDER", 91, 30, R_COMUN, 0x4C98, 30, 65, 100, 40, 45, 25, 1, T_WATER, T_NONE },  // 90 water
  { "CLOYSTER", 0, 0, R_EVO, 0x4C98, 50, 95, 180, 70, 85, 45, 1, T_WATER, T_ICE },  // 91 water/ice
  { "GASTLY", 93, 25, R_COMUN, 0x6AD3, 30, 35, 30, 80, 100, 35, 0, T_GHOST, T_POISON },  // 92 ghost/poison
  { "HAUNTER", 94, 40, R_EVO, 0x6AD3, 45, 50, 45, 95, 115, 55, 0, T_GHOST, T_POISON },  // 93 ghost/poison
  { "GENGAR", 0, 0, R_EVO, 0x6AD3, 60, 65, 60, 110, 130, 75, 0, T_GHOST, T_POISON },  // 94 ghost/poison
  { "ONIX", 208, 40, R_RARO, 0x9407, 35, 45, 160, 70, 30, 45, 4, T_ROCK, T_GROUND },  // 95 rock/ground
  { "DROWZEE", 97, 26, R_COMUN, 0xD28F, 60, 48, 45, 42, 43, 90, 0, T_PSYCHIC, T_NONE },  // 96 psychic
  { "HYPNO", 0, 0, R_EVO, 0xD28F, 85, 73, 70, 67, 73, 115, 0, T_PSYCHIC, T_NONE },  // 97 psychic
  { "KRABBY", 99, 28, R_COMUN, 0x4C98, 30, 105, 90, 50, 25, 25, 1, T_WATER, T_NONE },  // 98 water
  { "KINGLER", 0, 0, R_EVO, 0x4C98, 55, 130, 115, 75, 50, 50, 1, T_WATER, T_NONE },  // 99 water
  { "VOLTORB", 101, 30, R_COMUN, 0xBCA1, 40, 30, 50, 100, 55, 55, 0, T_ELECTRIC, T_NONE },  // 100 electric
  { "ELECTRODE", 0, 0, R_EVO, 0xBCA1, 60, 50, 70, 150, 80, 80, 0, T_ELECTRIC, T_NONE },  // 101 electric
  { "EXEGGCUTE", 103, 30, R_COMUN, 0x3C49, 60, 40, 80, 40, 60, 45, 2, T_GRASS, T_PSYCHIC },  // 102 grass/psychic
  { "EXEGGUTOR", 0, 0, R_EVO, 0x3C49, 95, 95, 85, 55, 125, 75, 2, T_GRASS, T_PSYCHIC },  // 103 grass/psychic
  { "CUBONE", 105, 28, R_COMUN, 0xB447, 50, 50, 95, 35, 40, 50, 4, T_GROUND, T_NONE },  // 104 ground
  { "MAROWAK", 0, 0, R_EVO, 0xB447, 60, 80, 110, 45, 50, 80, 4, T_GROUND, T_NONE },  // 105 ground
  { "HITMONLEE", 0, 0, R_EVO, 0xA2A5, 50, 120, 53, 87, 35, 110, 0, T_FIGHTING, T_NONE },  // 106 fighting
  { "HITMONCHAN", 0, 0, R_RARO, 0xA2A5, 50, 105, 79, 76, 35, 110, 0, T_FIGHTING, T_NONE },  // 107 fighting
  { "LICKITUNG", 463, 30, R_RARO, 0x8C4D, 90, 55, 75, 30, 60, 75, 0, T_NORMAL, T_NONE },  // 108 normal
  { "KOFFING", 110, 35, R_COMUN, 0x8A73, 40, 65, 95, 35, 60, 45, 0, T_POISON, T_NONE },  // 109 poison
  { "WEEZING", 0, 0, R_EVO, 0x8A73, 65, 90, 120, 60, 85, 70, 0, T_POISON, T_NONE },  // 110 poison
  { "RHYHORN", 112, 42, R_RARO, 0xB447, 80, 85, 95, 25, 30, 30, 4, T_GROUND, T_ROCK },  // 111 ground/rock
  { "RHYDON", 464, 40, R_EVO, 0xB447, 105, 130, 120, 40, 45, 45, 4, T_GROUND, T_ROCK },  // 112 ground/rock
  { "CHANSEY", 242, 25, R_EVO, 0x8C4D, 250, 5, 5, 50, 35, 105, 0, T_NORMAL, T_NONE },  // 113 normal
  { "TANGELA", 465, 30, R_RARO, 0x3C49, 65, 55, 115, 60, 100, 40, 2, T_GRASS, T_NONE },  // 114 grass
  { "KANGASKHAN", 0, 0, R_RARO, 0x8C4D, 105, 95, 80, 90, 40, 80, 0, T_NORMAL, T_NONE },  // 115 normal
  { "HORSEA", 117, 32, R_COMUN, 0x4C98, 30, 40, 70, 60, 70, 25, 1, T_WATER, T_NONE },  // 116 water
  { "SEADRA", 230, 40, R_EVO, 0x4C98, 55, 65, 95, 85, 95, 45, 1, T_WATER, T_NONE },  // 117 water
  { "GOLDEEN", 119, 33, R_COMUN, 0x4C98, 45, 67, 60, 63, 35, 50, 1, T_WATER, T_NONE },  // 118 water
  { "SEAKING", 0, 0, R_EVO, 0x4C98, 80, 92, 65, 68, 65, 80, 1, T_WATER, T_NONE },  // 119 water
  { "STARYU", 121, 30, R_COMUN, 0x4C98, 30, 45, 55, 85, 70, 55, 1, T_WATER, T_NONE },  // 120 water
  { "STARMIE", 0, 0, R_EVO, 0x4C98, 60, 75, 85, 115, 100, 85, 1, T_WATER, T_PSYCHIC },  // 121 water/psychic
  { "MR. MIME", 866, 42, R_EVO, 0xD28F, 40, 45, 65, 90, 100, 120, 0, T_PSYCHIC, T_FAIRY },  // 122 psychic/fairy
  { "SCYTHER", 212, 40, R_RARO, 0x7CC4, 70, 110, 80, 105, 55, 80, 2, T_BUG, T_FLYING },  // 123 bug/flying
  { "JYNX", 0, 0, R_EVO, 0x4DB8, 65, 50, 35, 95, 115, 95, 5, T_ICE, T_PSYCHIC },  // 124 ice/psychic
  { "ELECTABUZZ", 466, 40, R_EVO, 0xBCA1, 65, 83, 57, 105, 95, 85, 0, T_ELECTRIC, T_NONE },  // 125 electric
  { "MAGMAR", 467, 40, R_EVO, 0xEA87, 65, 95, 57, 93, 100, 85, 3, T_FIRE, T_NONE },  // 126 fire
  { "PINSIR", 0, 0, R_RARO, 0x7CC4, 65, 125, 100, 85, 55, 70, 2, T_BUG, T_NONE },  // 127 bug
  { "TAUROS", 0, 0, R_RARO, 0x8C4D, 75, 100, 95, 110, 40, 70, 0, T_NORMAL, T_NONE },  // 128 normal
  { "MAGIKARP", 130, 20, R_COMUN, 0x4C98, 20, 10, 55, 80, 15, 20, 1, T_WATER, T_NONE },  // 129 water
  { "GYARADOS", 0, 0, R_EVO, 0x4C98, 95, 125, 79, 81, 60, 100, 1, T_WATER, T_FLYING },  // 130 water/flying
  { "LAPRAS", 0, 0, R_RARO, 0x4C98, 130, 85, 80, 60, 85, 95, 1, T_WATER, T_ICE },  // 131 water/ice
  { "DITTO", 0, 0, R_RARO, 0x8C4D, 48, 48, 48, 48, 48, 48, 0, T_NORMAL, T_NONE },  // 132 normal
  { "EEVEE", 134, 30, R_COMUN, 0x8C4D, 55, 55, 50, 55, 45, 65, 0, T_NORMAL, T_NONE },  // 133 normal
  { "VAPOREON", 0, 0, R_EVO, 0x4C98, 130, 65, 60, 65, 110, 95, 1, T_WATER, T_NONE },  // 134 water
  { "JOLTEON", 0, 0, R_EVO, 0xBCA1, 65, 65, 60, 130, 110, 95, 0, T_ELECTRIC, T_NONE },  // 135 electric
  { "FLAREON", 0, 0, R_EVO, 0xEA87, 65, 130, 60, 65, 95, 110, 3, T_FIRE, T_NONE },  // 136 fire
  { "PORYGON", 233, 40, R_RARO, 0x8C4D, 65, 60, 70, 40, 85, 75, 0, T_NORMAL, T_NONE },  // 137 normal
  { "OMANYTE", 139, 40, R_RARO, 0x9407, 35, 40, 100, 35, 90, 55, 1, T_ROCK, T_WATER },  // 138 rock/water
  { "OMASTAR", 0, 0, R_EVO, 0x9407, 70, 60, 125, 55, 115, 70, 1, T_ROCK, T_WATER },  // 139 rock/water
  { "KABUTO", 141, 40, R_RARO, 0x9407, 30, 80, 90, 55, 55, 45, 1, T_ROCK, T_WATER },  // 140 rock/water
  { "KABUTOPS", 0, 0, R_EVO, 0x9407, 60, 115, 105, 80, 65, 70, 1, T_ROCK, T_WATER },  // 141 rock/water
  { "AERODACTYL", 0, 0, R_RARO, 0x9407, 80, 105, 65, 130, 60, 75, 4, T_ROCK, T_FLYING },  // 142 rock/flying
  { "SNORLAX", 0, 0, R_EVO, 0x8C4D, 160, 110, 65, 30, 65, 110, 0, T_NORMAL, T_NONE },  // 143 normal
  { "ARTICUNO", 0, 0, R_LEGENDARIO, 0x4DB8, 90, 85, 100, 85, 95, 125, 5, T_ICE, T_FLYING },  // 144 ice/flying
  { "ZAPDOS", 0, 0, R_LEGENDARIO, 0xBCA1, 90, 90, 85, 100, 125, 90, 0, T_ELECTRIC, T_FLYING },  // 145 electric/flying
  { "MOLTRES", 0, 0, R_LEGENDARIO, 0xEA87, 90, 100, 90, 90, 125, 85, 3, T_FIRE, T_FLYING },  // 146 fire/flying
  { "DRATINI", 148, 30, R_RARO, 0x5A98, 41, 64, 45, 50, 50, 50, 1, T_DRAGON, T_NONE },  // 147 dragon
  { "DRAGONAIR", 149, 55, R_EVO, 0x5A98, 61, 84, 65, 70, 70, 70, 1, T_DRAGON, T_NONE },  // 148 dragon
  { "DRAGONITE", 0, 0, R_EVO, 0x5A98, 91, 134, 95, 80, 100, 100, 1, T_DRAGON, T_FLYING },  // 149 dragon/flying
  { "MEWTWO", 0, 0, R_LEGENDARIO, 0xD28F, 106, 110, 90, 130, 154, 90, 0, T_PSYCHIC, T_NONE },  // 150 psychic
  { "MEW", 0, 0, R_LEGENDARIO, 0xD28F, 100, 100, 100, 100, 100, 100, 0, T_PSYCHIC, T_NONE },  // 151 psychic
  { "CHIKORITA", 153, 16, R_RARO, 0x3C49, 45, 49, 65, 45, 49, 65, 2, T_GRASS, T_NONE },  // 152 grass
  { "BAYLEEF", 154, 32, R_EVO, 0x3C49, 60, 62, 80, 60, 63, 80, 2, T_GRASS, T_NONE },  // 153 grass
  { "MEGANIUM", 0, 0, R_EVO, 0x3C49, 80, 82, 100, 80, 83, 100, 2, T_GRASS, T_NONE },  // 154 grass
  { "CYNDAQUIL", 156, 14, R_RARO, 0xEA87, 39, 52, 43, 65, 60, 50, 3, T_FIRE, T_NONE },  // 155 fire
  { "QUILAVA", 157, 36, R_EVO, 0xEA87, 58, 64, 58, 80, 80, 65, 3, T_FIRE, T_NONE },  // 156 fire
  { "TYPHLOSION", 0, 0, R_EVO, 0xEA87, 78, 84, 78, 100, 109, 85, 3, T_FIRE, T_NONE },  // 157 fire
  { "TOTODILE", 159, 18, R_RARO, 0x4C98, 50, 65, 64, 43, 44, 48, 1, T_WATER, T_NONE },  // 158 water
  { "CROCONAW", 160, 30, R_EVO, 0x4C98, 65, 80, 80, 58, 59, 63, 1, T_WATER, T_NONE },  // 159 water
  { "FERALIGATR", 0, 0, R_EVO, 0x4C98, 85, 105, 100, 78, 79, 83, 1, T_WATER, T_NONE },  // 160 water
  { "SENTRET", 162, 15, R_COMUN, 0x8C4D, 35, 46, 34, 20, 35, 45, 0, T_NORMAL, T_NONE },  // 161 normal
  { "FURRET", 0, 0, R_EVO, 0x8C4D, 85, 76, 64, 90, 45, 55, 0, T_NORMAL, T_NONE },  // 162 normal
  { "HOOTHOOT", 164, 20, R_COMUN, 0x8C4D, 60, 30, 30, 50, 36, 56, 0, T_NORMAL, T_FLYING },  // 163 normal/flying
  { "NOCTOWL", 0, 0, R_EVO, 0x8C4D, 100, 50, 50, 70, 86, 96, 0, T_NORMAL, T_FLYING },  // 164 normal/flying
  { "LEDYBA", 166, 18, R_COMUN, 0x7CC4, 40, 20, 30, 55, 40, 80, 2, T_BUG, T_FLYING },  // 165 bug/flying
  { "LEDIAN", 0, 0, R_EVO, 0x7CC4, 55, 35, 50, 85, 55, 110, 2, T_BUG, T_FLYING },  // 166 bug/flying
  { "SPINARAK", 168, 22, R_COMUN, 0x7CC4, 40, 60, 40, 30, 40, 40, 2, T_BUG, T_POISON },  // 167 bug/poison
  { "ARIADOS", 0, 0, R_EVO, 0x7CC4, 70, 90, 70, 40, 60, 70, 2, T_BUG, T_POISON },  // 168 bug/poison
  { "CROBAT", 0, 0, R_EVO, 0x8A73, 85, 90, 80, 130, 70, 80, 0, T_POISON, T_FLYING },  // 169 poison/flying
  { "CHINCHOU", 171, 27, R_COMUN, 0x4C98, 75, 38, 38, 67, 56, 56, 1, T_WATER, T_ELECTRIC },  // 170 water/electric
  { "LANTURN", 0, 0, R_EVO, 0x4C98, 125, 58, 58, 67, 76, 76, 1, T_WATER, T_ELECTRIC },  // 171 water/electric
  { "PICHU", 25, 25, R_COMUN, 0xBCA1, 20, 40, 15, 60, 35, 35, 0, T_ELECTRIC, T_NONE },  // 172 electric
  { "CLEFFA", 35, 25, R_COMUN, 0xC333, 50, 25, 28, 15, 45, 55, 0, T_FAIRY, T_NONE },  // 173 fairy
  { "IGGLYBUFF", 39, 25, R_COMUN, 0x8C4D, 90, 30, 15, 15, 40, 20, 0, T_NORMAL, T_FAIRY },  // 174 normal/fairy
  { "TOGEPI", 176, 25, R_COMUN, 0xC333, 35, 20, 65, 20, 40, 65, 0, T_FAIRY, T_NONE },  // 175 fairy
  { "TOGETIC", 468, 30, R_EVO, 0xC333, 55, 40, 85, 40, 80, 105, 0, T_FAIRY, T_FLYING },  // 176 fairy/flying
  { "NATU", 178, 25, R_COMUN, 0xD28F, 40, 50, 45, 70, 70, 45, 0, T_PSYCHIC, T_FLYING },  // 177 psychic/flying
  { "XATU", 0, 0, R_EVO, 0xD28F, 65, 75, 70, 95, 95, 70, 0, T_PSYCHIC, T_FLYING },  // 178 psychic/flying
  { "MAREEP", 180, 15, R_COMUN, 0xBCA1, 55, 40, 40, 35, 65, 45, 0, T_ELECTRIC, T_NONE },  // 179 electric
  { "FLAAFFY", 181, 30, R_EVO, 0xBCA1, 70, 55, 55, 45, 80, 60, 0, T_ELECTRIC, T_NONE },  // 180 electric
  { "AMPHAROS", 0, 0, R_EVO, 0xBCA1, 90, 75, 85, 55, 115, 90, 0, T_ELECTRIC, T_NONE },  // 181 electric
  { "BELLOSSOM", 0, 0, R_RARO, 0x3C49, 75, 80, 95, 50, 90, 100, 2, T_GRASS, T_NONE },  // 182 grass
  { "MARILL", 184, 18, R_EVO, 0x4C98, 70, 20, 50, 40, 20, 50, 1, T_WATER, T_FAIRY },  // 183 water/fairy
  { "AZUMARILL", 0, 0, R_EVO, 0x4C98, 100, 50, 80, 50, 60, 80, 1, T_WATER, T_FAIRY },  // 184 water/fairy
  { "SUDOWOODO", 0, 0, R_EVO, 0x9407, 70, 100, 115, 30, 30, 65, 4, T_ROCK, T_NONE },  // 185 rock
  { "POLITOED", 0, 0, R_RARO, 0x4C98, 90, 75, 75, 70, 90, 100, 1, T_WATER, T_NONE },  // 186 water
  { "HOPPIP", 188, 18, R_COMUN, 0x3C49, 35, 35, 40, 50, 35, 55, 2, T_GRASS, T_FLYING },  // 187 grass/flying
  { "SKIPLOOM", 189, 27, R_EVO, 0x3C49, 55, 45, 50, 80, 45, 65, 2, T_GRASS, T_FLYING },  // 188 grass/flying
  { "JUMPLUFF", 0, 0, R_EVO, 0x3C49, 75, 55, 70, 110, 55, 95, 2, T_GRASS, T_FLYING },  // 189 grass/flying
  { "AIPOM", 424, 30, R_RARO, 0x8C4D, 55, 70, 55, 85, 40, 55, 0, T_NORMAL, T_NONE },  // 190 normal
  { "SUNKERN", 192, 30, R_COMUN, 0x3C49, 30, 30, 30, 30, 30, 30, 2, T_GRASS, T_NONE },  // 191 grass
  { "SUNFLORA", 0, 0, R_EVO, 0x3C49, 75, 75, 55, 30, 105, 85, 2, T_GRASS, T_NONE },  // 192 grass
  { "YANMA", 469, 30, R_COMUN, 0x7CC4, 65, 65, 45, 95, 75, 45, 2, T_BUG, T_FLYING },  // 193 bug/flying
  { "WOOPER", 195, 20, R_COMUN, 0x4C98, 55, 45, 45, 15, 25, 25, 1, T_WATER, T_GROUND },  // 194 water/ground
  { "QUAGSIRE", 0, 0, R_EVO, 0x4C98, 95, 85, 85, 35, 65, 65, 1, T_WATER, T_GROUND },  // 195 water/ground
  { "ESPEON", 0, 0, R_EVO, 0xD28F, 65, 65, 60, 110, 130, 95, 0, T_PSYCHIC, T_NONE },  // 196 psychic
  { "UMBREON", 0, 0, R_EVO, 0x5A47, 95, 65, 110, 65, 60, 130, 2, T_DARK, T_NONE },  // 197 dark
  { "MURKROW", 430, 30, R_RARO, 0x5A47, 60, 85, 42, 91, 85, 42, 2, T_DARK, T_FLYING },  // 198 dark/flying
  { "SLOWKING", 0, 0, R_COMUN, 0x4C98, 95, 75, 80, 30, 100, 110, 1, T_WATER, T_PSYCHIC },  // 199 water/psychic
  { "MISDREAVUS", 429, 30, R_RARO, 0x6AD3, 60, 60, 60, 85, 85, 85, 0, T_GHOST, T_NONE },  // 200 ghost
  { "UNOWN", 0, 0, R_COMUN, 0xD28F, 48, 72, 48, 48, 72, 48, 0, T_PSYCHIC, T_NONE },  // 201 psychic
  { "WOBBUFFET", 0, 0, R_EVO, 0xD28F, 190, 33, 58, 33, 33, 58, 0, T_PSYCHIC, T_NONE },  // 202 psychic
  { "GIRAFARIG", 981, 30, R_COMUN, 0x8C4D, 70, 80, 65, 85, 90, 65, 0, T_NORMAL, T_PSYCHIC },  // 203 normal/psychic
  { "PINECO", 205, 31, R_COMUN, 0x7CC4, 50, 65, 90, 15, 35, 35, 2, T_BUG, T_NONE },  // 204 bug
  { "FORRETRESS", 0, 0, R_EVO, 0x7CC4, 75, 90, 140, 40, 60, 60, 2, T_BUG, T_STEEL },  // 205 bug/steel
  { "DUNSPARCE", 982, 30, R_COMUN, 0x8C4D, 100, 70, 70, 45, 65, 65, 0, T_NORMAL, T_NONE },  // 206 normal
  { "GLIGAR", 472, 30, R_COMUN, 0xB447, 65, 75, 105, 85, 35, 65, 4, T_GROUND, T_FLYING },  // 207 ground/flying
  { "STEELIX", 0, 0, R_EVO, 0x6BF1, 75, 85, 200, 30, 55, 65, 4, T_STEEL, T_GROUND },  // 208 steel/ground
  { "SNUBBULL", 210, 23, R_COMUN, 0xC333, 60, 80, 50, 30, 40, 40, 0, T_FAIRY, T_NONE },  // 209 fairy
  { "GRANBULL", 0, 0, R_EVO, 0xC333, 90, 120, 75, 45, 60, 60, 0, T_FAIRY, T_NONE },  // 210 fairy
  { "QWILFISH", 904, 30, R_RARO, 0x4C98, 65, 95, 85, 85, 55, 55, 1, T_WATER, T_POISON },  // 211 water/poison
  { "SCIZOR", 0, 0, R_EVO, 0x7CC4, 70, 130, 100, 65, 55, 80, 2, T_BUG, T_STEEL },  // 212 bug/steel
  { "SHUCKLE", 0, 0, R_COMUN, 0x7CC4, 20, 10, 230, 5, 10, 230, 2, T_BUG, T_ROCK },  // 213 bug/rock
  { "HERACROSS", 0, 0, R_RARO, 0x7CC4, 80, 125, 75, 85, 40, 95, 2, T_BUG, T_FIGHTING },  // 214 bug/fighting
  { "SNEASEL", 461, 30, R_COMUN, 0x5A47, 55, 95, 55, 115, 35, 75, 2, T_DARK, T_ICE },  // 215 dark/ice
  { "TEDDIURSA", 217, 30, R_COMUN, 0x8C4D, 60, 80, 50, 40, 50, 50, 0, T_NORMAL, T_NONE },  // 216 normal
  { "URSARING", 901, 30, R_EVO, 0x8C4D, 90, 130, 75, 55, 75, 75, 0, T_NORMAL, T_NONE },  // 217 normal
  { "SLUGMA", 219, 38, R_COMUN, 0xEA87, 40, 40, 40, 20, 70, 40, 3, T_FIRE, T_NONE },  // 218 fire
  { "MAGCARGO", 0, 0, R_EVO, 0xEA87, 60, 50, 120, 30, 90, 80, 3, T_FIRE, T_ROCK },  // 219 fire/rock
  { "SWINUB", 221, 33, R_COMUN, 0x4DB8, 50, 50, 40, 50, 30, 30, 5, T_ICE, T_GROUND },  // 220 ice/ground
  { "PILOSWINE", 473, 30, R_EVO, 0x4DB8, 100, 100, 80, 50, 60, 60, 5, T_ICE, T_GROUND },  // 221 ice/ground
  { "CORSOLA", 864, 38, R_COMUN, 0x4C98, 65, 55, 95, 35, 65, 95, 1, T_WATER, T_ROCK },  // 222 water/rock
  { "REMORAID", 224, 25, R_COMUN, 0x4C98, 35, 65, 35, 65, 65, 35, 1, T_WATER, T_NONE },  // 223 water
  { "OCTILLERY", 0, 0, R_EVO, 0x4C98, 75, 105, 75, 45, 105, 75, 1, T_WATER, T_NONE },  // 224 water
  { "DELIBIRD", 0, 0, R_RARO, 0x4DB8, 45, 55, 45, 75, 65, 45, 5, T_ICE, T_FLYING },  // 225 ice/flying
  { "MANTINE", 0, 0, R_EVO, 0x4C98, 85, 40, 70, 70, 80, 140, 1, T_WATER, T_FLYING },  // 226 water/flying
  { "SKARMORY", 0, 0, R_RARO, 0x6BF1, 65, 80, 140, 70, 40, 70, 4, T_STEEL, T_FLYING },  // 227 steel/flying
  { "HOUNDOUR", 229, 24, R_COMUN, 0x5A47, 45, 60, 30, 65, 80, 50, 2, T_DARK, T_FIRE },  // 228 dark/fire
  { "HOUNDOOM", 0, 0, R_EVO, 0x5A47, 75, 90, 50, 95, 110, 80, 2, T_DARK, T_FIRE },  // 229 dark/fire
  { "KINGDRA", 0, 0, R_EVO, 0x4C98, 75, 95, 95, 85, 95, 95, 1, T_WATER, T_DRAGON },  // 230 water/dragon
  { "PHANPY", 232, 25, R_COMUN, 0xB447, 90, 60, 60, 40, 40, 40, 4, T_GROUND, T_NONE },  // 231 ground
  { "DONPHAN", 0, 0, R_EVO, 0xB447, 90, 120, 120, 50, 60, 60, 4, T_GROUND, T_NONE },  // 232 ground
  { "PORYGON2", 474, 40, R_EVO, 0x8C4D, 85, 80, 90, 60, 105, 95, 0, T_NORMAL, T_NONE },  // 233 normal
  { "STANTLER", 899, 30, R_RARO, 0x8C4D, 73, 95, 62, 85, 85, 65, 0, T_NORMAL, T_NONE },  // 234 normal
  { "SMEARGLE", 0, 0, R_RARO, 0x8C4D, 55, 20, 35, 75, 20, 45, 0, T_NORMAL, T_NONE },  // 235 normal
  { "TYROGUE", 106, 20, R_COMUN, 0xA2A5, 35, 35, 35, 35, 35, 35, 0, T_FIGHTING, T_NONE },  // 236 fighting
  { "HITMONTOP", 0, 0, R_RARO, 0xA2A5, 50, 95, 95, 70, 35, 110, 0, T_FIGHTING, T_NONE },  // 237 fighting
  { "SMOOCHUM", 124, 30, R_RARO, 0x4DB8, 45, 30, 15, 65, 85, 65, 5, T_ICE, T_PSYCHIC },  // 238 ice/psychic
  { "ELEKID", 125, 30, R_RARO, 0xBCA1, 45, 63, 37, 95, 65, 55, 0, T_ELECTRIC, T_NONE },  // 239 electric
  { "MAGBY", 126, 30, R_RARO, 0xEA87, 45, 75, 37, 83, 70, 55, 3, T_FIRE, T_NONE },  // 240 fire
  { "MILTANK", 0, 0, R_RARO, 0x8C4D, 95, 80, 105, 100, 40, 70, 0, T_NORMAL, T_NONE },  // 241 normal
  { "BLISSEY", 0, 0, R_EVO, 0x8C4D, 255, 10, 10, 55, 75, 135, 0, T_NORMAL, T_NONE },  // 242 normal
  { "RAIKOU", 0, 0, R_LEGENDARIO, 0xBCA1, 90, 85, 75, 115, 115, 100, 0, T_ELECTRIC, T_NONE },  // 243 electric
  { "ENTEI", 0, 0, R_LEGENDARIO, 0xEA87, 115, 115, 85, 100, 90, 75, 3, T_FIRE, T_NONE },  // 244 fire
  { "SUICUNE", 0, 0, R_LEGENDARIO, 0x4C98, 100, 75, 115, 85, 90, 115, 1, T_WATER, T_NONE },  // 245 water
  { "LARVITAR", 247, 30, R_RARO, 0x9407, 50, 64, 50, 41, 45, 50, 4, T_ROCK, T_GROUND },  // 246 rock/ground
  { "PUPITAR", 248, 55, R_EVO, 0x9407, 70, 84, 70, 51, 65, 70, 4, T_ROCK, T_GROUND },  // 247 rock/ground
  { "TYRANITAR", 0, 0, R_EVO, 0x9407, 100, 134, 110, 61, 95, 100, 4, T_ROCK, T_DARK },  // 248 rock/dark
  { "LUGIA", 0, 0, R_LEGENDARIO, 0xD28F, 106, 90, 130, 110, 90, 154, 0, T_PSYCHIC, T_FLYING },  // 249 psychic/flying
  { "HO-OH", 0, 0, R_LEGENDARIO, 0xEA87, 106, 130, 90, 90, 110, 154, 3, T_FIRE, T_FLYING },  // 250 fire/flying
  { "CELEBI", 0, 0, R_LEGENDARIO, 0xD28F, 100, 100, 100, 100, 100, 100, 0, T_PSYCHIC, T_GRASS },  // 251 psychic/grass
  { "TREECKO", 253, 16, R_RARO, 0x3C49, 40, 45, 35, 70, 65, 55, 2, T_GRASS, T_NONE },  // 252 grass
  { "GROVYLE", 254, 36, R_EVO, 0x3C49, 50, 65, 45, 95, 85, 65, 2, T_GRASS, T_NONE },  // 253 grass
  { "SCEPTILE", 0, 0, R_EVO, 0x3C49, 70, 85, 65, 120, 105, 85, 2, T_GRASS, T_NONE },  // 254 grass
  { "TORCHIC", 256, 16, R_RARO, 0xEA87, 45, 60, 40, 45, 70, 50, 3, T_FIRE, T_NONE },  // 255 fire
  { "COMBUSKEN", 257, 36, R_EVO, 0xEA87, 60, 85, 60, 55, 85, 60, 3, T_FIRE, T_FIGHTING },  // 256 fire/fighting
  { "BLAZIKEN", 0, 0, R_EVO, 0xEA87, 80, 120, 70, 80, 110, 70, 3, T_FIRE, T_FIGHTING },  // 257 fire/fighting
  { "MUDKIP", 259, 16, R_RARO, 0x4C98, 50, 70, 50, 40, 50, 50, 1, T_WATER, T_NONE },  // 258 water
  { "MARSHTOMP", 260, 36, R_EVO, 0x4C98, 70, 85, 70, 50, 60, 70, 1, T_WATER, T_GROUND },  // 259 water/ground
  { "SWAMPERT", 0, 0, R_EVO, 0x4C98, 100, 110, 90, 60, 85, 90, 1, T_WATER, T_GROUND },  // 260 water/ground
  { "POOCHYENA", 262, 18, R_COMUN, 0x5A47, 35, 55, 35, 35, 30, 30, 2, T_DARK, T_NONE },  // 261 dark
  { "MIGHTYENA", 0, 0, R_EVO, 0x5A47, 70, 90, 70, 70, 60, 60, 2, T_DARK, T_NONE },  // 262 dark
  { "ZIGZAGOON", 264, 20, R_COMUN, 0x8C4D, 38, 30, 41, 60, 30, 41, 0, T_NORMAL, T_NONE },  // 263 normal
  { "LINOONE", 862, 35, R_EVO, 0x8C4D, 78, 70, 61, 100, 50, 61, 0, T_NORMAL, T_NONE },  // 264 normal
  { "WURMPLE", 266, 7, R_COMUN, 0x7CC4, 45, 45, 35, 20, 20, 30, 2, T_BUG, T_NONE },  // 265 bug
  { "SILCOON", 267, 10, R_EVO, 0x7CC4, 50, 35, 55, 15, 25, 25, 2, T_BUG, T_NONE },  // 266 bug
  { "BEAUTIFLY", 0, 0, R_EVO, 0x7CC4, 60, 70, 50, 65, 100, 50, 2, T_BUG, T_FLYING },  // 267 bug/flying
  { "CASCOON", 269, 10, R_COMUN, 0x7CC4, 50, 35, 55, 15, 25, 25, 2, T_BUG, T_NONE },  // 268 bug
  { "DUSTOX", 0, 0, R_EVO, 0x7CC4, 60, 50, 70, 65, 50, 90, 2, T_BUG, T_POISON },  // 269 bug/poison
  { "LOTAD", 271, 14, R_COMUN, 0x4C98, 40, 30, 30, 30, 40, 50, 1, T_WATER, T_GRASS },  // 270 water/grass
  { "LOMBRE", 272, 30, R_EVO, 0x4C98, 60, 50, 50, 50, 60, 70, 1, T_WATER, T_GRASS },  // 271 water/grass
  { "LUDICOLO", 0, 0, R_EVO, 0x4C98, 80, 70, 70, 70, 90, 100, 1, T_WATER, T_GRASS },  // 272 water/grass
  { "SEEDOT", 274, 14, R_COMUN, 0x3C49, 40, 40, 50, 30, 30, 30, 2, T_GRASS, T_NONE },  // 273 grass
  { "NUZLEAF", 275, 30, R_EVO, 0x3C49, 70, 70, 40, 60, 60, 40, 2, T_GRASS, T_DARK },  // 274 grass/dark
  { "SHIFTRY", 0, 0, R_EVO, 0x3C49, 90, 100, 60, 80, 90, 60, 2, T_GRASS, T_DARK },  // 275 grass/dark
  { "TAILLOW", 277, 22, R_COMUN, 0x8C4D, 40, 55, 30, 85, 30, 30, 0, T_NORMAL, T_FLYING },  // 276 normal/flying
  { "SWELLOW", 0, 0, R_EVO, 0x8C4D, 60, 85, 60, 125, 75, 50, 0, T_NORMAL, T_FLYING },  // 277 normal/flying
  { "WINGULL", 279, 25, R_COMUN, 0x4C98, 40, 30, 30, 85, 55, 30, 1, T_WATER, T_FLYING },  // 278 water/flying
  { "PELIPPER", 0, 0, R_EVO, 0x4C98, 60, 50, 100, 65, 95, 70, 1, T_WATER, T_FLYING },  // 279 water/flying
  { "RALTS", 281, 20, R_COMUN, 0xD28F, 28, 25, 25, 40, 45, 35, 0, T_PSYCHIC, T_FAIRY },  // 280 psychic/fairy
  { "KIRLIA", 282, 30, R_EVO, 0xD28F, 38, 35, 35, 50, 65, 55, 0, T_PSYCHIC, T_FAIRY },  // 281 psychic/fairy
  { "GARDEVOIR", 0, 0, R_EVO, 0xD28F, 68, 65, 65, 80, 125, 115, 0, T_PSYCHIC, T_FAIRY },  // 282 psychic/fairy
  { "SURSKIT", 284, 22, R_COMUN, 0x7CC4, 40, 30, 32, 65, 50, 52, 2, T_BUG, T_WATER },  // 283 bug/water
  { "MASQUERAIN", 0, 0, R_EVO, 0x7CC4, 70, 60, 62, 80, 100, 82, 2, T_BUG, T_FLYING },  // 284 bug/flying
  { "SHROOMISH", 286, 23, R_COMUN, 0x3C49, 60, 40, 60, 35, 40, 60, 2, T_GRASS, T_NONE },  // 285 grass
  { "BRELOOM", 0, 0, R_EVO, 0x3C49, 60, 130, 80, 70, 60, 60, 2, T_GRASS, T_FIGHTING },  // 286 grass/fighting
  { "SLAKOTH", 288, 18, R_COMUN, 0x8C4D, 60, 60, 60, 30, 35, 35, 0, T_NORMAL, T_NONE },  // 287 normal
  { "VIGOROTH", 289, 36, R_EVO, 0x8C4D, 80, 80, 80, 90, 55, 55, 0, T_NORMAL, T_NONE },  // 288 normal
  { "SLAKING", 0, 0, R_EVO, 0x8C4D, 150, 160, 100, 100, 95, 65, 0, T_NORMAL, T_NONE },  // 289 normal
  { "NINCADA", 291, 20, R_COMUN, 0x7CC4, 31, 45, 90, 40, 30, 30, 2, T_BUG, T_GROUND },  // 290 bug/ground
  { "NINJASK", 0, 0, R_EVO, 0x7CC4, 61, 90, 45, 160, 50, 50, 2, T_BUG, T_FLYING },  // 291 bug/flying
  { "SHEDINJA", 0, 0, R_RARO, 0x7CC4, 1, 90, 45, 40, 30, 30, 2, T_BUG, T_GHOST },  // 292 bug/ghost
  { "WHISMUR", 294, 20, R_COMUN, 0x8C4D, 64, 51, 23, 28, 51, 23, 0, T_NORMAL, T_NONE },  // 293 normal
  { "LOUDRED", 295, 40, R_EVO, 0x8C4D, 84, 71, 43, 48, 71, 43, 0, T_NORMAL, T_NONE },  // 294 normal
  { "EXPLOUD", 0, 0, R_EVO, 0x8C4D, 104, 91, 63, 68, 91, 73, 0, T_NORMAL, T_NONE },  // 295 normal
  { "MAKUHITA", 297, 24, R_COMUN, 0xA2A5, 72, 60, 30, 25, 20, 30, 0, T_FIGHTING, T_NONE },  // 296 fighting
  { "HARIYAMA", 0, 0, R_EVO, 0xA2A5, 144, 120, 60, 50, 40, 60, 0, T_FIGHTING, T_NONE },  // 297 fighting
  { "AZURILL", 183, 25, R_COMUN, 0x8C4D, 50, 20, 40, 20, 20, 40, 0, T_NORMAL, T_FAIRY },  // 298 normal/fairy
  { "NOSEPASS", 476, 30, R_COMUN, 0x9407, 30, 45, 135, 30, 45, 90, 4, T_ROCK, T_NONE },  // 299 rock
  { "SKITTY", 301, 30, R_COMUN, 0x8C4D, 50, 45, 45, 50, 35, 35, 0, T_NORMAL, T_NONE },  // 300 normal
  { "DELCATTY", 0, 0, R_EVO, 0x8C4D, 70, 65, 65, 90, 55, 55, 0, T_NORMAL, T_NONE },  // 301 normal
  { "SABLEYE", 0, 0, R_RARO, 0x5A47, 50, 75, 75, 50, 65, 65, 2, T_DARK, T_GHOST },  // 302 dark/ghost
  { "MAWILE", 0, 0, R_RARO, 0x6BF1, 50, 85, 85, 50, 55, 55, 4, T_STEEL, T_FAIRY },  // 303 steel/fairy
  { "ARON", 305, 32, R_COMUN, 0x6BF1, 50, 70, 100, 30, 40, 40, 4, T_STEEL, T_ROCK },  // 304 steel/rock
  { "LAIRON", 306, 42, R_EVO, 0x6BF1, 60, 90, 140, 40, 50, 50, 4, T_STEEL, T_ROCK },  // 305 steel/rock
  { "AGGRON", 0, 0, R_EVO, 0x6BF1, 70, 110, 180, 50, 60, 60, 4, T_STEEL, T_ROCK },  // 306 steel/rock
  { "MEDITITE", 308, 37, R_COMUN, 0xA2A5, 30, 40, 55, 60, 40, 55, 0, T_FIGHTING, T_PSYCHIC },  // 307 fighting/psychic
  { "MEDICHAM", 0, 0, R_EVO, 0xA2A5, 60, 60, 75, 80, 60, 75, 0, T_FIGHTING, T_PSYCHIC },  // 308 fighting/psychic
  { "ELECTRIKE", 310, 26, R_COMUN, 0xBCA1, 40, 45, 40, 65, 65, 40, 0, T_ELECTRIC, T_NONE },  // 309 electric
  { "MANECTRIC", 0, 0, R_EVO, 0xBCA1, 70, 75, 60, 105, 105, 60, 0, T_ELECTRIC, T_NONE },  // 310 electric
  { "PLUSLE", 0, 0, R_COMUN, 0xBCA1, 60, 50, 40, 95, 85, 75, 0, T_ELECTRIC, T_NONE },  // 311 electric
  { "MINUN", 0, 0, R_COMUN, 0xBCA1, 60, 40, 50, 95, 75, 85, 0, T_ELECTRIC, T_NONE },  // 312 electric
  { "VOLBEAT", 0, 0, R_COMUN, 0x7CC4, 65, 73, 75, 85, 47, 85, 2, T_BUG, T_NONE },  // 313 bug
  { "ILLUMISE", 0, 0, R_COMUN, 0x7CC4, 65, 47, 75, 85, 73, 85, 2, T_BUG, T_NONE },  // 314 bug
  { "ROSELIA", 407, 30, R_EVO, 0x3C49, 50, 60, 45, 65, 100, 80, 2, T_GRASS, T_POISON },  // 315 grass/poison
  { "GULPIN", 317, 26, R_COMUN, 0x8A73, 70, 43, 53, 40, 43, 53, 0, T_POISON, T_NONE },  // 316 poison
  { "SWALOT", 0, 0, R_EVO, 0x8A73, 100, 73, 83, 55, 73, 83, 0, T_POISON, T_NONE },  // 317 poison
  { "CARVANHA", 319, 30, R_COMUN, 0x4C98, 45, 90, 20, 65, 65, 20, 1, T_WATER, T_DARK },  // 318 water/dark
  { "SHARPEDO", 0, 0, R_EVO, 0x4C98, 70, 120, 40, 95, 95, 40, 1, T_WATER, T_DARK },  // 319 water/dark
  { "WAILMER", 321, 40, R_COMUN, 0x4C98, 130, 70, 35, 60, 70, 35, 1, T_WATER, T_NONE },  // 320 water
  { "WAILORD", 0, 0, R_EVO, 0x4C98, 170, 90, 45, 60, 90, 45, 1, T_WATER, T_NONE },  // 321 water
  { "NUMEL", 323, 33, R_COMUN, 0xEA87, 60, 60, 40, 35, 65, 45, 3, T_FIRE, T_GROUND },  // 322 fire/ground
  { "CAMERUPT", 0, 0, R_EVO, 0xEA87, 70, 100, 70, 40, 105, 75, 3, T_FIRE, T_GROUND },  // 323 fire/ground
  { "TORKOAL", 0, 0, R_COMUN, 0xEA87, 70, 85, 140, 20, 85, 70, 3, T_FIRE, T_NONE },  // 324 fire
  { "SPOINK", 326, 32, R_COMUN, 0xD28F, 60, 25, 35, 60, 70, 80, 0, T_PSYCHIC, T_NONE },  // 325 psychic
  { "GRUMPIG", 0, 0, R_EVO, 0xD28F, 80, 45, 65, 80, 90, 110, 0, T_PSYCHIC, T_NONE },  // 326 psychic
  { "SPINDA", 0, 0, R_COMUN, 0x8C4D, 60, 60, 60, 60, 60, 60, 0, T_NORMAL, T_NONE },  // 327 normal
  { "TRAPINCH", 329, 35, R_COMUN, 0xB447, 45, 100, 45, 10, 45, 45, 4, T_GROUND, T_NONE },  // 328 ground
  { "VIBRAVA", 330, 45, R_EVO, 0xB447, 50, 70, 50, 70, 50, 50, 4, T_GROUND, T_DRAGON },  // 329 ground/dragon
  { "FLYGON", 0, 0, R_EVO, 0xB447, 80, 100, 80, 100, 80, 80, 4, T_GROUND, T_DRAGON },  // 330 ground/dragon
  { "CACNEA", 332, 32, R_COMUN, 0x3C49, 50, 85, 40, 35, 85, 40, 2, T_GRASS, T_NONE },  // 331 grass
  { "CACTURNE", 0, 0, R_EVO, 0x3C49, 70, 115, 60, 55, 115, 60, 2, T_GRASS, T_DARK },  // 332 grass/dark
  { "SWABLU", 334, 35, R_COMUN, 0x8C4D, 45, 40, 60, 50, 40, 75, 0, T_NORMAL, T_FLYING },  // 333 normal/flying
  { "ALTARIA", 0, 0, R_EVO, 0x5A98, 75, 70, 90, 80, 70, 105, 1, T_DRAGON, T_FLYING },  // 334 dragon/flying
  { "ZANGOOSE", 0, 0, R_COMUN, 0x8C4D, 73, 115, 60, 90, 60, 60, 0, T_NORMAL, T_NONE },  // 335 normal
  { "SEVIPER", 0, 0, R_COMUN, 0x8A73, 73, 100, 60, 65, 100, 60, 0, T_POISON, T_NONE },  // 336 poison
  { "LUNATONE", 0, 0, R_RARO, 0x9407, 90, 55, 65, 70, 95, 85, 4, T_ROCK, T_PSYCHIC },  // 337 rock/psychic
  { "SOLROCK", 0, 0, R_RARO, 0x9407, 90, 95, 85, 70, 55, 65, 4, T_ROCK, T_PSYCHIC },  // 338 rock/psychic
  { "BARBOACH", 340, 30, R_COMUN, 0x4C98, 50, 48, 43, 60, 46, 41, 1, T_WATER, T_GROUND },  // 339 water/ground
  { "WHISCASH", 0, 0, R_EVO, 0x4C98, 110, 78, 73, 60, 76, 71, 1, T_WATER, T_GROUND },  // 340 water/ground
  { "CORPHISH", 342, 30, R_COMUN, 0x4C98, 43, 80, 65, 35, 50, 35, 1, T_WATER, T_NONE },  // 341 water
  { "CRAWDAUNT", 0, 0, R_EVO, 0x4C98, 63, 120, 85, 55, 90, 55, 1, T_WATER, T_DARK },  // 342 water/dark
  { "BALTOY", 344, 36, R_COMUN, 0xB447, 40, 40, 55, 55, 40, 70, 4, T_GROUND, T_PSYCHIC },  // 343 ground/psychic
  { "CLAYDOL", 0, 0, R_EVO, 0xB447, 60, 70, 105, 75, 70, 120, 4, T_GROUND, T_PSYCHIC },  // 344 ground/psychic
  { "LILEEP", 346, 40, R_RARO, 0x9407, 66, 41, 77, 23, 61, 87, 4, T_ROCK, T_GRASS },  // 345 rock/grass
  { "CRADILY", 0, 0, R_EVO, 0x9407, 86, 81, 97, 43, 81, 107, 4, T_ROCK, T_GRASS },  // 346 rock/grass
  { "ANORITH", 348, 40, R_RARO, 0x9407, 45, 95, 50, 75, 40, 50, 4, T_ROCK, T_BUG },  // 347 rock/bug
  { "ARMALDO", 0, 0, R_EVO, 0x9407, 75, 125, 100, 45, 70, 80, 4, T_ROCK, T_BUG },  // 348 rock/bug
  { "FEEBAS", 350, 40, R_COMUN, 0x4C98, 20, 15, 20, 80, 10, 55, 1, T_WATER, T_NONE },  // 349 water
  { "MILOTIC", 0, 0, R_EVO, 0x4C98, 95, 60, 79, 81, 100, 125, 1, T_WATER, T_NONE },  // 350 water
  { "CASTFORM", 0, 0, R_RARO, 0x8C4D, 70, 70, 70, 70, 70, 70, 0, T_NORMAL, T_NONE },  // 351 normal
  { "KECLEON", 0, 0, R_COMUN, 0x8C4D, 60, 90, 70, 40, 60, 120, 0, T_NORMAL, T_NONE },  // 352 normal
  { "SHUPPET", 354, 37, R_COMUN, 0x6AD3, 44, 75, 35, 45, 63, 33, 0, T_GHOST, T_NONE },  // 353 ghost
  { "BANETTE", 0, 0, R_EVO, 0x6AD3, 64, 115, 65, 65, 83, 63, 0, T_GHOST, T_NONE },  // 354 ghost
  { "DUSKULL", 356, 37, R_COMUN, 0x6AD3, 20, 40, 90, 25, 30, 90, 0, T_GHOST, T_NONE },  // 355 ghost
  { "DUSCLOPS", 477, 40, R_EVO, 0x6AD3, 40, 70, 130, 25, 60, 130, 0, T_GHOST, T_NONE },  // 356 ghost
  { "TROPIUS", 0, 0, R_COMUN, 0x3C49, 99, 68, 83, 51, 72, 87, 2, T_GRASS, T_FLYING },  // 357 grass/flying
  { "CHIMECHO", 0, 0, R_EVO, 0xD28F, 75, 50, 80, 65, 95, 90, 0, T_PSYCHIC, T_NONE },  // 358 psychic
  { "ABSOL", 0, 0, R_RARO, 0x5A47, 65, 130, 60, 75, 75, 60, 2, T_DARK, T_NONE },  // 359 dark
  { "WYNAUT", 202, 15, R_COMUN, 0xD28F, 95, 23, 48, 23, 23, 48, 0, T_PSYCHIC, T_NONE },  // 360 psychic
  { "SNORUNT", 362, 42, R_COMUN, 0x4DB8, 50, 50, 50, 50, 50, 50, 5, T_ICE, T_NONE },  // 361 ice
  { "GLALIE", 0, 0, R_EVO, 0x4DB8, 80, 80, 80, 80, 80, 80, 5, T_ICE, T_NONE },  // 362 ice
  { "SPHEAL", 364, 32, R_COMUN, 0x4DB8, 70, 40, 50, 25, 55, 50, 5, T_ICE, T_WATER },  // 363 ice/water
  { "SEALEO", 365, 44, R_EVO, 0x4DB8, 90, 60, 70, 45, 75, 70, 5, T_ICE, T_WATER },  // 364 ice/water
  { "WALREIN", 0, 0, R_EVO, 0x4DB8, 110, 80, 90, 65, 95, 90, 5, T_ICE, T_WATER },  // 365 ice/water
  { "CLAMPERL", 367, 40, R_COMUN, 0x4C98, 35, 64, 85, 32, 74, 55, 1, T_WATER, T_NONE },  // 366 water
  { "HUNTAIL", 0, 0, R_EVO, 0x4C98, 55, 104, 105, 52, 94, 75, 1, T_WATER, T_NONE },  // 367 water
  { "GOREBYSS", 0, 0, R_COMUN, 0x4C98, 55, 84, 105, 52, 114, 75, 1, T_WATER, T_NONE },  // 368 water
  { "RELICANTH", 0, 0, R_RARO, 0x4C98, 100, 90, 130, 55, 45, 65, 1, T_WATER, T_ROCK },  // 369 water/rock
  { "LUVDISC", 0, 0, R_COMUN, 0x4C98, 43, 30, 55, 97, 40, 65, 1, T_WATER, T_NONE },  // 370 water
  { "BAGON", 372, 30, R_RARO, 0x5A98, 45, 75, 60, 50, 40, 30, 1, T_DRAGON, T_NONE },  // 371 dragon
  { "SHELGON", 373, 50, R_EVO, 0x5A98, 65, 95, 100, 50, 60, 50, 1, T_DRAGON, T_NONE },  // 372 dragon
  { "SALAMENCE", 0, 0, R_EVO, 0x5A98, 95, 135, 80, 100, 110, 80, 1, T_DRAGON, T_FLYING },  // 373 dragon/flying
  { "BELDUM", 375, 20, R_RARO, 0x6BF1, 40, 55, 80, 30, 35, 60, 4, T_STEEL, T_PSYCHIC },  // 374 steel/psychic
  { "METANG", 376, 45, R_EVO, 0x6BF1, 60, 75, 100, 50, 55, 80, 4, T_STEEL, T_PSYCHIC },  // 375 steel/psychic
  { "METAGROSS", 0, 0, R_EVO, 0x6BF1, 80, 135, 130, 70, 95, 90, 4, T_STEEL, T_PSYCHIC },  // 376 steel/psychic
  { "REGIROCK", 0, 0, R_LEGENDARIO, 0x9407, 80, 100, 200, 50, 50, 100, 4, T_ROCK, T_NONE },  // 377 rock
  { "REGICE", 0, 0, R_LEGENDARIO, 0x4DB8, 80, 50, 100, 50, 100, 200, 5, T_ICE, T_NONE },  // 378 ice
  { "REGISTEEL", 0, 0, R_LEGENDARIO, 0x6BF1, 80, 75, 150, 50, 75, 150, 4, T_STEEL, T_NONE },  // 379 steel
  { "LATIAS", 0, 0, R_LEGENDARIO, 0x5A98, 80, 80, 90, 110, 110, 130, 1, T_DRAGON, T_PSYCHIC },  // 380 dragon/psychic
  { "LATIOS", 0, 0, R_LEGENDARIO, 0x5A98, 80, 90, 80, 110, 130, 110, 1, T_DRAGON, T_PSYCHIC },  // 381 dragon/psychic
  { "KYOGRE", 0, 0, R_LEGENDARIO, 0x4C98, 100, 100, 90, 90, 150, 140, 1, T_WATER, T_NONE },  // 382 water
  { "GROUDON", 0, 0, R_LEGENDARIO, 0xB447, 100, 150, 140, 90, 100, 90, 4, T_GROUND, T_NONE },  // 383 ground
  { "RAYQUAZA", 0, 0, R_LEGENDARIO, 0x5A98, 105, 150, 90, 95, 150, 90, 1, T_DRAGON, T_FLYING },  // 384 dragon/flying
  { "JIRACHI", 0, 0, R_LEGENDARIO, 0x6BF1, 100, 100, 100, 100, 100, 100, 4, T_STEEL, T_PSYCHIC },  // 385 steel/psychic
  { "DEOXYS", 0, 0, R_LEGENDARIO, 0xD28F, 50, 150, 50, 150, 150, 50, 0, T_PSYCHIC, T_NONE },  // 386 psychic
  { "TURTWIG", 388, 18, R_RARO, 0x3C49, 55, 68, 64, 31, 45, 55, 2, T_GRASS, T_NONE },  // 387 grass
  { "GROTLE", 389, 32, R_EVO, 0x3C49, 75, 89, 85, 36, 55, 65, 2, T_GRASS, T_NONE },  // 388 grass
  { "TORTERRA", 0, 0, R_EVO, 0x3C49, 95, 109, 105, 56, 75, 85, 2, T_GRASS, T_GROUND },  // 389 grass/ground
  { "CHIMCHAR", 391, 14, R_RARO, 0xEA87, 44, 58, 44, 61, 58, 44, 3, T_FIRE, T_NONE },  // 390 fire
  { "MONFERNO", 392, 36, R_EVO, 0xEA87, 64, 78, 52, 81, 78, 52, 3, T_FIRE, T_FIGHTING },  // 391 fire/fighting
  { "INFERNAPE", 0, 0, R_EVO, 0xEA87, 76, 104, 71, 108, 104, 71, 3, T_FIRE, T_FIGHTING },  // 392 fire/fighting
  { "PIPLUP", 394, 16, R_RARO, 0x4C98, 53, 51, 53, 40, 61, 56, 1, T_WATER, T_NONE },  // 393 water
  { "PRINPLUP", 395, 36, R_EVO, 0x4C98, 64, 66, 68, 50, 81, 76, 1, T_WATER, T_NONE },  // 394 water
  { "EMPOLEON", 0, 0, R_EVO, 0x4C98, 84, 86, 88, 60, 111, 101, 1, T_WATER, T_STEEL },  // 395 water/steel
  { "STARLY", 397, 14, R_COMUN, 0x8C4D, 40, 55, 30, 60, 30, 30, 0, T_NORMAL, T_FLYING },  // 396 normal/flying
  { "STARAVIA", 398, 34, R_EVO, 0x8C4D, 55, 75, 50, 80, 40, 40, 0, T_NORMAL, T_FLYING },  // 397 normal/flying
  { "STARAPTOR", 0, 0, R_EVO, 0x8C4D, 85, 120, 70, 100, 50, 60, 0, T_NORMAL, T_FLYING },  // 398 normal/flying
  { "BIDOOF", 400, 15, R_COMUN, 0x8C4D, 59, 45, 40, 31, 35, 40, 0, T_NORMAL, T_NONE },  // 399 normal
  { "BIBAREL", 0, 0, R_EVO, 0x8C4D, 79, 85, 60, 71, 55, 60, 0, T_NORMAL, T_WATER },  // 400 normal/water
  { "KRICKETOT", 402, 10, R_COMUN, 0x7CC4, 37, 25, 41, 25, 25, 41, 2, T_BUG, T_NONE },  // 401 bug
  { "KRICKETUNE", 0, 0, R_EVO, 0x7CC4, 77, 85, 51, 65, 55, 51, 2, T_BUG, T_NONE },  // 402 bug
  { "SHINX", 404, 15, R_COMUN, 0xBCA1, 45, 65, 34, 45, 40, 34, 0, T_ELECTRIC, T_NONE },  // 403 electric
  { "LUXIO", 405, 30, R_EVO, 0xBCA1, 60, 85, 49, 60, 60, 49, 0, T_ELECTRIC, T_NONE },  // 404 electric
  { "LUXRAY", 0, 0, R_EVO, 0xBCA1, 80, 120, 79, 70, 95, 79, 0, T_ELECTRIC, T_NONE },  // 405 electric
  { "BUDEW", 315, 25, R_COMUN, 0x3C49, 40, 30, 35, 55, 50, 70, 2, T_GRASS, T_POISON },  // 406 grass/poison
  { "ROSERADE", 0, 0, R_EVO, 0x3C49, 60, 70, 65, 90, 125, 105, 2, T_GRASS, T_POISON },  // 407 grass/poison
  { "CRANIDOS", 409, 30, R_RARO, 0x9407, 67, 125, 40, 58, 30, 30, 4, T_ROCK, T_NONE },  // 408 rock
  { "RAMPARDOS", 0, 0, R_EVO, 0x9407, 97, 165, 60, 58, 65, 50, 4, T_ROCK, T_NONE },  // 409 rock
  { "SHIELDON", 411, 30, R_RARO, 0x9407, 30, 42, 118, 30, 42, 88, 4, T_ROCK, T_STEEL },  // 410 rock/steel
  { "BASTIODON", 0, 0, R_EVO, 0x9407, 60, 52, 168, 30, 47, 138, 4, T_ROCK, T_STEEL },  // 411 rock/steel
  { "BURMY", 413, 20, R_COMUN, 0x7CC4, 40, 29, 45, 36, 29, 45, 2, T_BUG, T_NONE },  // 412 bug
  { "WORMADAM", 0, 0, R_EVO, 0x7CC4, 60, 59, 85, 36, 79, 105, 2, T_BUG, T_GRASS },  // 413 bug/grass
  { "MOTHIM", 0, 0, R_RARO, 0x7CC4, 70, 94, 50, 66, 94, 50, 2, T_BUG, T_FLYING },  // 414 bug/flying
  { "COMBEE", 416, 21, R_COMUN, 0x7CC4, 30, 30, 42, 70, 30, 42, 2, T_BUG, T_FLYING },  // 415 bug/flying
  { "VESPIQUEN", 0, 0, R_EVO, 0x7CC4, 70, 80, 102, 40, 80, 102, 2, T_BUG, T_FLYING },  // 416 bug/flying
  { "PACHIRISU", 0, 0, R_COMUN, 0xBCA1, 60, 45, 70, 95, 45, 90, 0, T_ELECTRIC, T_NONE },  // 417 electric
  { "BUIZEL", 419, 26, R_COMUN, 0x4C98, 55, 65, 35, 85, 60, 30, 1, T_WATER, T_NONE },  // 418 water
  { "FLOATZEL", 0, 0, R_EVO, 0x4C98, 85, 105, 55, 115, 85, 50, 1, T_WATER, T_NONE },  // 419 water
  { "CHERUBI", 421, 25, R_COMUN, 0x3C49, 45, 35, 45, 35, 62, 53, 2, T_GRASS, T_NONE },  // 420 grass
  { "CHERRIM", 0, 0, R_EVO, 0x3C49, 70, 60, 70, 85, 87, 78, 2, T_GRASS, T_NONE },  // 421 grass
  { "SHELLOS", 423, 30, R_COMUN, 0x4C98, 76, 48, 48, 34, 57, 62, 1, T_WATER, T_NONE },  // 422 water
  { "GASTRODON", 0, 0, R_EVO, 0x4C98, 111, 83, 68, 39, 92, 82, 1, T_WATER, T_GROUND },  // 423 water/ground
  { "AMBIPOM", 0, 0, R_EVO, 0x8C4D, 75, 100, 66, 115, 60, 66, 0, T_NORMAL, T_NONE },  // 424 normal
  { "DRIFLOON", 426, 28, R_COMUN, 0x6AD3, 90, 50, 34, 70, 60, 44, 0, T_GHOST, T_FLYING },  // 425 ghost/flying
  { "DRIFBLIM", 0, 0, R_EVO, 0x6AD3, 150, 80, 44, 80, 90, 54, 0, T_GHOST, T_FLYING },  // 426 ghost/flying
  { "BUNEARY", 428, 25, R_COMUN, 0x8C4D, 55, 66, 44, 85, 44, 56, 0, T_NORMAL, T_NONE },  // 427 normal
  { "LOPUNNY", 0, 0, R_EVO, 0x8C4D, 65, 76, 84, 105, 54, 96, 0, T_NORMAL, T_NONE },  // 428 normal
  { "MISMAGIUS", 0, 0, R_EVO, 0x6AD3, 60, 60, 60, 105, 105, 105, 0, T_GHOST, T_NONE },  // 429 ghost
  { "HONCHKROW", 0, 0, R_EVO, 0x5A47, 100, 125, 52, 71, 105, 52, 2, T_DARK, T_FLYING },  // 430 dark/flying
  { "GLAMEOW", 432, 38, R_COMUN, 0x8C4D, 49, 55, 42, 85, 42, 37, 0, T_NORMAL, T_NONE },  // 431 normal
  { "PURUGLY", 0, 0, R_EVO, 0x8C4D, 71, 82, 64, 112, 64, 59, 0, T_NORMAL, T_NONE },  // 432 normal
  { "CHINGLING", 358, 25, R_COMUN, 0xD28F, 45, 30, 50, 45, 65, 50, 0, T_PSYCHIC, T_NONE },  // 433 psychic
  { "STUNKY", 435, 34, R_COMUN, 0x8A73, 63, 63, 47, 74, 41, 41, 0, T_POISON, T_DARK },  // 434 poison/dark
  { "SKUNTANK", 0, 0, R_EVO, 0x8A73, 103, 93, 67, 84, 71, 61, 0, T_POISON, T_DARK },  // 435 poison/dark
  { "BRONZOR", 437, 33, R_COMUN, 0x6BF1, 57, 24, 86, 23, 24, 86, 4, T_STEEL, T_PSYCHIC },  // 436 steel/psychic
  { "BRONZONG", 0, 0, R_EVO, 0x6BF1, 67, 89, 116, 33, 79, 116, 4, T_STEEL, T_PSYCHIC },  // 437 steel/psychic
  { "BONSLY", 185, 30, R_COMUN, 0x9407, 50, 80, 95, 10, 10, 45, 4, T_ROCK, T_NONE },  // 438 rock
  { "MIME JR", 122, 30, R_COMUN, 0xD28F, 20, 25, 45, 60, 70, 90, 0, T_PSYCHIC, T_FAIRY },  // 439 psychic/fairy
  { "HAPPINY", 113, 30, R_COMUN, 0x8C4D, 100, 5, 5, 30, 15, 65, 0, T_NORMAL, T_NONE },  // 440 normal
  { "CHATOT", 0, 0, R_RARO, 0x8C4D, 76, 65, 45, 91, 92, 42, 0, T_NORMAL, T_FLYING },  // 441 normal/flying
  { "SPIRITOMB", 0, 0, R_COMUN, 0x6AD3, 50, 92, 108, 35, 92, 108, 0, T_GHOST, T_DARK },  // 442 ghost/dark
  { "GIBLE", 444, 24, R_RARO, 0x5A98, 58, 70, 45, 42, 40, 45, 1, T_DRAGON, T_GROUND },  // 443 dragon/ground
  { "GABITE", 445, 48, R_EVO, 0x5A98, 68, 90, 65, 82, 50, 55, 1, T_DRAGON, T_GROUND },  // 444 dragon/ground
  { "GARCHOMP", 0, 0, R_EVO, 0x5A98, 108, 130, 95, 102, 80, 85, 1, T_DRAGON, T_GROUND },  // 445 dragon/ground
  { "MUNCHLAX", 143, 25, R_COMUN, 0x8C4D, 135, 85, 40, 5, 40, 85, 0, T_NORMAL, T_NONE },  // 446 normal
  { "RIOLU", 448, 25, R_COMUN, 0xA2A5, 40, 70, 40, 60, 35, 40, 0, T_FIGHTING, T_NONE },  // 447 fighting
  { "LUCARIO", 0, 0, R_EVO, 0xA2A5, 70, 110, 70, 90, 115, 70, 0, T_FIGHTING, T_STEEL },  // 448 fighting/steel
  { "HIPPOPOTAS", 450, 34, R_COMUN, 0xB447, 68, 72, 78, 32, 38, 42, 4, T_GROUND, T_NONE },  // 449 ground
  { "HIPPOWDON", 0, 0, R_EVO, 0xB447, 108, 112, 118, 47, 68, 72, 4, T_GROUND, T_NONE },  // 450 ground
  { "SKORUPI", 452, 40, R_COMUN, 0x8A73, 40, 50, 90, 65, 30, 55, 0, T_POISON, T_BUG },  // 451 poison/bug
  { "DRAPION", 0, 0, R_EVO, 0x8A73, 70, 90, 110, 95, 60, 75, 0, T_POISON, T_DARK },  // 452 poison/dark
  { "CROAGUNK", 454, 37, R_COMUN, 0x8A73, 48, 61, 40, 50, 61, 40, 0, T_POISON, T_FIGHTING },  // 453 poison/fighting
  { "TOXICROAK", 0, 0, R_EVO, 0x8A73, 83, 106, 65, 85, 86, 65, 0, T_POISON, T_FIGHTING },  // 454 poison/fighting
  { "CARNIVINE", 0, 0, R_COMUN, 0x3C49, 74, 100, 72, 46, 90, 72, 2, T_GRASS, T_NONE },  // 455 grass
  { "FINNEON", 457, 31, R_COMUN, 0x4C98, 49, 49, 56, 66, 49, 61, 1, T_WATER, T_NONE },  // 456 water
  { "LUMINEON", 0, 0, R_EVO, 0x4C98, 69, 69, 76, 91, 69, 86, 1, T_WATER, T_NONE },  // 457 water
  { "MANTYKE", 226, 30, R_RARO, 0x4C98, 45, 20, 50, 50, 60, 120, 1, T_WATER, T_FLYING },  // 458 water/flying
  { "SNOVER", 460, 40, R_COMUN, 0x3C49, 60, 62, 50, 40, 62, 60, 2, T_GRASS, T_ICE },  // 459 grass/ice
  { "ABOMASNOW", 0, 0, R_EVO, 0x3C49, 90, 92, 75, 60, 92, 85, 2, T_GRASS, T_ICE },  // 460 grass/ice
  { "WEAVILE", 0, 0, R_EVO, 0x5A47, 70, 120, 65, 125, 45, 85, 2, T_DARK, T_ICE },  // 461 dark/ice
  { "MAGNEZONE", 0, 0, R_EVO, 0xBCA1, 70, 70, 115, 60, 130, 90, 0, T_ELECTRIC, T_STEEL },  // 462 electric/steel
  { "LICKILICKY", 0, 0, R_EVO, 0x8C4D, 110, 85, 95, 50, 80, 95, 0, T_NORMAL, T_NONE },  // 463 normal
  { "RHYPERIOR", 0, 0, R_EVO, 0xB447, 115, 140, 130, 40, 55, 55, 4, T_GROUND, T_ROCK },  // 464 ground/rock
  { "TANGROWTH", 0, 0, R_EVO, 0x3C49, 100, 100, 125, 50, 110, 50, 2, T_GRASS, T_NONE },  // 465 grass
  { "ELECTIVIRE", 0, 0, R_EVO, 0xBCA1, 75, 123, 67, 95, 95, 85, 0, T_ELECTRIC, T_NONE },  // 466 electric
  { "MAGMORTAR", 0, 0, R_EVO, 0xEA87, 75, 95, 67, 83, 125, 95, 3, T_FIRE, T_NONE },  // 467 fire
  { "TOGEKISS", 0, 0, R_EVO, 0xC333, 85, 50, 95, 80, 120, 115, 0, T_FAIRY, T_FLYING },  // 468 fairy/flying
  { "YANMEGA", 0, 0, R_EVO, 0x7CC4, 86, 76, 86, 95, 116, 56, 2, T_BUG, T_FLYING },  // 469 bug/flying
  { "LEAFEON", 0, 0, R_EVO, 0x3C49, 65, 110, 130, 95, 60, 65, 2, T_GRASS, T_NONE },  // 470 grass
  { "GLACEON", 0, 0, R_EVO, 0x4DB8, 65, 60, 110, 65, 130, 95, 5, T_ICE, T_NONE },  // 471 ice
  { "GLISCOR", 0, 0, R_EVO, 0xB447, 75, 95, 125, 95, 45, 75, 4, T_GROUND, T_FLYING },  // 472 ground/flying
  { "MAMOSWINE", 0, 0, R_EVO, 0x4DB8, 110, 130, 80, 80, 70, 60, 5, T_ICE, T_GROUND },  // 473 ice/ground
  { "PORYGON Z", 0, 0, R_EVO, 0x8C4D, 85, 80, 70, 90, 135, 75, 0, T_NORMAL, T_NONE },  // 474 normal
  { "GALLADE", 0, 0, R_RARO, 0xD28F, 68, 125, 65, 80, 65, 115, 0, T_PSYCHIC, T_FIGHTING },  // 475 psychic/fighting
  { "PROBOPASS", 0, 0, R_EVO, 0x9407, 60, 55, 145, 40, 75, 150, 4, T_ROCK, T_STEEL },  // 476 rock/steel
  { "DUSKNOIR", 0, 0, R_EVO, 0x6AD3, 45, 100, 135, 45, 65, 135, 0, T_GHOST, T_NONE },  // 477 ghost
  { "FROSLASS", 0, 0, R_COMUN, 0x4DB8, 70, 80, 70, 110, 80, 70, 5, T_ICE, T_GHOST },  // 478 ice/ghost
  { "ROTOM", 0, 0, R_RARO, 0xBCA1, 50, 50, 77, 91, 95, 77, 0, T_ELECTRIC, T_GHOST },  // 479 electric/ghost
  { "UXIE", 0, 0, R_LEGENDARIO, 0xD28F, 75, 75, 130, 95, 75, 130, 0, T_PSYCHIC, T_NONE },  // 480 psychic
  { "MESPRIT", 0, 0, R_LEGENDARIO, 0xD28F, 80, 105, 105, 80, 105, 105, 0, T_PSYCHIC, T_NONE },  // 481 psychic
  { "AZELF", 0, 0, R_LEGENDARIO, 0xD28F, 75, 125, 70, 115, 125, 70, 0, T_PSYCHIC, T_NONE },  // 482 psychic
  { "DIALGA", 0, 0, R_LEGENDARIO, 0x6BF1, 100, 120, 120, 90, 150, 100, 4, T_STEEL, T_DRAGON },  // 483 steel/dragon
  { "PALKIA", 0, 0, R_LEGENDARIO, 0x4C98, 90, 120, 100, 100, 150, 120, 1, T_WATER, T_DRAGON },  // 484 water/dragon
  { "HEATRAN", 0, 0, R_LEGENDARIO, 0xEA87, 91, 90, 106, 77, 130, 106, 3, T_FIRE, T_STEEL },  // 485 fire/steel
  { "REGIGIGAS", 0, 0, R_LEGENDARIO, 0x8C4D, 110, 160, 110, 100, 80, 110, 0, T_NORMAL, T_NONE },  // 486 normal
  { "GIRATINA", 0, 0, R_LEGENDARIO, 0x6AD3, 150, 100, 120, 90, 100, 120, 0, T_GHOST, T_DRAGON },  // 487 ghost/dragon
  { "CRESSELIA", 0, 0, R_LEGENDARIO, 0xD28F, 120, 70, 110, 85, 75, 120, 0, T_PSYCHIC, T_NONE },  // 488 psychic
  { "PHIONE", 490, 30, R_LEGENDARIO, 0x4C98, 80, 80, 80, 80, 80, 80, 1, T_WATER, T_NONE },  // 489 water
  { "MANAPHY", 0, 0, R_EVO, 0x4C98, 100, 100, 100, 100, 100, 100, 1, T_WATER, T_NONE },  // 490 water
  { "DARKRAI", 0, 0, R_LEGENDARIO, 0x5A47, 70, 90, 90, 125, 135, 90, 2, T_DARK, T_NONE },  // 491 dark
  { "SHAYMIN", 0, 0, R_LEGENDARIO, 0x3C49, 100, 100, 100, 100, 100, 100, 2, T_GRASS, T_NONE },  // 492 grass
  { "ARCEUS", 0, 0, R_LEGENDARIO, 0x8C4D, 120, 120, 120, 120, 120, 120, 0, T_NORMAL, T_NONE },  // 493 normal
  { "VICTINI", 0, 0, R_LEGENDARIO, 0xD28F, 100, 100, 100, 100, 100, 100, 0, T_PSYCHIC, T_FIRE },  // 494 psychic/fire
  { "SNIVY", 496, 17, R_RARO, 0x3C49, 45, 45, 55, 63, 45, 55, 2, T_GRASS, T_NONE },  // 495 grass
  { "SERVINE", 497, 36, R_EVO, 0x3C49, 60, 60, 75, 83, 60, 75, 2, T_GRASS, T_NONE },  // 496 grass
  { "SERPERIOR", 0, 0, R_EVO, 0x3C49, 75, 75, 95, 113, 75, 95, 2, T_GRASS, T_NONE },  // 497 grass
  { "TEPIG", 499, 17, R_RARO, 0xEA87, 65, 63, 45, 45, 45, 45, 3, T_FIRE, T_NONE },  // 498 fire
  { "PIGNITE", 500, 36, R_EVO, 0xEA87, 90, 93, 55, 55, 70, 55, 3, T_FIRE, T_FIGHTING },  // 499 fire/fighting
  { "EMBOAR", 0, 0, R_EVO, 0xEA87, 110, 123, 65, 65, 100, 65, 3, T_FIRE, T_FIGHTING },  // 500 fire/fighting
  { "OSHAWOTT", 502, 17, R_RARO, 0x4C98, 55, 55, 45, 45, 63, 45, 1, T_WATER, T_NONE },  // 501 water
  { "DEWOTT", 503, 36, R_EVO, 0x4C98, 75, 75, 60, 60, 83, 60, 1, T_WATER, T_NONE },  // 502 water
  { "SAMUROTT", 0, 0, R_EVO, 0x4C98, 95, 100, 85, 70, 108, 70, 1, T_WATER, T_NONE },  // 503 water
  { "PATRAT", 505, 20, R_COMUN, 0x8C4D, 45, 55, 39, 42, 35, 39, 0, T_NORMAL, T_NONE },  // 504 normal
  { "WATCHOG", 0, 0, R_EVO, 0x8C4D, 60, 85, 69, 77, 60, 69, 0, T_NORMAL, T_NONE },  // 505 normal
  { "LILLIPUP", 507, 16, R_COMUN, 0x8C4D, 45, 60, 45, 55, 25, 45, 0, T_NORMAL, T_NONE },  // 506 normal
  { "HERDIER", 508, 32, R_EVO, 0x8C4D, 65, 80, 65, 60, 35, 65, 0, T_NORMAL, T_NONE },  // 507 normal
  { "STOUTLAND", 0, 0, R_EVO, 0x8C4D, 85, 110, 90, 80, 45, 90, 0, T_NORMAL, T_NONE },  // 508 normal
  { "PURRLOIN", 510, 20, R_COMUN, 0x5A47, 41, 50, 37, 66, 50, 37, 2, T_DARK, T_NONE },  // 509 dark
  { "LIEPARD", 0, 0, R_EVO, 0x5A47, 64, 88, 50, 106, 88, 50, 2, T_DARK, T_NONE },  // 510 dark
  { "PANSAGE", 512, 30, R_COMUN, 0x3C49, 50, 53, 48, 64, 53, 48, 2, T_GRASS, T_NONE },  // 511 grass
  { "SIMISAGE", 0, 0, R_EVO, 0x3C49, 75, 98, 63, 101, 98, 63, 2, T_GRASS, T_NONE },  // 512 grass
  { "PANSEAR", 514, 30, R_COMUN, 0xEA87, 50, 53, 48, 64, 53, 48, 3, T_FIRE, T_NONE },  // 513 fire
  { "SIMISEAR", 0, 0, R_EVO, 0xEA87, 75, 98, 63, 101, 98, 63, 3, T_FIRE, T_NONE },  // 514 fire
  { "PANPOUR", 516, 30, R_COMUN, 0x4C98, 50, 53, 48, 64, 53, 48, 1, T_WATER, T_NONE },  // 515 water
  { "SIMIPOUR", 0, 0, R_EVO, 0x4C98, 75, 98, 63, 101, 98, 63, 1, T_WATER, T_NONE },  // 516 water
  { "MUNNA", 518, 30, R_COMUN, 0xD28F, 76, 25, 45, 24, 67, 55, 0, T_PSYCHIC, T_NONE },  // 517 psychic
  { "MUSHARNA", 0, 0, R_EVO, 0xD28F, 116, 55, 85, 29, 107, 95, 0, T_PSYCHIC, T_NONE },  // 518 psychic
  { "PIDOVE", 520, 21, R_COMUN, 0x8C4D, 50, 55, 50, 43, 36, 30, 0, T_NORMAL, T_FLYING },  // 519 normal/flying
  { "TRANQUILL", 521, 32, R_EVO, 0x8C4D, 62, 77, 62, 65, 50, 42, 0, T_NORMAL, T_FLYING },  // 520 normal/flying
  { "UNFEZANT", 0, 0, R_EVO, 0x8C4D, 80, 115, 80, 93, 65, 55, 0, T_NORMAL, T_FLYING },  // 521 normal/flying
  { "BLITZLE", 523, 27, R_COMUN, 0xBCA1, 45, 60, 32, 76, 50, 32, 0, T_ELECTRIC, T_NONE },  // 522 electric
  { "ZEBSTRIKA", 0, 0, R_EVO, 0xBCA1, 75, 100, 63, 116, 80, 63, 0, T_ELECTRIC, T_NONE },  // 523 electric
  { "ROGGENROLA", 525, 25, R_COMUN, 0x9407, 55, 75, 85, 15, 25, 25, 4, T_ROCK, T_NONE },  // 524 rock
  { "BOLDORE", 526, 40, R_EVO, 0x9407, 70, 105, 105, 20, 50, 40, 4, T_ROCK, T_NONE },  // 525 rock
  { "GIGALITH", 0, 0, R_EVO, 0x9407, 85, 135, 130, 25, 60, 80, 4, T_ROCK, T_NONE },  // 526 rock
  { "WOOBAT", 528, 25, R_COMUN, 0xD28F, 65, 45, 43, 72, 55, 43, 0, T_PSYCHIC, T_FLYING },  // 527 psychic/flying
  { "SWOOBAT", 0, 0, R_EVO, 0xD28F, 67, 57, 55, 114, 77, 55, 0, T_PSYCHIC, T_FLYING },  // 528 psychic/flying
  { "DRILBUR", 530, 31, R_COMUN, 0xB447, 60, 85, 40, 68, 30, 45, 4, T_GROUND, T_NONE },  // 529 ground
  { "EXCADRILL", 0, 0, R_EVO, 0xB447, 110, 135, 60, 88, 50, 65, 4, T_GROUND, T_STEEL },  // 530 ground/steel
  { "AUDINO", 0, 0, R_COMUN, 0x8C4D, 103, 60, 86, 50, 60, 86, 0, T_NORMAL, T_NONE },  // 531 normal
  { "TIMBURR", 533, 25, R_COMUN, 0xA2A5, 75, 80, 55, 35, 25, 35, 0, T_FIGHTING, T_NONE },  // 532 fighting
  { "GURDURR", 534, 40, R_EVO, 0xA2A5, 85, 105, 85, 40, 40, 50, 0, T_FIGHTING, T_NONE },  // 533 fighting
  { "CONKELDURR", 0, 0, R_EVO, 0xA2A5, 105, 140, 95, 45, 55, 65, 0, T_FIGHTING, T_NONE },  // 534 fighting
  { "TYMPOLE", 536, 25, R_COMUN, 0x4C98, 50, 50, 40, 64, 50, 40, 1, T_WATER, T_NONE },  // 535 water
  { "PALPITOAD", 537, 36, R_EVO, 0x4C98, 75, 65, 55, 69, 65, 55, 1, T_WATER, T_GROUND },  // 536 water/ground
  { "SEISMITOAD", 0, 0, R_EVO, 0x4C98, 105, 95, 75, 74, 85, 75, 1, T_WATER, T_GROUND },  // 537 water/ground
  { "THROH", 0, 0, R_RARO, 0xA2A5, 120, 100, 85, 45, 30, 85, 0, T_FIGHTING, T_NONE },  // 538 fighting
  { "SAWK", 0, 0, R_RARO, 0xA2A5, 75, 125, 75, 85, 30, 75, 0, T_FIGHTING, T_NONE },  // 539 fighting
  { "SEWADDLE", 541, 20, R_COMUN, 0x7CC4, 45, 53, 70, 42, 40, 60, 2, T_BUG, T_GRASS },  // 540 bug/grass
  { "SWADLOON", 542, 25, R_EVO, 0x7CC4, 55, 63, 90, 42, 50, 80, 2, T_BUG, T_GRASS },  // 541 bug/grass
  { "LEAVANNY", 0, 0, R_EVO, 0x7CC4, 75, 103, 80, 92, 70, 80, 2, T_BUG, T_GRASS },  // 542 bug/grass
  { "VENIPEDE", 544, 22, R_COMUN, 0x7CC4, 30, 45, 59, 57, 30, 39, 2, T_BUG, T_POISON },  // 543 bug/poison
  { "WHIRLIPEDE", 545, 30, R_EVO, 0x7CC4, 40, 55, 99, 47, 40, 79, 2, T_BUG, T_POISON },  // 544 bug/poison
  { "SCOLIPEDE", 0, 0, R_EVO, 0x7CC4, 60, 100, 89, 112, 55, 69, 2, T_BUG, T_POISON },  // 545 bug/poison
  { "COTTONEE", 547, 30, R_COMUN, 0x3C49, 40, 27, 60, 66, 37, 50, 2, T_GRASS, T_FAIRY },  // 546 grass/fairy
  { "WHIMSICOTT", 0, 0, R_EVO, 0x3C49, 60, 67, 85, 116, 77, 75, 2, T_GRASS, T_FAIRY },  // 547 grass/fairy
  { "PETILIL", 549, 30, R_COMUN, 0x3C49, 45, 35, 50, 30, 70, 50, 2, T_GRASS, T_NONE },  // 548 grass
  { "LILLIGANT", 0, 0, R_EVO, 0x3C49, 70, 60, 75, 90, 110, 75, 2, T_GRASS, T_NONE },  // 549 grass
  { "BASCULIN", 902, 30, R_RARO, 0x4C98, 70, 92, 65, 98, 80, 55, 1, T_WATER, T_NONE },  // 550 water
  { "SANDILE", 552, 29, R_COMUN, 0xB447, 50, 72, 35, 65, 35, 35, 4, T_GROUND, T_DARK },  // 551 ground/dark
  { "KROKOROK", 553, 40, R_EVO, 0xB447, 60, 82, 45, 74, 45, 45, 4, T_GROUND, T_DARK },  // 552 ground/dark
  { "KROOKODILE", 0, 0, R_EVO, 0xB447, 95, 117, 80, 92, 65, 70, 4, T_GROUND, T_DARK },  // 553 ground/dark
  { "DARUMAKA", 555, 35, R_COMUN, 0xEA87, 70, 90, 45, 50, 15, 45, 3, T_FIRE, T_NONE },  // 554 fire
  { "DARMANITAN", 0, 0, R_EVO, 0xEA87, 105, 140, 55, 95, 30, 55, 3, T_FIRE, T_NONE },  // 555 fire
  { "MARACTUS", 0, 0, R_COMUN, 0x3C49, 75, 86, 67, 60, 106, 67, 2, T_GRASS, T_NONE },  // 556 grass
  { "DWEBBLE", 558, 34, R_COMUN, 0x7CC4, 50, 65, 85, 55, 35, 35, 2, T_BUG, T_ROCK },  // 557 bug/rock
  { "CRUSTLE", 0, 0, R_EVO, 0x7CC4, 70, 105, 125, 45, 65, 75, 2, T_BUG, T_ROCK },  // 558 bug/rock
  { "SCRAGGY", 560, 39, R_COMUN, 0x5A47, 50, 75, 70, 48, 35, 70, 2, T_DARK, T_FIGHTING },  // 559 dark/fighting
  { "SCRAFTY", 0, 0, R_EVO, 0x5A47, 65, 90, 115, 58, 45, 115, 2, T_DARK, T_FIGHTING },  // 560 dark/fighting
  { "SIGILYPH", 0, 0, R_RARO, 0xD28F, 72, 58, 80, 97, 103, 80, 0, T_PSYCHIC, T_FLYING },  // 561 psychic/flying
  { "YAMASK", 563, 34, R_COMUN, 0x6AD3, 38, 30, 85, 30, 55, 65, 0, T_GHOST, T_NONE },  // 562 ghost
  { "COFAGRIGUS", 0, 0, R_EVO, 0x6AD3, 58, 50, 145, 30, 95, 105, 0, T_GHOST, T_NONE },  // 563 ghost
  { "TIRTOUGA", 565, 37, R_RARO, 0x4C98, 54, 78, 103, 22, 53, 45, 1, T_WATER, T_ROCK },  // 564 water/rock
  { "CARRACOSTA", 0, 0, R_EVO, 0x4C98, 74, 108, 133, 32, 83, 65, 1, T_WATER, T_ROCK },  // 565 water/rock
  { "ARCHEN", 567, 37, R_RARO, 0x9407, 55, 112, 45, 70, 74, 45, 4, T_ROCK, T_FLYING },  // 566 rock/flying
  { "ARCHEOPS", 0, 0, R_EVO, 0x9407, 75, 140, 65, 110, 112, 65, 4, T_ROCK, T_FLYING },  // 567 rock/flying
  { "TRUBBISH", 569, 36, R_COMUN, 0x8A73, 50, 50, 62, 65, 40, 62, 0, T_POISON, T_NONE },  // 568 poison
  { "GARBODOR", 0, 0, R_EVO, 0x8A73, 80, 95, 82, 75, 60, 82, 0, T_POISON, T_NONE },  // 569 poison
  { "ZORUA", 571, 30, R_COMUN, 0x5A47, 40, 65, 40, 65, 80, 40, 2, T_DARK, T_NONE },  // 570 dark
  { "ZOROARK", 0, 0, R_EVO, 0x5A47, 60, 105, 60, 105, 120, 60, 2, T_DARK, T_NONE },  // 571 dark
  { "MINCCINO", 573, 30, R_COMUN, 0x8C4D, 55, 50, 40, 75, 40, 40, 0, T_NORMAL, T_NONE },  // 572 normal
  { "CINCCINO", 0, 0, R_EVO, 0x8C4D, 75, 95, 60, 115, 65, 60, 0, T_NORMAL, T_NONE },  // 573 normal
  { "GOTHITA", 575, 32, R_COMUN, 0xD28F, 45, 30, 50, 45, 55, 65, 0, T_PSYCHIC, T_NONE },  // 574 psychic
  { "GOTHORITA", 576, 41, R_EVO, 0xD28F, 60, 45, 70, 55, 75, 85, 0, T_PSYCHIC, T_NONE },  // 575 psychic
  { "GOTHITELLE", 0, 0, R_EVO, 0xD28F, 70, 55, 95, 65, 95, 110, 0, T_PSYCHIC, T_NONE },  // 576 psychic
  { "SOLOSIS", 578, 32, R_COMUN, 0xD28F, 45, 30, 40, 20, 105, 50, 0, T_PSYCHIC, T_NONE },  // 577 psychic
  { "DUOSION", 579, 41, R_EVO, 0xD28F, 65, 40, 50, 30, 125, 60, 0, T_PSYCHIC, T_NONE },  // 578 psychic
  { "REUNICLUS", 0, 0, R_EVO, 0xD28F, 110, 65, 75, 30, 125, 85, 0, T_PSYCHIC, T_NONE },  // 579 psychic
  { "DUCKLETT", 581, 35, R_COMUN, 0x4C98, 62, 44, 50, 55, 44, 50, 1, T_WATER, T_FLYING },  // 580 water/flying
  { "SWANNA", 0, 0, R_EVO, 0x4C98, 75, 87, 63, 98, 87, 63, 1, T_WATER, T_FLYING },  // 581 water/flying
  { "VANILLITE", 583, 35, R_COMUN, 0x4DB8, 36, 50, 50, 44, 65, 60, 5, T_ICE, T_NONE },  // 582 ice
  { "VANILLISH", 584, 47, R_EVO, 0x4DB8, 51, 65, 65, 59, 80, 75, 5, T_ICE, T_NONE },  // 583 ice
  { "VANILLUXE", 0, 0, R_EVO, 0x4DB8, 71, 95, 85, 79, 110, 95, 5, T_ICE, T_NONE },  // 584 ice
  { "DEERLING", 586, 34, R_COMUN, 0x8C4D, 60, 60, 50, 75, 40, 50, 0, T_NORMAL, T_GRASS },  // 585 normal/grass
  { "SAWSBUCK", 0, 0, R_EVO, 0x8C4D, 80, 100, 70, 95, 60, 70, 0, T_NORMAL, T_GRASS },  // 586 normal/grass
  { "EMOLGA", 0, 0, R_COMUN, 0xBCA1, 55, 75, 60, 103, 75, 60, 0, T_ELECTRIC, T_FLYING },  // 587 electric/flying
  { "KARRABLAST", 589, 40, R_COMUN, 0x7CC4, 50, 75, 45, 60, 40, 45, 2, T_BUG, T_NONE },  // 588 bug
  { "ESCAVALIER", 0, 0, R_EVO, 0x7CC4, 70, 135, 105, 20, 60, 105, 2, T_BUG, T_STEEL },  // 589 bug/steel
  { "FOONGUS", 591, 39, R_COMUN, 0x3C49, 69, 55, 45, 15, 55, 55, 2, T_GRASS, T_POISON },  // 590 grass/poison
  { "AMOONGUSS", 0, 0, R_EVO, 0x3C49, 114, 85, 70, 30, 85, 80, 2, T_GRASS, T_POISON },  // 591 grass/poison
  { "FRILLISH", 593, 40, R_COMUN, 0x4C98, 55, 40, 50, 40, 65, 85, 1, T_WATER, T_GHOST },  // 592 water/ghost
  { "JELLICENT", 0, 0, R_EVO, 0x4C98, 100, 60, 70, 60, 85, 105, 1, T_WATER, T_GHOST },  // 593 water/ghost
  { "ALOMOMOLA", 0, 0, R_COMUN, 0x4C98, 165, 75, 80, 65, 40, 45, 1, T_WATER, T_NONE },  // 594 water
  { "JOLTIK", 596, 36, R_COMUN, 0x7CC4, 50, 47, 50, 65, 57, 50, 2, T_BUG, T_ELECTRIC },  // 595 bug/electric
  { "GALVANTULA", 0, 0, R_EVO, 0x7CC4, 70, 77, 60, 108, 97, 60, 2, T_BUG, T_ELECTRIC },  // 596 bug/electric
  { "FERROSEED", 598, 40, R_COMUN, 0x3C49, 44, 50, 91, 10, 24, 86, 2, T_GRASS, T_STEEL },  // 597 grass/steel
  { "FERROTHORN", 0, 0, R_EVO, 0x3C49, 74, 94, 131, 20, 54, 116, 2, T_GRASS, T_STEEL },  // 598 grass/steel
  { "KLINK", 600, 38, R_COMUN, 0x6BF1, 40, 55, 70, 30, 45, 60, 4, T_STEEL, T_NONE },  // 599 steel
  { "KLANG", 601, 49, R_EVO, 0x6BF1, 60, 80, 95, 50, 70, 85, 4, T_STEEL, T_NONE },  // 600 steel
  { "KLINKLANG", 0, 0, R_EVO, 0x6BF1, 60, 100, 115, 90, 70, 85, 4, T_STEEL, T_NONE },  // 601 steel
  { "TYNAMO", 603, 39, R_COMUN, 0xBCA1, 35, 55, 40, 60, 45, 40, 0, T_ELECTRIC, T_NONE },  // 602 electric
  { "EELEKTRIK", 604, 30, R_EVO, 0xBCA1, 65, 85, 70, 40, 75, 70, 0, T_ELECTRIC, T_NONE },  // 603 electric
  { "EELEKTROSS", 0, 0, R_EVO, 0xBCA1, 85, 115, 80, 50, 105, 80, 0, T_ELECTRIC, T_NONE },  // 604 electric
  { "ELGYEM", 606, 42, R_COMUN, 0xD28F, 55, 55, 55, 30, 85, 55, 0, T_PSYCHIC, T_NONE },  // 605 psychic
  { "BEHEEYEM", 0, 0, R_EVO, 0xD28F, 75, 75, 75, 40, 125, 95, 0, T_PSYCHIC, T_NONE },  // 606 psychic
  { "LITWICK", 608, 41, R_COMUN, 0x6AD3, 50, 30, 55, 20, 65, 55, 0, T_GHOST, T_FIRE },  // 607 ghost/fire
  { "LAMPENT", 609, 30, R_EVO, 0x6AD3, 60, 40, 60, 55, 95, 60, 0, T_GHOST, T_FIRE },  // 608 ghost/fire
  { "CHANDELURE", 0, 0, R_EVO, 0x6AD3, 60, 55, 90, 80, 145, 90, 0, T_GHOST, T_FIRE },  // 609 ghost/fire
  { "AXEW", 611, 38, R_COMUN, 0x5A98, 46, 87, 60, 57, 30, 40, 1, T_DRAGON, T_NONE },  // 610 dragon
  { "FRAXURE", 612, 48, R_EVO, 0x5A98, 66, 117, 70, 67, 40, 50, 1, T_DRAGON, T_NONE },  // 611 dragon
  { "HAXORUS", 0, 0, R_EVO, 0x5A98, 76, 147, 90, 97, 60, 70, 1, T_DRAGON, T_NONE },  // 612 dragon
  { "CUBCHOO", 614, 37, R_COMUN, 0x4DB8, 55, 70, 40, 40, 60, 40, 5, T_ICE, T_NONE },  // 613 ice
  { "BEARTIC", 0, 0, R_EVO, 0x4DB8, 95, 130, 80, 50, 70, 80, 5, T_ICE, T_NONE },  // 614 ice
  { "CRYOGONAL", 0, 0, R_RARO, 0x4DB8, 80, 50, 50, 105, 95, 135, 5, T_ICE, T_NONE },  // 615 ice
  { "SHELMET", 617, 40, R_COMUN, 0x7CC4, 50, 40, 85, 25, 40, 65, 2, T_BUG, T_NONE },  // 616 bug
  { "ACCELGOR", 0, 0, R_EVO, 0x7CC4, 80, 70, 40, 145, 100, 60, 2, T_BUG, T_NONE },  // 617 bug
  { "STUNFISK", 0, 0, R_COMUN, 0xB447, 109, 66, 84, 32, 81, 99, 4, T_GROUND, T_ELECTRIC },  // 618 ground/electric
  { "MIENFOO", 620, 50, R_COMUN, 0xA2A5, 45, 85, 50, 65, 55, 50, 0, T_FIGHTING, T_NONE },  // 619 fighting
  { "MIENSHAO", 0, 0, R_EVO, 0xA2A5, 65, 125, 60, 105, 95, 60, 0, T_FIGHTING, T_NONE },  // 620 fighting
  { "DRUDDIGON", 0, 0, R_RARO, 0x5A98, 77, 120, 90, 48, 60, 90, 1, T_DRAGON, T_NONE },  // 621 dragon
  { "GOLETT", 623, 43, R_COMUN, 0xB447, 59, 74, 50, 35, 35, 50, 4, T_GROUND, T_GHOST },  // 622 ground/ghost
  { "GOLURK", 0, 0, R_EVO, 0xB447, 89, 124, 80, 55, 55, 80, 4, T_GROUND, T_GHOST },  // 623 ground/ghost
  { "PAWNIARD", 625, 52, R_COMUN, 0x5A47, 45, 85, 70, 60, 40, 40, 2, T_DARK, T_STEEL },  // 624 dark/steel
  { "BISHARP", 983, 30, R_EVO, 0x5A47, 65, 125, 100, 70, 60, 70, 2, T_DARK, T_STEEL },  // 625 dark/steel
  { "BOUFFALANT", 0, 0, R_RARO, 0x8C4D, 95, 110, 95, 55, 40, 95, 0, T_NORMAL, T_NONE },  // 626 normal
  { "RUFFLET", 628, 54, R_COMUN, 0x8C4D, 70, 83, 50, 60, 37, 50, 0, T_NORMAL, T_FLYING },  // 627 normal/flying
  { "BRAVIARY", 0, 0, R_EVO, 0x8C4D, 100, 123, 75, 80, 57, 75, 0, T_NORMAL, T_FLYING },  // 628 normal/flying
  { "VULLABY", 630, 54, R_COMUN, 0x5A47, 70, 55, 75, 60, 45, 65, 2, T_DARK, T_FLYING },  // 629 dark/flying
  { "MANDIBUZZ", 0, 0, R_EVO, 0x5A47, 110, 65, 105, 80, 55, 95, 2, T_DARK, T_FLYING },  // 630 dark/flying
  { "HEATMOR", 0, 0, R_COMUN, 0xEA87, 85, 97, 66, 65, 105, 66, 3, T_FIRE, T_NONE },  // 631 fire
  { "DURANT", 0, 0, R_COMUN, 0x7CC4, 58, 109, 112, 109, 48, 48, 2, T_BUG, T_STEEL },  // 632 bug/steel
  { "DEINO", 634, 50, R_RARO, 0x5A47, 52, 65, 50, 38, 45, 50, 2, T_DARK, T_DRAGON },  // 633 dark/dragon
  { "ZWEILOUS", 635, 64, R_EVO, 0x5A47, 72, 85, 70, 58, 65, 70, 2, T_DARK, T_DRAGON },  // 634 dark/dragon
  { "HYDREIGON", 0, 0, R_EVO, 0x5A47, 92, 105, 90, 98, 125, 90, 2, T_DARK, T_DRAGON },  // 635 dark/dragon
  { "LARVESTA", 637, 59, R_RARO, 0x7CC4, 55, 85, 55, 60, 50, 55, 2, T_BUG, T_FIRE },  // 636 bug/fire
  { "VOLCARONA", 0, 0, R_EVO, 0x7CC4, 85, 60, 65, 100, 135, 105, 2, T_BUG, T_FIRE },  // 637 bug/fire
  { "COBALION", 0, 0, R_LEGENDARIO, 0x6BF1, 91, 90, 129, 108, 90, 72, 4, T_STEEL, T_FIGHTING },  // 638 steel/fighting
  { "TERRAKION", 0, 0, R_LEGENDARIO, 0x9407, 91, 129, 90, 108, 72, 90, 4, T_ROCK, T_FIGHTING },  // 639 rock/fighting
  { "VIRIZION", 0, 0, R_LEGENDARIO, 0x3C49, 91, 90, 72, 108, 90, 129, 2, T_GRASS, T_FIGHTING },  // 640 grass/fighting
  { "TORNADUS", 0, 0, R_LEGENDARIO, 0x7C7A, 79, 115, 70, 111, 125, 80, 0, T_FLYING, T_NONE },  // 641 flying
  { "THUNDURUS", 0, 0, R_LEGENDARIO, 0xBCA1, 79, 115, 70, 111, 125, 80, 0, T_ELECTRIC, T_FLYING },  // 642 electric/flying
  { "RESHIRAM", 0, 0, R_LEGENDARIO, 0x5A98, 100, 120, 100, 90, 150, 120, 1, T_DRAGON, T_FIRE },  // 643 dragon/fire
  { "ZEKROM", 0, 0, R_LEGENDARIO, 0x5A98, 100, 150, 120, 90, 120, 100, 1, T_DRAGON, T_ELECTRIC },  // 644 dragon/electric
  { "LANDORUS", 0, 0, R_LEGENDARIO, 0xB447, 89, 125, 90, 101, 115, 80, 4, T_GROUND, T_FLYING },  // 645 ground/flying
  { "KYUREM", 0, 0, R_LEGENDARIO, 0x5A98, 125, 130, 90, 95, 130, 90, 1, T_DRAGON, T_ICE },  // 646 dragon/ice
  { "KELDEO", 0, 0, R_LEGENDARIO, 0x4C98, 91, 72, 90, 108, 129, 90, 1, T_WATER, T_FIGHTING },  // 647 water/fighting
  { "MELOETTA", 0, 0, R_LEGENDARIO, 0x8C4D, 100, 77, 77, 90, 128, 128, 0, T_NORMAL, T_PSYCHIC },  // 648 normal/psychic
  { "GENESECT", 0, 0, R_LEGENDARIO, 0x7CC4, 71, 120, 95, 99, 120, 95, 2, T_BUG, T_STEEL },  // 649 bug/steel
  { "CHESPIN", 651, 16, R_RARO, 0x3C49, 56, 61, 65, 38, 48, 45, 2, T_GRASS, T_NONE },  // 650 grass
  { "QUILLADIN", 652, 36, R_EVO, 0x3C49, 61, 78, 95, 57, 56, 58, 2, T_GRASS, T_NONE },  // 651 grass
  { "CHESNAUGHT", 0, 0, R_EVO, 0x3C49, 88, 107, 122, 64, 74, 75, 2, T_GRASS, T_FIGHTING },  // 652 grass/fighting
  { "FENNEKIN", 654, 16, R_RARO, 0xEA87, 40, 45, 40, 60, 62, 60, 3, T_FIRE, T_NONE },  // 653 fire
  { "BRAIXEN", 655, 36, R_EVO, 0xEA87, 59, 59, 58, 73, 90, 70, 3, T_FIRE, T_NONE },  // 654 fire
  { "DELPHOX", 0, 0, R_EVO, 0xEA87, 75, 69, 72, 104, 114, 100, 3, T_FIRE, T_PSYCHIC },  // 655 fire/psychic
  { "FROAKIE", 657, 16, R_RARO, 0x4C98, 41, 56, 40, 71, 62, 44, 1, T_WATER, T_NONE },  // 656 water
  { "FROGADIER", 658, 36, R_EVO, 0x4C98, 54, 63, 52, 97, 83, 56, 1, T_WATER, T_NONE },  // 657 water
  { "GRENINJA", 0, 0, R_EVO, 0x4C98, 72, 95, 67, 122, 103, 71, 1, T_WATER, T_DARK },  // 658 water/dark
  { "BUNNELBY", 660, 20, R_COMUN, 0x8C4D, 38, 36, 38, 57, 32, 36, 0, T_NORMAL, T_NONE },  // 659 normal
  { "DIGGERSBY", 0, 0, R_EVO, 0x8C4D, 85, 56, 77, 78, 50, 77, 0, T_NORMAL, T_GROUND },  // 660 normal/ground
  { "FLETCHLING", 662, 17, R_COMUN, 0x8C4D, 45, 50, 43, 62, 40, 38, 0, T_NORMAL, T_FLYING },  // 661 normal/flying
  { "FLETCHINDER", 663, 35, R_EVO, 0xEA87, 62, 73, 55, 84, 56, 52, 3, T_FIRE, T_FLYING },  // 662 fire/flying
  { "TALONFLAME", 0, 0, R_EVO, 0xEA87, 78, 81, 71, 126, 74, 69, 3, T_FIRE, T_FLYING },  // 663 fire/flying
  { "SCATTERBUG", 665, 9, R_COMUN, 0x7CC4, 38, 35, 40, 35, 27, 25, 2, T_BUG, T_NONE },  // 664 bug
  { "SPEWPA", 666, 12, R_EVO, 0x7CC4, 45, 22, 60, 29, 27, 30, 2, T_BUG, T_NONE },  // 665 bug
  { "VIVILLON", 0, 0, R_EVO, 0x7CC4, 80, 52, 50, 89, 90, 50, 2, T_BUG, T_FLYING },  // 666 bug/flying
  { "LITLEO", 668, 35, R_COMUN, 0xEA87, 62, 50, 58, 72, 73, 54, 3, T_FIRE, T_NORMAL },  // 667 fire/normal
  { "PYROAR", 0, 0, R_EVO, 0xEA87, 86, 68, 72, 106, 109, 66, 3, T_FIRE, T_NORMAL },  // 668 fire/normal
  { "FLABEBE", 670, 19, R_COMUN, 0xC333, 44, 38, 39, 42, 61, 79, 0, T_FAIRY, T_NONE },  // 669 fairy
  { "FLOETTE", 671, 30, R_EVO, 0xC333, 54, 45, 47, 52, 75, 98, 0, T_FAIRY, T_NONE },  // 670 fairy
  { "FLORGES", 0, 0, R_EVO, 0xC333, 78, 65, 68, 75, 112, 154, 0, T_FAIRY, T_NONE },  // 671 fairy
  { "SKIDDO", 673, 32, R_COMUN, 0x3C49, 66, 65, 48, 52, 62, 57, 2, T_GRASS, T_NONE },  // 672 grass
  { "GOGOAT", 0, 0, R_EVO, 0x3C49, 123, 100, 62, 68, 97, 81, 2, T_GRASS, T_NONE },  // 673 grass
  { "PANCHAM", 675, 32, R_COMUN, 0xA2A5, 67, 82, 62, 43, 46, 48, 0, T_FIGHTING, T_NONE },  // 674 fighting
  { "PANGORO", 0, 0, R_EVO, 0xA2A5, 95, 124, 78, 58, 69, 71, 0, T_FIGHTING, T_DARK },  // 675 fighting/dark
  { "FURFROU", 0, 0, R_COMUN, 0x8C4D, 75, 80, 60, 102, 65, 90, 0, T_NORMAL, T_NONE },  // 676 normal
  { "ESPURR", 678, 25, R_COMUN, 0xD28F, 62, 48, 54, 68, 63, 60, 0, T_PSYCHIC, T_NONE },  // 677 psychic
  { "MEOWSTIC", 0, 0, R_EVO, 0xD28F, 74, 48, 76, 104, 83, 81, 0, T_PSYCHIC, T_NONE },  // 678 psychic
  { "HONEDGE", 680, 35, R_COMUN, 0x6BF1, 45, 80, 100, 28, 35, 37, 4, T_STEEL, T_GHOST },  // 679 steel/ghost
  { "DOUBLADE", 681, 30, R_EVO, 0x6BF1, 59, 110, 150, 35, 45, 49, 4, T_STEEL, T_GHOST },  // 680 steel/ghost
  { "AEGISLASH", 0, 0, R_EVO, 0x6BF1, 60, 50, 140, 60, 50, 140, 4, T_STEEL, T_GHOST },  // 681 steel/ghost
  { "SPRITZEE", 683, 40, R_COMUN, 0xC333, 78, 52, 60, 23, 63, 65, 0, T_FAIRY, T_NONE },  // 682 fairy
  { "AROMATISSE", 0, 0, R_EVO, 0xC333, 101, 72, 72, 29, 99, 89, 0, T_FAIRY, T_NONE },  // 683 fairy
  { "SWIRLIX", 685, 40, R_COMUN, 0xC333, 62, 48, 66, 49, 59, 57, 0, T_FAIRY, T_NONE },  // 684 fairy
  { "SLURPUFF", 0, 0, R_EVO, 0xC333, 82, 80, 86, 72, 85, 75, 0, T_FAIRY, T_NONE },  // 685 fairy
  { "INKAY", 687, 30, R_COMUN, 0x5A47, 53, 54, 53, 45, 37, 46, 2, T_DARK, T_PSYCHIC },  // 686 dark/psychic
  { "MALAMAR", 0, 0, R_EVO, 0x5A47, 86, 92, 88, 73, 68, 75, 2, T_DARK, T_PSYCHIC },  // 687 dark/psychic
  { "BINACLE", 689, 39, R_COMUN, 0x9407, 42, 52, 67, 50, 39, 56, 4, T_ROCK, T_WATER },  // 688 rock/water
  { "BARBARACLE", 0, 0, R_EVO, 0x9407, 72, 105, 115, 68, 54, 86, 4, T_ROCK, T_WATER },  // 689 rock/water
  { "SKRELP", 691, 48, R_COMUN, 0x8A73, 50, 60, 60, 30, 60, 60, 0, T_POISON, T_WATER },  // 690 poison/water
  { "DRAGALGE", 0, 0, R_EVO, 0x8A73, 65, 75, 90, 44, 97, 123, 0, T_POISON, T_DRAGON },  // 691 poison/dragon
  { "CLAUNCHER", 693, 37, R_COMUN, 0x4C98, 50, 53, 62, 44, 58, 63, 1, T_WATER, T_NONE },  // 692 water
  { "CLAWITZER", 0, 0, R_EVO, 0x4C98, 71, 73, 88, 59, 120, 89, 1, T_WATER, T_NONE },  // 693 water
  { "HELIOPTILE", 695, 30, R_COMUN, 0xBCA1, 44, 38, 33, 70, 61, 43, 0, T_ELECTRIC, T_NORMAL },  // 694 electric/normal
  { "HELIOLISK", 0, 0, R_EVO, 0xBCA1, 62, 55, 52, 109, 109, 94, 0, T_ELECTRIC, T_NORMAL },  // 695 electric/normal
  { "TYRUNT", 697, 39, R_RARO, 0x9407, 58, 89, 77, 48, 45, 45, 4, T_ROCK, T_DRAGON },  // 696 rock/dragon
  { "TYRANTRUM", 0, 0, R_EVO, 0x9407, 82, 121, 119, 71, 69, 59, 4, T_ROCK, T_DRAGON },  // 697 rock/dragon
  { "AMAURA", 699, 39, R_RARO, 0x9407, 77, 59, 50, 46, 67, 63, 4, T_ROCK, T_ICE },  // 698 rock/ice
  { "AURORUS", 0, 0, R_EVO, 0x9407, 123, 77, 72, 58, 99, 92, 4, T_ROCK, T_ICE },  // 699 rock/ice
  { "SYLVEON", 0, 0, R_EVO, 0xC333, 95, 65, 65, 60, 110, 130, 0, T_FAIRY, T_NONE },  // 700 fairy
  { "HAWLUCHA", 0, 0, R_COMUN, 0xA2A5, 78, 92, 75, 118, 74, 63, 0, T_FIGHTING, T_FLYING },  // 701 fighting/flying
  { "DEDENNE", 0, 0, R_COMUN, 0xBCA1, 67, 58, 57, 101, 81, 67, 0, T_ELECTRIC, T_FAIRY },  // 702 electric/fairy
  { "CARBINK", 0, 0, R_COMUN, 0x9407, 50, 50, 150, 50, 50, 150, 4, T_ROCK, T_FAIRY },  // 703 rock/fairy
  { "GOOMY", 705, 40, R_RARO, 0x5A98, 45, 50, 35, 40, 55, 75, 1, T_DRAGON, T_NONE },  // 704 dragon
  { "SLIGGOO", 706, 50, R_EVO, 0x5A98, 68, 75, 53, 60, 83, 113, 1, T_DRAGON, T_NONE },  // 705 dragon
  { "GOODRA", 0, 0, R_EVO, 0x5A98, 90, 100, 70, 80, 110, 150, 1, T_DRAGON, T_NONE },  // 706 dragon
  { "KLEFKI", 0, 0, R_COMUN, 0x6BF1, 57, 80, 91, 75, 80, 87, 4, T_STEEL, T_FAIRY },  // 707 steel/fairy
  { "PHANTUMP", 709, 40, R_COMUN, 0x6AD3, 43, 70, 48, 38, 50, 60, 0, T_GHOST, T_GRASS },  // 708 ghost/grass
  { "TREVENANT", 0, 0, R_EVO, 0x6AD3, 85, 110, 76, 56, 65, 82, 0, T_GHOST, T_GRASS },  // 709 ghost/grass
  { "PUMPKABOO", 711, 40, R_COMUN, 0x6AD3, 49, 66, 70, 51, 44, 55, 0, T_GHOST, T_GRASS },  // 710 ghost/grass
  { "GOURGEIST", 0, 0, R_EVO, 0x6AD3, 65, 90, 122, 84, 58, 75, 0, T_GHOST, T_GRASS },  // 711 ghost/grass
  { "BERGMITE", 713, 37, R_COMUN, 0x4DB8, 55, 69, 85, 28, 32, 35, 5, T_ICE, T_NONE },  // 712 ice
  { "AVALUGG", 0, 0, R_EVO, 0x4DB8, 95, 117, 184, 28, 44, 46, 5, T_ICE, T_NONE },  // 713 ice
  { "NOIBAT", 715, 48, R_COMUN, 0x7C7A, 40, 30, 35, 55, 45, 40, 0, T_FLYING, T_DRAGON },  // 714 flying/dragon
  { "NOIVERN", 0, 0, R_EVO, 0x7C7A, 85, 70, 80, 123, 97, 80, 0, T_FLYING, T_DRAGON },  // 715 flying/dragon
  { "XERNEAS", 0, 0, R_LEGENDARIO, 0xC333, 126, 131, 95, 99, 131, 98, 0, T_FAIRY, T_NONE },  // 716 fairy
  { "YVELTAL", 0, 0, R_LEGENDARIO, 0x5A47, 126, 131, 95, 99, 131, 98, 2, T_DARK, T_FLYING },  // 717 dark/flying
  { "ZYGARDE", 0, 0, R_LEGENDARIO, 0x5A98, 108, 100, 121, 95, 81, 95, 1, T_DRAGON, T_GROUND },  // 718 dragon/ground
  { "DIANCIE", 0, 0, R_LEGENDARIO, 0x9407, 50, 100, 150, 50, 100, 150, 4, T_ROCK, T_FAIRY },  // 719 rock/fairy
  { "HOOPA", 0, 0, R_LEGENDARIO, 0xD28F, 80, 110, 60, 70, 150, 130, 0, T_PSYCHIC, T_GHOST },  // 720 psychic/ghost
  { "VOLCANION", 0, 0, R_LEGENDARIO, 0xEA87, 80, 110, 120, 70, 130, 90, 3, T_FIRE, T_WATER },  // 721 fire/water
  { "ROWLET", 723, 17, R_RARO, 0x3C49, 68, 55, 55, 42, 50, 50, 2, T_GRASS, T_FLYING },  // 722 grass/flying
  { "DARTRIX", 724, 34, R_EVO, 0x3C49, 78, 75, 75, 52, 70, 70, 2, T_GRASS, T_FLYING },  // 723 grass/flying
  { "DECIDUEYE", 0, 0, R_EVO, 0x3C49, 78, 107, 75, 70, 100, 100, 2, T_GRASS, T_GHOST },  // 724 grass/ghost
  { "LITTEN", 726, 17, R_RARO, 0xEA87, 45, 65, 40, 70, 60, 40, 3, T_FIRE, T_NONE },  // 725 fire
  { "TORRACAT", 727, 34, R_EVO, 0xEA87, 65, 85, 50, 90, 80, 50, 3, T_FIRE, T_NONE },  // 726 fire
  { "INCINEROAR", 0, 0, R_EVO, 0xEA87, 95, 115, 90, 60, 80, 90, 3, T_FIRE, T_DARK },  // 727 fire/dark
  { "POPPLIO", 729, 17, R_RARO, 0x4C98, 50, 54, 54, 40, 66, 56, 1, T_WATER, T_NONE },  // 728 water
  { "BRIONNE", 730, 34, R_EVO, 0x4C98, 60, 69, 69, 50, 91, 81, 1, T_WATER, T_NONE },  // 729 water
  { "PRIMARINA", 0, 0, R_EVO, 0x4C98, 80, 74, 74, 60, 126, 116, 1, T_WATER, T_FAIRY },  // 730 water/fairy
  { "PIKIPEK", 732, 14, R_COMUN, 0x8C4D, 35, 75, 30, 65, 30, 30, 0, T_NORMAL, T_FLYING },  // 731 normal/flying
  { "TRUMBEAK", 733, 28, R_EVO, 0x8C4D, 55, 85, 50, 75, 40, 50, 0, T_NORMAL, T_FLYING },  // 732 normal/flying
  { "TOUCANNON", 0, 0, R_EVO, 0x8C4D, 80, 120, 75, 60, 75, 75, 0, T_NORMAL, T_FLYING },  // 733 normal/flying
  { "YUNGOOS", 735, 20, R_COMUN, 0x8C4D, 48, 70, 30, 45, 30, 30, 0, T_NORMAL, T_NONE },  // 734 normal
  { "GUMSHOOS", 0, 0, R_EVO, 0x8C4D, 88, 110, 60, 45, 55, 60, 0, T_NORMAL, T_NONE },  // 735 normal
  { "GRUBBIN", 737, 20, R_COMUN, 0x7CC4, 47, 62, 45, 46, 55, 45, 2, T_BUG, T_NONE },  // 736 bug
  { "CHARJABUG", 738, 30, R_EVO, 0x7CC4, 57, 82, 95, 36, 55, 75, 2, T_BUG, T_ELECTRIC },  // 737 bug/electric
  { "VIKAVOLT", 0, 0, R_EVO, 0x7CC4, 77, 70, 90, 43, 145, 75, 2, T_BUG, T_ELECTRIC },  // 738 bug/electric
  { "CRABRAWLER", 740, 30, R_COMUN, 0xA2A5, 47, 82, 57, 63, 42, 47, 0, T_FIGHTING, T_NONE },  // 739 fighting
  { "CRABOMINABLE", 0, 0, R_EVO, 0xA2A5, 97, 132, 77, 43, 62, 67, 0, T_FIGHTING, T_ICE },  // 740 fighting/ice
  { "ORICORIO", 0, 0, R_RARO, 0xEA87, 75, 70, 70, 93, 98, 70, 3, T_FIRE, T_FLYING },  // 741 fire/flying
  { "CUTIEFLY", 743, 25, R_COMUN, 0x7CC4, 40, 45, 40, 84, 55, 40, 2, T_BUG, T_FAIRY },  // 742 bug/fairy
  { "RIBOMBEE", 0, 0, R_EVO, 0x7CC4, 60, 55, 60, 124, 95, 70, 2, T_BUG, T_FAIRY },  // 743 bug/fairy
  { "ROCKRUFF", 745, 25, R_COMUN, 0x9407, 45, 65, 40, 60, 30, 40, 4, T_ROCK, T_NONE },  // 744 rock
  { "LYCANROC", 0, 0, R_EVO, 0x9407, 75, 115, 65, 112, 55, 65, 4, T_ROCK, T_NONE },  // 745 rock
  { "WISHIWASHI", 0, 0, R_COMUN, 0x4C98, 45, 20, 20, 40, 25, 25, 1, T_WATER, T_NONE },  // 746 water
  { "MAREANIE", 748, 38, R_COMUN, 0x8A73, 50, 53, 62, 45, 43, 52, 0, T_POISON, T_WATER },  // 747 poison/water
  { "TOXAPEX", 0, 0, R_EVO, 0x8A73, 50, 63, 152, 35, 53, 142, 0, T_POISON, T_WATER },  // 748 poison/water
  { "MUDBRAY", 750, 30, R_COMUN, 0xB447, 70, 100, 70, 45, 45, 55, 4, T_GROUND, T_NONE },  // 749 ground
  { "MUDSDALE", 0, 0, R_EVO, 0xB447, 100, 125, 100, 35, 55, 85, 4, T_GROUND, T_NONE },  // 750 ground
  { "DEWPIDER", 752, 22, R_COMUN, 0x4C98, 38, 40, 52, 27, 40, 72, 1, T_WATER, T_BUG },  // 751 water/bug
  { "ARAQUANID", 0, 0, R_EVO, 0x4C98, 68, 70, 92, 42, 50, 132, 1, T_WATER, T_BUG },  // 752 water/bug
  { "FOMANTIS", 754, 34, R_COMUN, 0x3C49, 40, 55, 35, 35, 50, 35, 2, T_GRASS, T_NONE },  // 753 grass
  { "LURANTIS", 0, 0, R_EVO, 0x3C49, 70, 105, 90, 45, 80, 90, 2, T_GRASS, T_NONE },  // 754 grass
  { "MORELULL", 756, 24, R_COMUN, 0x3C49, 40, 35, 55, 15, 65, 75, 2, T_GRASS, T_FAIRY },  // 755 grass/fairy
  { "SHIINOTIC", 0, 0, R_EVO, 0x3C49, 60, 45, 80, 30, 90, 100, 2, T_GRASS, T_FAIRY },  // 756 grass/fairy
  { "SALANDIT", 758, 33, R_COMUN, 0x8A73, 48, 44, 40, 77, 71, 40, 0, T_POISON, T_FIRE },  // 757 poison/fire
  { "SALAZZLE", 0, 0, R_EVO, 0x8A73, 68, 64, 60, 117, 111, 60, 0, T_POISON, T_FIRE },  // 758 poison/fire
  { "STUFFUL", 760, 27, R_COMUN, 0x8C4D, 70, 75, 50, 50, 45, 50, 0, T_NORMAL, T_FIGHTING },  // 759 normal/fighting
  { "BEWEAR", 0, 0, R_EVO, 0x8C4D, 120, 125, 80, 60, 55, 60, 0, T_NORMAL, T_FIGHTING },  // 760 normal/fighting
  { "BOUNSWEET", 762, 18, R_COMUN, 0x3C49, 42, 30, 38, 32, 30, 38, 2, T_GRASS, T_NONE },  // 761 grass
  { "STEENEE", 763, 30, R_EVO, 0x3C49, 52, 40, 48, 62, 40, 48, 2, T_GRASS, T_NONE },  // 762 grass
  { "TSAREENA", 0, 0, R_EVO, 0x3C49, 72, 120, 98, 72, 50, 98, 2, T_GRASS, T_NONE },  // 763 grass
  { "COMFEY", 0, 0, R_COMUN, 0xC333, 51, 52, 90, 100, 82, 110, 0, T_FAIRY, T_NONE },  // 764 fairy
  { "ORANGURU", 0, 0, R_RARO, 0x8C4D, 90, 60, 80, 60, 90, 110, 0, T_NORMAL, T_PSYCHIC },  // 765 normal/psychic
  { "PASSIMIAN", 0, 0, R_RARO, 0xA2A5, 100, 120, 90, 80, 40, 60, 0, T_FIGHTING, T_NONE },  // 766 fighting
  { "WIMPOD", 768, 30, R_COMUN, 0x7CC4, 25, 35, 40, 80, 20, 30, 2, T_BUG, T_WATER },  // 767 bug/water
  { "GOLISOPOD", 0, 0, R_EVO, 0x7CC4, 75, 125, 140, 40, 60, 90, 2, T_BUG, T_WATER },  // 768 bug/water
  { "SANDYGAST", 770, 42, R_COMUN, 0x6AD3, 55, 55, 80, 15, 70, 45, 0, T_GHOST, T_GROUND },  // 769 ghost/ground
  { "PALOSSAND", 0, 0, R_EVO, 0x6AD3, 85, 75, 110, 35, 100, 75, 0, T_GHOST, T_GROUND },  // 770 ghost/ground
  { "PYUKUMUKU", 0, 0, R_COMUN, 0x4C98, 55, 60, 130, 5, 30, 130, 1, T_WATER, T_NONE },  // 771 water
  { "TYPE NULL", 773, 25, R_LEGENDARIO, 0x8C4D, 95, 95, 95, 59, 95, 95, 0, T_NORMAL, T_NONE },  // 772 normal
  { "SILVALLY", 0, 0, R_EVO, 0x8C4D, 95, 95, 95, 95, 95, 95, 0, T_NORMAL, T_NONE },  // 773 normal
  { "MINIOR", 0, 0, R_RARO, 0x9407, 60, 60, 100, 60, 60, 100, 4, T_ROCK, T_FLYING },  // 774 rock/flying
  { "KOMALA", 0, 0, R_RARO, 0x8C4D, 65, 115, 65, 65, 75, 95, 0, T_NORMAL, T_NONE },  // 775 normal
  { "TURTONATOR", 0, 0, R_COMUN, 0xEA87, 60, 78, 135, 36, 91, 85, 3, T_FIRE, T_DRAGON },  // 776 fire/dragon
  { "TOGEDEMARU", 0, 0, R_COMUN, 0xBCA1, 65, 98, 63, 96, 40, 73, 0, T_ELECTRIC, T_STEEL },  // 777 electric/steel
  { "MIMIKYU", 0, 0, R_RARO, 0x6AD3, 55, 90, 80, 96, 50, 105, 0, T_GHOST, T_FAIRY },  // 778 ghost/fairy
  { "BRUXISH", 0, 0, R_COMUN, 0x4C98, 68, 105, 70, 92, 70, 70, 1, T_WATER, T_PSYCHIC },  // 779 water/psychic
  { "DRAMPA", 0, 0, R_COMUN, 0x8C4D, 78, 60, 85, 36, 135, 91, 0, T_NORMAL, T_DRAGON },  // 780 normal/dragon
  { "DHELMISE", 0, 0, R_RARO, 0x6AD3, 70, 131, 100, 40, 86, 90, 0, T_GHOST, T_GRASS },  // 781 ghost/grass
  { "JANGMO O", 783, 35, R_RARO, 0x5A98, 45, 55, 65, 45, 45, 45, 1, T_DRAGON, T_NONE },  // 782 dragon
  { "HAKAMO O", 784, 45, R_EVO, 0x5A98, 55, 75, 90, 65, 65, 70, 1, T_DRAGON, T_FIGHTING },  // 783 dragon/fighting
  { "KOMMO O", 0, 0, R_EVO, 0x5A98, 75, 110, 125, 85, 100, 105, 1, T_DRAGON, T_FIGHTING },  // 784 dragon/fighting
  { "TAPU KOKO", 0, 0, R_LEGENDARIO, 0xBCA1, 70, 115, 85, 130, 95, 75, 0, T_ELECTRIC, T_FAIRY },  // 785 electric/fairy
  { "TAPU LELE", 0, 0, R_LEGENDARIO, 0xD28F, 70, 85, 75, 95, 130, 115, 0, T_PSYCHIC, T_FAIRY },  // 786 psychic/fairy
  { "TAPU BULU", 0, 0, R_LEGENDARIO, 0x3C49, 70, 130, 115, 75, 85, 95, 2, T_GRASS, T_FAIRY },  // 787 grass/fairy
  { "TAPU FINI", 0, 0, R_LEGENDARIO, 0x4C98, 70, 75, 115, 85, 95, 130, 1, T_WATER, T_FAIRY },  // 788 water/fairy
  { "COSMOG", 790, 43, R_LEGENDARIO, 0xD28F, 43, 29, 31, 37, 29, 31, 0, T_PSYCHIC, T_NONE },  // 789 psychic
  { "COSMOEM", 791, 53, R_EVO, 0xD28F, 43, 29, 131, 37, 29, 131, 0, T_PSYCHIC, T_NONE },  // 790 psychic
  { "SOLGALEO", 0, 0, R_EVO, 0xD28F, 137, 137, 107, 97, 113, 89, 0, T_PSYCHIC, T_STEEL },  // 791 psychic/steel
  { "LUNALA", 0, 0, R_LEGENDARIO, 0xD28F, 137, 113, 89, 97, 137, 107, 0, T_PSYCHIC, T_GHOST },  // 792 psychic/ghost
  { "NIHILEGO", 0, 0, R_RARO, 0x9407, 109, 53, 47, 103, 127, 131, 4, T_ROCK, T_POISON },  // 793 rock/poison
  { "BUZZWOLE", 0, 0, R_RARO, 0x7CC4, 107, 139, 139, 79, 53, 53, 2, T_BUG, T_FIGHTING },  // 794 bug/fighting
  { "PHEROMOSA", 0, 0, R_RARO, 0x7CC4, 71, 137, 37, 151, 137, 37, 2, T_BUG, T_FIGHTING },  // 795 bug/fighting
  { "XURKITREE", 0, 0, R_RARO, 0xBCA1, 83, 89, 71, 83, 173, 71, 0, T_ELECTRIC, T_NONE },  // 796 electric
  { "CELESTEELA", 0, 0, R_RARO, 0x6BF1, 97, 101, 103, 61, 107, 101, 4, T_STEEL, T_FLYING },  // 797 steel/flying
  { "KARTANA", 0, 0, R_RARO, 0x3C49, 59, 181, 131, 109, 59, 31, 2, T_GRASS, T_STEEL },  // 798 grass/steel
  { "GUZZLORD", 0, 0, R_RARO, 0x5A47, 223, 101, 53, 43, 97, 53, 2, T_DARK, T_DRAGON },  // 799 dark/dragon
  { "NECROZMA", 0, 0, R_LEGENDARIO, 0xD28F, 97, 107, 101, 79, 127, 89, 0, T_PSYCHIC, T_NONE },  // 800 psychic
  { "MAGEARNA", 0, 0, R_LEGENDARIO, 0x6BF1, 80, 95, 115, 65, 130, 115, 4, T_STEEL, T_FAIRY },  // 801 steel/fairy
  { "MARSHADOW", 0, 0, R_LEGENDARIO, 0xA2A5, 90, 125, 80, 125, 90, 90, 0, T_FIGHTING, T_GHOST },  // 802 fighting/ghost
  { "POIPOLE", 804, 30, R_RARO, 0x8A73, 67, 73, 67, 73, 73, 67, 0, T_POISON, T_NONE },  // 803 poison
  { "NAGANADEL", 0, 0, R_EVO, 0x8A73, 73, 73, 73, 121, 127, 73, 0, T_POISON, T_DRAGON },  // 804 poison/dragon
  { "STAKATAKA", 0, 0, R_RARO, 0x9407, 61, 131, 211, 13, 53, 101, 4, T_ROCK, T_STEEL },  // 805 rock/steel
  { "BLACEPHALON", 0, 0, R_RARO, 0xEA87, 53, 127, 53, 107, 151, 79, 3, T_FIRE, T_GHOST },  // 806 fire/ghost
  { "ZERAORA", 0, 0, R_LEGENDARIO, 0xBCA1, 88, 112, 75, 143, 102, 80, 0, T_ELECTRIC, T_NONE },  // 807 electric
  { "MELTAN", 0, 0, R_LEGENDARIO, 0x6BF1, 46, 65, 65, 34, 55, 35, 4, T_STEEL, T_NONE },  // 808 steel
  { "MELMETAL", 0, 0, R_LEGENDARIO, 0x6BF1, 135, 143, 143, 34, 80, 65, 4, T_STEEL, T_NONE },  // 809 steel
  { "GROOKEY", 811, 16, R_RARO, 0x3C49, 50, 65, 50, 65, 40, 40, 2, T_GRASS, T_NONE },  // 810 grass
  { "THWACKEY", 812, 35, R_EVO, 0x3C49, 70, 85, 70, 80, 55, 60, 2, T_GRASS, T_NONE },  // 811 grass
  { "RILLABOOM", 0, 0, R_EVO, 0x3C49, 100, 125, 90, 85, 60, 70, 2, T_GRASS, T_NONE },  // 812 grass
  { "SCORBUNNY", 814, 16, R_RARO, 0xEA87, 50, 71, 40, 69, 40, 40, 3, T_FIRE, T_NONE },  // 813 fire
  { "RABOOT", 815, 35, R_EVO, 0xEA87, 65, 86, 60, 94, 55, 60, 3, T_FIRE, T_NONE },  // 814 fire
  { "CINDERACE", 0, 0, R_EVO, 0xEA87, 80, 116, 75, 119, 65, 75, 3, T_FIRE, T_NONE },  // 815 fire
  { "SOBBLE", 817, 16, R_RARO, 0x4C98, 50, 40, 40, 70, 70, 40, 1, T_WATER, T_NONE },  // 816 water
  { "DRIZZILE", 818, 35, R_EVO, 0x4C98, 65, 60, 55, 90, 95, 55, 1, T_WATER, T_NONE },  // 817 water
  { "INTELEON", 0, 0, R_EVO, 0x4C98, 70, 85, 65, 120, 125, 65, 1, T_WATER, T_NONE },  // 818 water
  { "SKWOVET", 820, 24, R_COMUN, 0x8C4D, 70, 55, 55, 25, 35, 35, 0, T_NORMAL, T_NONE },  // 819 normal
  { "GREEDENT", 0, 0, R_EVO, 0x8C4D, 120, 95, 95, 20, 55, 75, 0, T_NORMAL, T_NONE },  // 820 normal
  { "ROOKIDEE", 822, 18, R_COMUN, 0x7C7A, 38, 47, 35, 57, 33, 35, 0, T_FLYING, T_NONE },  // 821 flying
  { "CORVISQUIRE", 823, 38, R_EVO, 0x7C7A, 68, 67, 55, 77, 43, 55, 0, T_FLYING, T_NONE },  // 822 flying
  { "CORVIKNIGHT", 0, 0, R_EVO, 0x7C7A, 98, 87, 105, 67, 53, 85, 0, T_FLYING, T_STEEL },  // 823 flying/steel
  { "BLIPBUG", 825, 10, R_COMUN, 0x7CC4, 25, 20, 20, 45, 25, 45, 2, T_BUG, T_NONE },  // 824 bug
  { "DOTTLER", 826, 30, R_EVO, 0x7CC4, 50, 35, 80, 30, 50, 90, 2, T_BUG, T_PSYCHIC },  // 825 bug/psychic
  { "ORBEETLE", 0, 0, R_EVO, 0x7CC4, 60, 45, 110, 90, 80, 120, 2, T_BUG, T_PSYCHIC },  // 826 bug/psychic
  { "NICKIT", 828, 18, R_COMUN, 0x5A47, 40, 28, 28, 50, 47, 52, 2, T_DARK, T_NONE },  // 827 dark
  { "THIEVUL", 0, 0, R_EVO, 0x5A47, 70, 58, 58, 90, 87, 92, 2, T_DARK, T_NONE },  // 828 dark
  { "GOSSIFLEUR", 830, 20, R_COMUN, 0x3C49, 40, 40, 60, 10, 40, 60, 2, T_GRASS, T_NONE },  // 829 grass
  { "ELDEGOSS", 0, 0, R_EVO, 0x3C49, 60, 50, 90, 60, 80, 120, 2, T_GRASS, T_NONE },  // 830 grass
  { "WOOLOO", 832, 24, R_COMUN, 0x8C4D, 42, 40, 55, 48, 40, 45, 0, T_NORMAL, T_NONE },  // 831 normal
  { "DUBWOOL", 0, 0, R_EVO, 0x8C4D, 72, 80, 100, 88, 60, 90, 0, T_NORMAL, T_NONE },  // 832 normal
  { "CHEWTLE", 834, 22, R_COMUN, 0x4C98, 50, 64, 50, 44, 38, 38, 1, T_WATER, T_NONE },  // 833 water
  { "DREDNAW", 0, 0, R_EVO, 0x4C98, 90, 115, 90, 74, 48, 68, 1, T_WATER, T_ROCK },  // 834 water/rock
  { "YAMPER", 836, 25, R_COMUN, 0xBCA1, 59, 45, 50, 26, 40, 50, 0, T_ELECTRIC, T_NONE },  // 835 electric
  { "BOLTUND", 0, 0, R_EVO, 0xBCA1, 69, 90, 60, 121, 90, 60, 0, T_ELECTRIC, T_NONE },  // 836 electric
  { "ROLYCOLY", 838, 18, R_COMUN, 0x9407, 30, 40, 50, 30, 40, 50, 4, T_ROCK, T_NONE },  // 837 rock
  { "CARKOL", 839, 34, R_EVO, 0x9407, 80, 60, 90, 50, 60, 70, 4, T_ROCK, T_FIRE },  // 838 rock/fire
  { "COALOSSAL", 0, 0, R_EVO, 0x9407, 110, 80, 120, 30, 80, 90, 4, T_ROCK, T_FIRE },  // 839 rock/fire
  { "APPLIN", 841, 30, R_COMUN, 0x3C49, 40, 40, 80, 20, 40, 40, 2, T_GRASS, T_DRAGON },  // 840 grass/dragon
  { "FLAPPLE", 0, 0, R_EVO, 0x3C49, 70, 110, 80, 70, 95, 60, 2, T_GRASS, T_DRAGON },  // 841 grass/dragon
  { "APPLETUN", 0, 0, R_RARO, 0x3C49, 110, 85, 80, 30, 100, 80, 2, T_GRASS, T_DRAGON },  // 842 grass/dragon
  { "SILICOBRA", 844, 36, R_COMUN, 0xB447, 52, 57, 75, 46, 35, 50, 4, T_GROUND, T_NONE },  // 843 ground
  { "SANDACONDA", 0, 0, R_EVO, 0xB447, 72, 107, 125, 71, 65, 70, 4, T_GROUND, T_NONE },  // 844 ground
  { "CRAMORANT", 0, 0, R_RARO, 0x7C7A, 70, 85, 55, 85, 85, 95, 0, T_FLYING, T_WATER },  // 845 flying/water
  { "ARROKUDA", 847, 26, R_COMUN, 0x4C98, 41, 63, 40, 66, 40, 30, 1, T_WATER, T_NONE },  // 846 water
  { "BARRASKEWDA", 0, 0, R_EVO, 0x4C98, 61, 123, 60, 136, 60, 50, 1, T_WATER, T_NONE },  // 847 water
  { "TOXEL", 849, 30, R_COMUN, 0xBCA1, 40, 38, 35, 40, 54, 35, 0, T_ELECTRIC, T_POISON },  // 848 electric/poison
  { "TOXTRICITY", 0, 0, R_EVO, 0xBCA1, 75, 98, 70, 75, 114, 70, 0, T_ELECTRIC, T_POISON },  // 849 electric/poison
  { "SIZZLIPEDE", 851, 28, R_COMUN, 0xEA87, 50, 65, 45, 45, 50, 50, 3, T_FIRE, T_BUG },  // 850 fire/bug
  { "CENTISKORCH", 0, 0, R_EVO, 0xEA87, 100, 115, 65, 65, 90, 90, 3, T_FIRE, T_BUG },  // 851 fire/bug
  { "CLOBBOPUS", 853, 30, R_COMUN, 0xA2A5, 50, 68, 60, 32, 50, 50, 0, T_FIGHTING, T_NONE },  // 852 fighting
  { "GRAPPLOCT", 0, 0, R_EVO, 0xA2A5, 80, 118, 90, 42, 70, 80, 0, T_FIGHTING, T_NONE },  // 853 fighting
  { "SINISTEA", 855, 30, R_COMUN, 0x6AD3, 40, 45, 45, 50, 74, 54, 0, T_GHOST, T_NONE },  // 854 ghost
  { "POLTEAGEIST", 0, 0, R_EVO, 0x6AD3, 60, 65, 65, 70, 134, 114, 0, T_GHOST, T_NONE },  // 855 ghost
  { "HATENNA", 857, 32, R_COMUN, 0xD28F, 42, 30, 45, 39, 56, 53, 0, T_PSYCHIC, T_NONE },  // 856 psychic
  { "HATTREM", 858, 42, R_EVO, 0xD28F, 57, 40, 65, 49, 86, 73, 0, T_PSYCHIC, T_NONE },  // 857 psychic
  { "HATTERENE", 0, 0, R_EVO, 0xD28F, 57, 90, 95, 29, 136, 103, 0, T_PSYCHIC, T_FAIRY },  // 858 psychic/fairy
  { "IMPIDIMP", 860, 32, R_COMUN, 0x5A47, 45, 45, 30, 50, 55, 40, 2, T_DARK, T_FAIRY },  // 859 dark/fairy
  { "MORGREM", 861, 42, R_EVO, 0x5A47, 65, 60, 45, 70, 75, 55, 2, T_DARK, T_FAIRY },  // 860 dark/fairy
  { "GRIMMSNARL", 0, 0, R_EVO, 0x5A47, 95, 120, 65, 60, 95, 75, 2, T_DARK, T_FAIRY },  // 861 dark/fairy
  { "OBSTAGOON", 0, 0, R_EVO, 0x5A47, 93, 90, 101, 95, 60, 81, 2, T_DARK, T_NORMAL },  // 862 dark/normal
  { "PERRSERKER", 0, 0, R_COMUN, 0x6BF1, 70, 110, 100, 50, 50, 60, 4, T_STEEL, T_NONE },  // 863 steel
  { "CURSOLA", 0, 0, R_EVO, 0x6AD3, 60, 95, 50, 30, 145, 130, 0, T_GHOST, T_NONE },  // 864 ghost
  { "SIRFETCHD", 0, 0, R_EVO, 0xA2A5, 62, 135, 95, 65, 68, 82, 0, T_FIGHTING, T_NONE },  // 865 fighting
  { "MR RIME", 0, 0, R_EVO, 0x4DB8, 80, 85, 75, 70, 110, 100, 5, T_ICE, T_PSYCHIC },  // 866 ice/psychic
  { "RUNERIGUS", 0, 0, R_COMUN, 0xB447, 58, 95, 145, 30, 50, 105, 4, T_GROUND, T_GHOST },  // 867 ground/ghost
  { "MILCERY", 869, 30, R_COMUN, 0xC333, 45, 40, 40, 34, 50, 61, 0, T_FAIRY, T_NONE },  // 868 fairy
  { "ALCREMIE", 0, 0, R_EVO, 0xC333, 65, 60, 75, 64, 110, 121, 0, T_FAIRY, T_NONE },  // 869 fairy
  { "FALINKS", 0, 0, R_RARO, 0xA2A5, 65, 100, 100, 75, 70, 60, 0, T_FIGHTING, T_NONE },  // 870 fighting
  { "PINCURCHIN", 0, 0, R_COMUN, 0xBCA1, 48, 101, 95, 15, 91, 85, 0, T_ELECTRIC, T_NONE },  // 871 electric
  { "SNOM", 873, 25, R_COMUN, 0x4DB8, 30, 25, 35, 20, 45, 30, 5, T_ICE, T_BUG },  // 872 ice/bug
  { "FROSMOTH", 0, 0, R_EVO, 0x4DB8, 70, 65, 60, 65, 125, 90, 5, T_ICE, T_BUG },  // 873 ice/bug
  { "STONJOURNER", 0, 0, R_COMUN, 0x9407, 100, 125, 135, 70, 20, 20, 4, T_ROCK, T_NONE },  // 874 rock
  { "EISCUE", 0, 0, R_COMUN, 0x4DB8, 75, 80, 110, 50, 65, 90, 5, T_ICE, T_NONE },  // 875 ice
  { "INDEEDEE", 0, 0, R_RARO, 0xD28F, 60, 65, 55, 95, 105, 95, 0, T_PSYCHIC, T_NORMAL },  // 876 psychic/normal
  { "MORPEKO", 0, 0, R_COMUN, 0xBCA1, 58, 95, 58, 97, 70, 58, 0, T_ELECTRIC, T_DARK },  // 877 electric/dark
  { "CUFANT", 879, 34, R_COMUN, 0x6BF1, 72, 80, 49, 40, 40, 49, 4, T_STEEL, T_NONE },  // 878 steel
  { "COPPERAJAH", 0, 0, R_EVO, 0x6BF1, 122, 130, 69, 30, 80, 69, 4, T_STEEL, T_NONE },  // 879 steel
  { "DRACOZOLT", 0, 0, R_RARO, 0xBCA1, 90, 100, 90, 75, 80, 70, 0, T_ELECTRIC, T_DRAGON },  // 880 electric/dragon
  { "ARCTOZOLT", 0, 0, R_RARO, 0xBCA1, 90, 100, 90, 55, 90, 80, 0, T_ELECTRIC, T_ICE },  // 881 electric/ice
  { "DRACOVISH", 0, 0, R_RARO, 0x4C98, 90, 90, 100, 75, 70, 80, 1, T_WATER, T_DRAGON },  // 882 water/dragon
  { "ARCTOVISH", 0, 0, R_RARO, 0x4C98, 90, 90, 100, 55, 80, 90, 1, T_WATER, T_ICE },  // 883 water/ice
  { "DURALUDON", 1018, 30, R_RARO, 0x6BF1, 70, 95, 115, 85, 120, 50, 4, T_STEEL, T_DRAGON },  // 884 steel/dragon
  { "DREEPY", 886, 50, R_RARO, 0x5A98, 28, 60, 30, 82, 40, 30, 1, T_DRAGON, T_GHOST },  // 885 dragon/ghost
  { "DRAKLOAK", 887, 60, R_EVO, 0x5A98, 68, 80, 50, 102, 60, 50, 1, T_DRAGON, T_GHOST },  // 886 dragon/ghost
  { "DRAGAPULT", 0, 0, R_EVO, 0x5A98, 88, 120, 75, 142, 100, 75, 1, T_DRAGON, T_GHOST },  // 887 dragon/ghost
  { "ZACIAN", 0, 0, R_LEGENDARIO, 0xC333, 92, 120, 115, 138, 80, 115, 0, T_FAIRY, T_NONE },  // 888 fairy
  { "ZAMAZENTA", 0, 0, R_LEGENDARIO, 0xA2A5, 92, 120, 115, 138, 80, 115, 0, T_FIGHTING, T_NONE },  // 889 fighting
  { "ETERNATUS", 0, 0, R_LEGENDARIO, 0x8A73, 140, 85, 95, 130, 145, 95, 0, T_POISON, T_DRAGON },  // 890 poison/dragon
  { "KUBFU", 892, 30, R_LEGENDARIO, 0xA2A5, 60, 90, 60, 72, 53, 50, 0, T_FIGHTING, T_NONE },  // 891 fighting
  { "URSHIFU", 0, 0, R_EVO, 0xA2A5, 100, 130, 100, 97, 63, 60, 0, T_FIGHTING, T_DARK },  // 892 fighting/dark
  { "ZARUDE", 0, 0, R_LEGENDARIO, 0x5A47, 105, 120, 105, 105, 70, 95, 2, T_DARK, T_GRASS },  // 893 dark/grass
  { "REGIELEKI", 0, 0, R_LEGENDARIO, 0xBCA1, 80, 100, 50, 200, 100, 50, 0, T_ELECTRIC, T_NONE },  // 894 electric
  { "REGIDRAGO", 0, 0, R_LEGENDARIO, 0x5A98, 200, 100, 50, 80, 100, 50, 1, T_DRAGON, T_NONE },  // 895 dragon
  { "GLASTRIER", 0, 0, R_LEGENDARIO, 0x4DB8, 100, 145, 130, 30, 65, 110, 5, T_ICE, T_NONE },  // 896 ice
  { "SPECTRIER", 0, 0, R_LEGENDARIO, 0x6AD3, 100, 65, 60, 130, 145, 80, 0, T_GHOST, T_NONE },  // 897 ghost
  { "CALYREX", 0, 0, R_LEGENDARIO, 0xD28F, 100, 80, 80, 80, 80, 80, 0, T_PSYCHIC, T_GRASS },  // 898 psychic/grass
  { "WYRDEER", 0, 0, R_EVO, 0x8C4D, 103, 105, 72, 65, 105, 75, 0, T_NORMAL, T_PSYCHIC },  // 899 normal/psychic
  { "KLEAVOR", 0, 0, R_COMUN, 0x7CC4, 70, 135, 95, 85, 45, 70, 2, T_BUG, T_ROCK },  // 900 bug/rock
  { "URSALUNA", 0, 0, R_EVO, 0xB447, 130, 140, 105, 50, 45, 80, 4, T_GROUND, T_NORMAL },  // 901 ground/normal
  { "BASCULEGION", 0, 0, R_EVO, 0x4C98, 120, 112, 65, 78, 80, 75, 1, T_WATER, T_GHOST },  // 902 water/ghost
  { "SNEASLER", 0, 0, R_COMUN, 0xA2A5, 80, 130, 60, 120, 40, 80, 0, T_FIGHTING, T_POISON },  // 903 fighting/poison
  { "OVERQWIL", 0, 0, R_EVO, 0x5A47, 85, 115, 95, 85, 65, 65, 2, T_DARK, T_POISON },  // 904 dark/poison
  { "ENAMORUS", 0, 0, R_LEGENDARIO, 0xC333, 74, 115, 70, 106, 135, 80, 0, T_FAIRY, T_FLYING },  // 905 fairy/flying
  { "SPRIGATITO", 907, 16, R_RARO, 0x3C49, 40, 61, 54, 65, 45, 45, 2, T_GRASS, T_NONE },  // 906 grass
  { "FLORAGATO", 908, 36, R_EVO, 0x3C49, 61, 80, 63, 83, 60, 63, 2, T_GRASS, T_NONE },  // 907 grass
  { "MEOWSCARADA", 0, 0, R_EVO, 0x3C49, 76, 110, 70, 123, 81, 70, 2, T_GRASS, T_DARK },  // 908 grass/dark
  { "FUECOCO", 910, 16, R_RARO, 0xEA87, 67, 45, 59, 36, 63, 40, 3, T_FIRE, T_NONE },  // 909 fire
  { "CROCALOR", 911, 36, R_EVO, 0xEA87, 81, 55, 78, 49, 90, 58, 3, T_FIRE, T_NONE },  // 910 fire
  { "SKELEDIRGE", 0, 0, R_EVO, 0xEA87, 104, 75, 100, 66, 110, 75, 3, T_FIRE, T_GHOST },  // 911 fire/ghost
  { "QUAXLY", 913, 16, R_RARO, 0x4C98, 55, 65, 45, 50, 50, 45, 1, T_WATER, T_NONE },  // 912 water
  { "QUAXWELL", 914, 36, R_EVO, 0x4C98, 70, 85, 65, 65, 65, 60, 1, T_WATER, T_NONE },  // 913 water
  { "QUAQUAVAL", 0, 0, R_EVO, 0x4C98, 85, 120, 80, 85, 85, 75, 1, T_WATER, T_FIGHTING },  // 914 water/fighting
  { "LECHONK", 916, 18, R_COMUN, 0x8C4D, 54, 45, 40, 35, 35, 45, 0, T_NORMAL, T_NONE },  // 915 normal
  { "OINKOLOGNE", 0, 0, R_EVO, 0x8C4D, 110, 100, 75, 65, 59, 80, 0, T_NORMAL, T_NONE },  // 916 normal
  { "TAROUNTULA", 918, 15, R_COMUN, 0x7CC4, 35, 41, 45, 20, 29, 40, 2, T_BUG, T_NONE },  // 917 bug
  { "SPIDOPS", 0, 0, R_EVO, 0x7CC4, 60, 79, 92, 35, 52, 86, 2, T_BUG, T_NONE },  // 918 bug
  { "NYMBLE", 920, 24, R_COMUN, 0x7CC4, 33, 46, 40, 45, 21, 25, 2, T_BUG, T_NONE },  // 919 bug
  { "LOKIX", 0, 0, R_EVO, 0x7CC4, 71, 102, 78, 92, 52, 55, 2, T_BUG, T_DARK },  // 920 bug/dark
  { "PAWMI", 922, 18, R_COMUN, 0xBCA1, 45, 50, 20, 60, 40, 25, 0, T_ELECTRIC, T_NONE },  // 921 electric
  { "PAWMO", 923, 30, R_EVO, 0xBCA1, 60, 75, 40, 85, 50, 40, 0, T_ELECTRIC, T_FIGHTING },  // 922 electric/fighting
  { "PAWMOT", 0, 0, R_EVO, 0xBCA1, 70, 115, 70, 105, 70, 60, 0, T_ELECTRIC, T_FIGHTING },  // 923 electric/fighting
  { "TANDEMAUS", 925, 25, R_COMUN, 0x8C4D, 50, 50, 45, 75, 40, 45, 0, T_NORMAL, T_NONE },  // 924 normal
  { "MAUSHOLD", 0, 0, R_EVO, 0x8C4D, 74, 75, 70, 111, 65, 75, 0, T_NORMAL, T_NONE },  // 925 normal
  { "FIDOUGH", 927, 26, R_COMUN, 0xC333, 37, 55, 70, 65, 30, 55, 0, T_FAIRY, T_NONE },  // 926 fairy
  { "DACHSBUN", 0, 0, R_EVO, 0xC333, 57, 80, 115, 95, 50, 80, 0, T_FAIRY, T_NONE },  // 927 fairy
  { "SMOLIV", 929, 25, R_COMUN, 0x3C49, 41, 35, 45, 30, 58, 51, 2, T_GRASS, T_NORMAL },  // 928 grass/normal
  { "DOLLIV", 930, 35, R_EVO, 0x3C49, 52, 53, 60, 33, 78, 78, 2, T_GRASS, T_NORMAL },  // 929 grass/normal
  { "ARBOLIVA", 0, 0, R_EVO, 0x3C49, 78, 69, 90, 39, 125, 109, 2, T_GRASS, T_NORMAL },  // 930 grass/normal
  { "SQUAWKABILLY", 0, 0, R_COMUN, 0x8C4D, 82, 96, 51, 92, 45, 51, 0, T_NORMAL, T_FLYING },  // 931 normal/flying
  { "NACLI", 933, 24, R_COMUN, 0x9407, 55, 55, 75, 25, 35, 35, 4, T_ROCK, T_NONE },  // 932 rock
  { "NACLSTACK", 934, 38, R_EVO, 0x9407, 60, 60, 100, 35, 35, 65, 4, T_ROCK, T_NONE },  // 933 rock
  { "GARGANACL", 0, 0, R_EVO, 0x9407, 100, 100, 130, 35, 45, 90, 4, T_ROCK, T_NONE },  // 934 rock
  { "CHARCADET", 936, 30, R_COMUN, 0xEA87, 40, 50, 40, 35, 50, 40, 3, T_FIRE, T_NONE },  // 935 fire
  { "ARMAROUGE", 0, 0, R_EVO, 0xEA87, 85, 60, 100, 75, 125, 80, 3, T_FIRE, T_PSYCHIC },  // 936 fire/psychic
  { "CERULEDGE", 0, 0, R_RARO, 0xEA87, 75, 125, 80, 85, 60, 100, 3, T_FIRE, T_GHOST },  // 937 fire/ghost
  { "TADBULB", 939, 30, R_COMUN, 0xBCA1, 61, 31, 41, 45, 59, 35, 0, T_ELECTRIC, T_NONE },  // 938 electric
  { "BELLIBOLT", 0, 0, R_EVO, 0xBCA1, 109, 64, 91, 45, 103, 83, 0, T_ELECTRIC, T_NONE },  // 939 electric
  { "WATTREL", 941, 25, R_COMUN, 0xBCA1, 40, 40, 35, 70, 55, 40, 0, T_ELECTRIC, T_FLYING },  // 940 electric/flying
  { "KILOWATTREL", 0, 0, R_EVO, 0xBCA1, 70, 70, 60, 125, 105, 60, 0, T_ELECTRIC, T_FLYING },  // 941 electric/flying
  { "MASCHIFF", 943, 30, R_COMUN, 0x5A47, 60, 78, 60, 51, 40, 51, 2, T_DARK, T_NONE },  // 942 dark
  { "MABOSSTIFF", 0, 0, R_EVO, 0x5A47, 80, 120, 90, 85, 60, 70, 2, T_DARK, T_NONE },  // 943 dark
  { "SHROODLE", 945, 28, R_COMUN, 0x8A73, 40, 65, 35, 75, 40, 35, 0, T_POISON, T_NORMAL },  // 944 poison/normal
  { "GRAFAIAI", 0, 0, R_EVO, 0x8A73, 63, 95, 65, 110, 80, 72, 0, T_POISON, T_NORMAL },  // 945 poison/normal
  { "BRAMBLIN", 947, 30, R_COMUN, 0x3C49, 40, 65, 30, 60, 45, 35, 2, T_GRASS, T_GHOST },  // 946 grass/ghost
  { "BRAMBLEGHAST", 0, 0, R_EVO, 0x3C49, 55, 115, 70, 90, 80, 70, 2, T_GRASS, T_GHOST },  // 947 grass/ghost
  { "TOEDSCOOL", 949, 30, R_COMUN, 0xB447, 40, 40, 35, 70, 50, 100, 4, T_GROUND, T_GRASS },  // 948 ground/grass
  { "TOEDSCRUEL", 0, 0, R_EVO, 0xB447, 80, 70, 65, 100, 80, 120, 4, T_GROUND, T_GRASS },  // 949 ground/grass
  { "KLAWF", 0, 0, R_COMUN, 0x9407, 70, 100, 115, 75, 35, 55, 4, T_ROCK, T_NONE },  // 950 rock
  { "CAPSAKID", 952, 30, R_COMUN, 0x3C49, 50, 62, 40, 50, 62, 40, 2, T_GRASS, T_NONE },  // 951 grass
  { "SCOVILLAIN", 0, 0, R_EVO, 0x3C49, 65, 108, 65, 75, 108, 65, 2, T_GRASS, T_FIRE },  // 952 grass/fire
  { "RELLOR", 954, 30, R_COMUN, 0x7CC4, 41, 50, 60, 30, 31, 58, 2, T_BUG, T_NONE },  // 953 bug
  { "RABSCA", 0, 0, R_EVO, 0x7CC4, 75, 50, 85, 45, 115, 100, 2, T_BUG, T_PSYCHIC },  // 954 bug/psychic
  { "FLITTLE", 956, 35, R_COMUN, 0xD28F, 30, 35, 30, 75, 55, 30, 0, T_PSYCHIC, T_NONE },  // 955 psychic
  { "ESPATHRA", 0, 0, R_EVO, 0xD28F, 95, 60, 60, 105, 101, 60, 0, T_PSYCHIC, T_NONE },  // 956 psychic
  { "TINKATINK", 958, 24, R_COMUN, 0xC333, 50, 45, 45, 58, 35, 64, 0, T_FAIRY, T_STEEL },  // 957 fairy/steel
  { "TINKATUFF", 959, 38, R_EVO, 0xC333, 65, 55, 55, 78, 45, 82, 0, T_FAIRY, T_STEEL },  // 958 fairy/steel
  { "TINKATON", 0, 0, R_EVO, 0xC333, 85, 75, 77, 94, 70, 105, 0, T_FAIRY, T_STEEL },  // 959 fairy/steel
  { "WIGLETT", 961, 26, R_COMUN, 0x4C98, 10, 55, 25, 95, 35, 25, 1, T_WATER, T_NONE },  // 960 water
  { "WUGTRIO", 0, 0, R_EVO, 0x4C98, 35, 100, 50, 120, 50, 70, 1, T_WATER, T_NONE },  // 961 water
  { "BOMBIRDIER", 0, 0, R_RARO, 0x7C7A, 70, 103, 85, 82, 60, 85, 0, T_FLYING, T_DARK },  // 962 flying/dark
  { "FINIZEN", 964, 38, R_COMUN, 0x4C98, 70, 45, 40, 75, 45, 40, 1, T_WATER, T_NONE },  // 963 water
  { "PALAFIN", 0, 0, R_EVO, 0x4C98, 100, 70, 72, 100, 53, 62, 1, T_WATER, T_NONE },  // 964 water
  { "VAROOM", 966, 40, R_COMUN, 0x6BF1, 45, 70, 63, 47, 30, 45, 4, T_STEEL, T_POISON },  // 965 steel/poison
  { "REVAVROOM", 0, 0, R_EVO, 0x6BF1, 80, 119, 90, 90, 54, 67, 4, T_STEEL, T_POISON },  // 966 steel/poison
  { "CYCLIZAR", 0, 0, R_COMUN, 0x5A98, 70, 95, 65, 121, 85, 65, 1, T_DRAGON, T_NORMAL },  // 967 dragon/normal
  { "ORTHWORM", 0, 0, R_RARO, 0x6BF1, 70, 85, 145, 65, 60, 55, 4, T_STEEL, T_NONE },  // 968 steel
  { "GLIMMET", 970, 35, R_COMUN, 0x9407, 48, 35, 42, 60, 105, 60, 4, T_ROCK, T_POISON },  // 969 rock/poison
  { "GLIMMORA", 0, 0, R_EVO, 0x9407, 83, 55, 90, 86, 130, 81, 4, T_ROCK, T_POISON },  // 970 rock/poison
  { "GREAVARD", 972, 30, R_COMUN, 0x6AD3, 50, 61, 60, 34, 30, 55, 0, T_GHOST, T_NONE },  // 971 ghost
  { "HOUNDSTONE", 0, 0, R_EVO, 0x6AD3, 72, 101, 100, 68, 50, 97, 0, T_GHOST, T_NONE },  // 972 ghost
  { "FLAMIGO", 0, 0, R_COMUN, 0x7C7A, 82, 115, 74, 90, 75, 64, 0, T_FLYING, T_FIGHTING },  // 973 flying/fighting
  { "CETODDLE", 975, 30, R_COMUN, 0x4DB8, 108, 68, 45, 43, 30, 40, 5, T_ICE, T_NONE },  // 974 ice
  { "CETITAN", 0, 0, R_EVO, 0x4DB8, 170, 113, 65, 73, 45, 55, 5, T_ICE, T_NONE },  // 975 ice
  { "VELUZA", 0, 0, R_COMUN, 0x4C98, 90, 102, 73, 70, 78, 65, 1, T_WATER, T_PSYCHIC },  // 976 water/psychic
  { "DONDOZO", 0, 0, R_RARO, 0x4C98, 150, 100, 115, 35, 65, 65, 1, T_WATER, T_NONE },  // 977 water
  { "TATSUGIRI", 0, 0, R_COMUN, 0x5A98, 68, 50, 60, 82, 120, 95, 1, T_DRAGON, T_WATER },  // 978 dragon/water
  { "ANNIHILAPE", 0, 0, R_EVO, 0xA2A5, 110, 115, 80, 90, 50, 90, 0, T_FIGHTING, T_GHOST },  // 979 fighting/ghost
  { "CLODSIRE", 0, 0, R_COMUN, 0x8A73, 130, 75, 60, 20, 45, 100, 0, T_POISON, T_GROUND },  // 980 poison/ground
  { "FARIGIRAF", 0, 0, R_EVO, 0x8C4D, 120, 90, 70, 60, 110, 70, 0, T_NORMAL, T_PSYCHIC },  // 981 normal/psychic
  { "DUDUNSPARCE", 0, 0, R_EVO, 0x8C4D, 125, 100, 80, 55, 85, 75, 0, T_NORMAL, T_NONE },  // 982 normal
  { "KINGAMBIT", 0, 0, R_EVO, 0x5A47, 100, 135, 120, 50, 60, 85, 2, T_DARK, T_STEEL },  // 983 dark/steel
  { "GREAT TUSK", 0, 0, R_RARO, 0xB447, 115, 131, 131, 87, 53, 53, 4, T_GROUND, T_FIGHTING },  // 984 ground/fighting
  { "SCREAM TAIL", 0, 0, R_COMUN, 0xC333, 115, 65, 99, 111, 65, 115, 0, T_FAIRY, T_PSYCHIC },  // 985 fairy/psychic
  { "BRUTE BONNET", 0, 0, R_COMUN, 0x3C49, 111, 127, 99, 55, 79, 99, 2, T_GRASS, T_DARK },  // 986 grass/dark
  { "FLUTTER MANE", 0, 0, R_RARO, 0x6AD3, 55, 55, 55, 135, 135, 135, 0, T_GHOST, T_FAIRY },  // 987 ghost/fairy
  { "SLITHER WING", 0, 0, R_RARO, 0x7CC4, 85, 135, 79, 81, 85, 105, 2, T_BUG, T_FIGHTING },  // 988 bug/fighting
  { "SANDY SHOCKS", 0, 0, R_RARO, 0xBCA1, 85, 81, 97, 101, 121, 85, 0, T_ELECTRIC, T_GROUND },  // 989 electric/ground
  { "IRON TREADS", 0, 0, R_RARO, 0xB447, 90, 112, 120, 106, 72, 70, 4, T_GROUND, T_STEEL },  // 990 ground/steel
  { "IRON BUNDLE", 0, 0, R_COMUN, 0x4DB8, 56, 80, 114, 136, 124, 60, 5, T_ICE, T_WATER },  // 991 ice/water
  { "IRON HANDS", 0, 0, R_COMUN, 0xA2A5, 154, 140, 108, 50, 50, 68, 0, T_FIGHTING, T_ELECTRIC },  // 992 fighting/electric
  { "IRON JUGULIS", 0, 0, R_RARO, 0x5A47, 94, 80, 86, 108, 122, 80, 2, T_DARK, T_FLYING },  // 993 dark/flying
  { "IRON MOTH", 0, 0, R_RARO, 0xEA87, 80, 70, 60, 110, 140, 110, 3, T_FIRE, T_POISON },  // 994 fire/poison
  { "IRON THORNS", 0, 0, R_RARO, 0x9407, 100, 134, 110, 72, 70, 84, 4, T_ROCK, T_ELECTRIC },  // 995 rock/electric
  { "FRIGIBAX", 997, 35, R_RARO, 0x5A98, 65, 75, 45, 55, 35, 45, 1, T_DRAGON, T_ICE },  // 996 dragon/ice
  { "ARCTIBAX", 998, 54, R_EVO, 0x5A98, 90, 95, 66, 62, 45, 65, 1, T_DRAGON, T_ICE },  // 997 dragon/ice
  { "BAXCALIBUR", 0, 0, R_EVO, 0x5A98, 115, 145, 92, 87, 75, 86, 1, T_DRAGON, T_ICE },  // 998 dragon/ice
  { "GIMMIGHOUL", 1000, 30, R_RARO, 0x6AD3, 45, 30, 70, 10, 75, 70, 0, T_GHOST, T_NONE },  // 999 ghost
  { "GHOLDENGO", 0, 0, R_EVO, 0x6BF1, 87, 60, 95, 84, 133, 91, 4, T_STEEL, T_GHOST },  // 1000 steel/ghost
  { "WO CHIEN", 0, 0, R_LEGENDARIO, 0x5A47, 85, 85, 100, 70, 95, 135, 2, T_DARK, T_GRASS },  // 1001 dark/grass
  { "CHIEN PAO", 0, 0, R_LEGENDARIO, 0x5A47, 80, 120, 80, 135, 90, 65, 2, T_DARK, T_ICE },  // 1002 dark/ice
  { "TING LU", 0, 0, R_LEGENDARIO, 0x5A47, 155, 110, 125, 45, 55, 80, 2, T_DARK, T_GROUND },  // 1003 dark/ground
  { "CHI YU", 0, 0, R_LEGENDARIO, 0x5A47, 55, 80, 80, 100, 135, 120, 2, T_DARK, T_FIRE },  // 1004 dark/fire
  { "ROARING MOON", 0, 0, R_RARO, 0x5A98, 105, 139, 71, 119, 55, 101, 1, T_DRAGON, T_DARK },  // 1005 dragon/dark
  { "IRON VALIANT", 0, 0, R_RARO, 0xC333, 74, 130, 90, 116, 120, 60, 0, T_FAIRY, T_FIGHTING },  // 1006 fairy/fighting
  { "KORAIDON", 0, 0, R_LEGENDARIO, 0xA2A5, 100, 135, 115, 135, 85, 100, 0, T_FIGHTING, T_DRAGON },  // 1007 fighting/dragon
  { "MIRAIDON", 0, 0, R_LEGENDARIO, 0xBCA1, 100, 85, 100, 135, 135, 115, 0, T_ELECTRIC, T_DRAGON },  // 1008 electric/dragon
  { "WALKING WAKE", 0, 0, R_RARO, 0x4C98, 99, 83, 91, 109, 125, 83, 1, T_WATER, T_DRAGON },  // 1009 water/dragon
  { "IRON LEAVES", 0, 0, R_RARO, 0x3C49, 90, 130, 88, 104, 70, 108, 2, T_GRASS, T_PSYCHIC },  // 1010 grass/psychic
  { "DIPPLIN", 1019, 30, R_RARO, 0x3C49, 80, 80, 110, 40, 95, 80, 2, T_GRASS, T_DRAGON },  // 1011 grass/dragon
  { "POLTCHAGEIST", 1013, 30, R_COMUN, 0x3C49, 40, 45, 45, 50, 74, 54, 2, T_GRASS, T_GHOST },  // 1012 grass/ghost
  { "SINISTCHA", 0, 0, R_EVO, 0x3C49, 71, 60, 106, 70, 121, 80, 2, T_GRASS, T_GHOST },  // 1013 grass/ghost
  { "OKIDOGI", 0, 0, R_LEGENDARIO, 0x8A73, 88, 128, 115, 80, 58, 86, 0, T_POISON, T_FIGHTING },  // 1014 poison/fighting
  { "MUNKIDORI", 0, 0, R_LEGENDARIO, 0x8A73, 88, 75, 66, 106, 130, 90, 0, T_POISON, T_PSYCHIC },  // 1015 poison/psychic
  { "FEZANDIPITI", 0, 0, R_LEGENDARIO, 0x8A73, 88, 91, 82, 99, 70, 125, 0, T_POISON, T_FAIRY },  // 1016 poison/fairy
  { "OGERPON", 0, 0, R_LEGENDARIO, 0x3C49, 80, 120, 84, 110, 60, 96, 2, T_GRASS, T_NONE },  // 1017 grass
  { "ARCHALUDON", 0, 0, R_EVO, 0x6BF1, 90, 105, 130, 85, 125, 65, 4, T_STEEL, T_DRAGON },  // 1018 steel/dragon
  { "HYDRAPPLE", 0, 0, R_EVO, 0x3C49, 106, 80, 110, 44, 120, 80, 2, T_GRASS, T_DRAGON },  // 1019 grass/dragon
  { "GOUGING FIRE", 0, 0, R_RARO, 0xEA87, 105, 115, 121, 91, 65, 93, 3, T_FIRE, T_DRAGON },  // 1020 fire/dragon
  { "RAGING BOLT", 0, 0, R_RARO, 0xBCA1, 125, 73, 91, 75, 137, 89, 0, T_ELECTRIC, T_DRAGON },  // 1021 electric/dragon
  { "IRON BOULDER", 0, 0, R_RARO, 0x9407, 90, 120, 80, 124, 68, 108, 4, T_ROCK, T_PSYCHIC },  // 1022 rock/psychic
  { "IRON CROWN", 0, 0, R_RARO, 0x6BF1, 90, 72, 100, 98, 122, 108, 4, T_STEEL, T_PSYCHIC },  // 1023 steel/psychic
  { "TERAPAGOS", 0, 0, R_LEGENDARIO, 0x8C4D, 90, 65, 85, 60, 65, 85, 0, T_NORMAL, T_NONE },  // 1024 normal
  { "PECHARUNT", 0, 0, R_LEGENDARIO, 0x8A73, 88, 88, 160, 88, 88, 88, 0, T_POISON, T_GHOST },  // 1025 poison/ghost
};

// el primer huevo de la partida: iniciales clasicos
static const int16_t CLASSIC_DEX[] = { 1, 4, 7, 25, 133 };
#define NUM_CLASSIC_DEX 5

// Which generation an egg may come from. A region is decided by the BASE
// species, and evolutions follow wherever they lead.
struct RegionInfo {
  const char *name;
  uint16_t lo, hi;
  const int16_t *starters;
  uint8_t starterCount;
};
static const int16_t REGION_START_KANTO[] = { 1, 4, 7, 25, 133 };
static const int16_t REGION_START_JOHTO[] = { 152, 155, 158 };
static const int16_t REGION_START_HOENN[] = { 252, 255, 258 };
static const int16_t REGION_START_SINNOH[] = { 387, 390, 393 };
static const int16_t REGION_START_UNOVA[] = { 495, 498, 501 };
static const int16_t REGION_START_KALOS[] = { 650, 653, 656 };
static const int16_t REGION_START_ALOLA[] = { 722, 725, 728 };
static const int16_t REGION_START_GALAR[] = { 810, 813, 816 };
static const int16_t REGION_START_PALDEA[] = { 906, 909, 912 };
static const int16_t REGION_START_ALL[] = { 1, 4, 7, 25, 133, 152, 155, 158, 252, 255, 258, 387, 390, 393, 495, 498, 501, 650, 653, 656, 722, 725, 728, 810, 813, 816, 906, 909, 912 };
#define REGION_COUNT 10
#define REGION_ALL 9
static const RegionInfo REGIONS[REGION_COUNT] = {
  { "KANTO", 1, 151, REGION_START_KANTO, 5 },
  { "JOHTO", 152, 251, REGION_START_JOHTO, 3 },
  { "HOENN", 252, 386, REGION_START_HOENN, 3 },
  { "SINNOH", 387, 493, REGION_START_SINNOH, 3 },
  { "UNOVA", 494, 649, REGION_START_UNOVA, 3 },
  { "KALOS", 650, 721, REGION_START_KALOS, 3 },
  { "ALOLA", 722, 809, REGION_START_ALOLA, 3 },
  { "GALAR", 810, 905, REGION_START_GALAR, 3 },
  { "PALDEA", 906, 1025, REGION_START_PALDEA, 3 },
  { "ALL", 1, 1025, REGION_START_ALL, 29 },
};
