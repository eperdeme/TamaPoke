#include "wild.h"
#include "pet.h"     // regionAvailable(), regionOfDex()
#include "noart.h"   // speciesHasArt()

static uint8_t shinyBonus(uint8_t bonus) {
  return bonus > WILD_SHINY_BONUS_MAX ? WILD_SHINY_BONUS_MAX : bonus;
}

uint32_t wildShinyThreshold(uint8_t bonus) {
  return WILD_SHINY_BASE + (uint32_t)shinyBonus(bonus) * WILD_SHINY_PER_POINT;
}

bool wildShinyForRoll(uint32_t roll, uint8_t bonus) {
  return (roll % WILD_SHINY_SCALE) < wildShinyThreshold(bonus);
}

void wildApplyShiny(bool shiny, uint8_t &ivAtk, uint8_t &ivDef,
                    uint8_t &ivSpe, uint8_t &ivHp) {
  if (!shiny) return;
  uint8_t *ivs[] = { &ivAtk, &ivDef, &ivSpe, &ivHp };
  for (uint8_t *iv : ivs)
    if (*iv < WILD_SHINY_IV_FLOOR) *iv = WILD_SHINY_IV_FLOOR;
}

uint8_t wildCaptureChance(uint8_t rarity, uint16_t hp, uint16_t maxHp,
                          bool hasStatus, int16_t ballPct) {
  if (!maxHp || !hp) return 0;
  if (ballPct == ITEM_CATCH_GUARANTEED) return 100;
  if (ballPct <= 0) return 0;
  if (hp > maxHp) hp = maxHp;
  uint8_t base;
  switch (rarity) {
    case R_LEGENDARIO: base = 5; break;
    case R_RARO:       base = 18; break;
    case R_EVO:        base = 24; break;
    default:           base = 30; break;
  }
  // A full-health target is 100; an almost-fainted one is 180. Weakening it
  // matters, but never as much as the ball you chose.
  uint16_t hpFactor = (uint16_t)(100U + (uint32_t)(maxHp - hp) * 80U / maxHp);
  uint16_t statusFactor = hasStatus ? 130 : 100;
  uint32_t chance = (uint32_t)base * hpFactor * statusFactor *
                    (uint32_t)(uint16_t)ballPct / 1000000U;
  if (!chance) chance = 1;
  return chance > 95 ? 95 : (uint8_t)chance;
}

uint8_t wildLevelMin(uint8_t playerLevel, bool hard) {
  if (hard) return 1;
  return playerLevel > WILD_LEVEL_SPREAD
             ? (uint8_t)(playerLevel - WILD_LEVEL_SPREAD) : 1;
}

uint8_t wildLevelMax(uint8_t playerLevel, bool hard) {
  if (hard) return MAX_LEVEL;
  uint16_t top = (uint16_t)playerLevel + WILD_LEVEL_SPREAD;
  return top > MAX_LEVEL ? MAX_LEVEL : (uint8_t)top;
}

uint8_t wildDropCount(bool hard, uint8_t bonusRoll) {
  return (uint8_t)((hard ? 2 : 1) +
                   (bonusRoll < WILD_BONUS_DROP_CHANCE ? 1 : 0));
}

uint8_t wildEscapeChance(uint8_t playerLevel, uint8_t foeLevel) {
  if (!foeLevel || playerLevel >= foeLevel) return 90;
  uint8_t chance = (uint8_t)((uint16_t)90 * playerLevel / foeLevel);
  return chance < 10 ? 10 : chance;
}

uint8_t wildFoeEscapeChance(uint16_t hp, uint16_t maxHp) {
  if (!maxHp || !hp) return 0;
  if (hp > maxHp) hp = maxHp;
  uint32_t scaled = (uint32_t)hp * 100U;
  if (scaled <= (uint32_t)maxHp * 10U) return 10;
  if (scaled <= (uint32_t)maxHp * 20U) {
    uint32_t above = scaled - (uint32_t)maxHp * 10U;
    return (uint8_t)(10U + above * 10U / ((uint32_t)maxHp * 10U));
  }
  if (scaled <= (uint32_t)maxHp * 40U) {
    uint32_t below = (uint32_t)maxHp * 40U - scaled;
    return (uint8_t)(10U + below * 10U / ((uint32_t)maxHp * 20U));
  }
  return 0;
}

uint8_t wildTierForRoll(uint8_t roll) {
  if (roll < 1) return R_LEGENDARIO;   // 1%
  if (roll < 8) return R_RARO;         // 7%
  if (roll < 30) return R_EVO;         // 22%
  return R_COMUN;                      // the rest
}

int16_t wildPickSpecies(uint8_t region, uint8_t tier, uint32_t roll) {
  uint16_t lo = 1, hi = DEX_COUNT;
  if (region < REGION_COUNT && region != REGION_ALL) {
    lo = REGIONS[region].lo;
    hi = REGIONS[region].hi;
  }
  if (hi > DEX_COUNT) hi = DEX_COUNT;
  // Two passes rather than a candidate array: DEX_COUNT is 809 and an int16_t
  // buffer of that size is 1.6 KB of stack on a device whose framebuffer
  // already lives in PSRAM.
  uint16_t n = 0;
  for (uint16_t d = lo; d <= hi; d++) {
    if (DEX_TBL[d].rarity != tier) continue;
    if (!speciesHasArt((int16_t)d)) continue;
    if (!regionAvailable(regionOfDex((int16_t)d))) continue;
    n++;
  }
  if (!n) return 0;
  uint16_t want = (uint16_t)(roll % n);
  for (uint16_t d = lo; d <= hi; d++) {
    if (DEX_TBL[d].rarity != tier) continue;
    if (!speciesHasArt((int16_t)d)) continue;
    if (!regionAvailable(regionOfDex((int16_t)d))) continue;
    if (!want) return (int16_t)d;
    want--;
  }
  return 0;
}
