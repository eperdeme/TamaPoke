#pragma once
#include <stdint.h>
#include "dex.h"
#include "items.h"

// Wild encounters: the one way to meet a creature outside a gym.
//
// Everything here is a PURE FUNCTION of its arguments -- every roll is passed
// in rather than drawn from random(). That is not style: it is what lets
// wild_test pin the capture curve and both escape curves at their boundaries
// instead of asserting that some average came out about right. See CLAUDE.md
// § "A test that proves the transcription rather than the firmware".

// The shiny roll uses one common denominator so 100 / 409600 is exactly 1/4096
// and each bonus point is exactly one percentage point.
constexpr uint32_t WILD_SHINY_SCALE = 409600;
constexpr uint32_t WILD_SHINY_BASE = 100;
constexpr uint32_t WILD_SHINY_PER_POINT = 4096;
constexpr uint8_t WILD_SHINY_BONUS_MAX = 15;
// A shiny is not merely a repaint: it floors every IV. It does NOT cap them,
// so an already-better roll is kept.
constexpr uint8_t WILD_SHINY_IV_FLOOR = 20;

constexpr uint8_t WILD_BONUS_DROP_CHANCE = 30;   // percent, one extra item
constexpr uint8_t WILD_LEVEL_SPREAD = 5;         // either side of the player

uint32_t wildShinyThreshold(uint8_t bonus);
bool wildShinyForRoll(uint32_t roll, uint8_t bonus);
void wildApplyShiny(bool shiny, uint8_t &ivAtk, uint8_t &ivDef,
                    uint8_t &ivSpe, uint8_t &ivHp);

// Capture chance as a percentage, 0..95 -- never a certainty, so a ball is
// always a gamble, except for the guaranteed sentinel which returns 100.
// `ballPct` is ItemEntry::param for an IC_BALL item.
uint8_t wildCaptureChance(uint8_t rarity, uint16_t hp, uint16_t maxHp,
                          bool hasStatus, int16_t ballPct);

// The band a wild creature's level is drawn from. Hard opens the whole ladder;
// normal stays within WILD_LEVEL_SPREAD either side of the player so an
// encounter is always winnable.
uint8_t wildLevelMin(uint8_t playerLevel, bool hard);
uint8_t wildLevelMax(uint8_t playerLevel, bool hard);

// Normal grants one weighted item, hard two. Both then make one independent
// roll for a single bonus item.
uint8_t wildDropCount(bool hard, uint8_t bonusRoll);

// Your chance of getting away. 90% against something at or below your level,
// scaled down by the level ratio above it, with a 10% floor -- so fleeing a
// bad matchup is reliable but fleeing a much stronger one is not.
uint8_t wildEscapeChance(uint8_t playerLevel, uint8_t foeLevel);

// The foe's. It only starts considering escape at 40% HP (10%), rises linearly
// to 20% at 20% HP, then falls back to 10% at 10% and stays there -- a nearly
// beaten creature stops running and fights, which is what makes weakening it
// the right way to catch it rather than a race.
uint8_t wildFoeEscapeChance(uint16_t hp, uint16_t maxHp);

// Which rarity tier this encounter is, from a 0..99 roll. Legendaries are
// deliberately about 1 in 100 rather than gated behind a Pokedex count: a wild
// legendary you cannot beat is content, not a reward.
uint8_t wildTierForRoll(uint8_t roll);

// A species of that tier that is in an available region AND has art. Both
// filters matter: a caught creature is kept forever, so one that can only ever
// draw as a dex number would be a permanent defect -- the same fault CLAUDE.md
// records being removed from the gym rosters. `region` is a REGIONS index, or
// REGION_ALL for the mixed pool. 0 if the tier has nothing available.
int16_t wildPickSpecies(uint8_t region, uint8_t tier, uint32_t roll);
