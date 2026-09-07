#include "rtcbat.h"
#include "pin_config.h"  // define XPOWERS_CHIP_AXP2101
#include <Wire.h>
#include <time.h>
#include <SensorPCF85063.hpp>
#include <XPowersLib.h>

static SensorPCF85063 rtc;
static XPowersPMU pmu;
static bool rtcOk = false;
static bool pmuOk = false;

bool rtcBegin() {
  rtcOk = rtc.begin(Wire, IIC_SDA, IIC_SCL);
  if (!rtcOk) Serial.println("PCF85063 no detectado");
  return rtcOk;
}

uint32_t rtcEpoch() {
  if (!rtcOk) return 0;
  RTC_DateTime t = rtc.getDateTime();
  if (t.getYear() < 2025 || t.getYear() > 2120) return 0;  // sin hora valida
  struct tm tmv = {};
  tmv.tm_year = t.getYear() - 1900;
  tmv.tm_mon = t.getMonth() - 1;
  tmv.tm_mday = t.getDay();
  tmv.tm_hour = t.getHour();
  tmv.tm_min = t.getMinute();
  tmv.tm_sec = t.getSecond();
  time_t e = mktime(&tmv);  // TZ por defecto = UTC, consistente con gmtime_r
  return e > 0 ? (uint32_t)e : 0;
}

void rtcSetEpoch(uint32_t e) {
  if (!rtcOk) return;
  time_t tt = e;
  struct tm tmv;
  gmtime_r(&tt, &tmv);
  rtc.setDateTime(RTC_DateTime(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                               tmv.tm_hour, tmv.tm_min, tmv.tm_sec));
}

bool batBegin() {
  pmuOk = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL);
  if (!pmuOk) Serial.println("AXP2101 no detectado");
  return pmuOk;
}

// Enciende la alimentacion de la AMOLED. En la Waveshare 1.75 el panel (OLED VDD)
// cuelga del rail BLDO1 a 3.3V del AXP2101. El firmware daba por hecho que estaba
// encendido; si el PMU se resetea (drenaje total), BLDO1 queda OFF y la pantalla
// se ve negra aunque el resto funcione. Hay que llamarla ANTES de gfx->begin().
void pmuEnablePanel() {
  if (!pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)) {
    Serial.println("AXP2101 no detectado (pmuEnablePanel)");
    return;
  }
  pmu.setBLDO1Voltage(3300);   // OLED VDD
  pmu.enableBLDO1();
}

// el estado de energia (I2C) se cachea ~2 s: leerlo en cada frame del loop
// metia trafico I2C inutil y podia oscilar (parpadeo de brillo)
static uint32_t powerCacheT = 0;
static int cachedPct = -1;
static bool cachedCharging = false, cachedUsb = true;

static void refreshPower() {
  uint32_t now = millis();
  if (powerCacheT && now - powerCacheT < 2000) return;
  powerCacheT = now ? now : 1;
  if (!pmuOk) { cachedPct = -1; cachedCharging = false; cachedUsb = true; return; }
  cachedPct = pmu.isBatteryConnect() ? pmu.getBatteryPercent() : -1;
  cachedCharging = pmu.isCharging();
  cachedUsb = pmu.isVbusIn();
}

int batPercent() { refreshPower(); return cachedPct; }
bool batCharging() { refreshPower(); return cachedCharging; }
bool usbPresent() { refreshPower(); return cachedUsb; }

void pwrSetup() {
  if (!pmuOk) return;
  // A 4-second hold is a HARDWARE power-off inside the AXP2101: the rails drop
  // and the firmware is never told. That is why the long-press interrupt below
  // matters -- it fires at the PMU's long-press threshold, which comes BEFORE
  // the off threshold, and is the only warning the board can get that it is
  // about to lose power. The lead time is the PMU's, not ours, so measure it on
  // hardware before relying on it for anything slower than one NVS write; if it
  // turns out too tight, XPOWERS_POWEROFF_6S widens the gap at the cost of a
  // longer hold to switch the thing off.
  //
  // Deliberately NOT disableLongPressShutdown() plus a software pmu.shutdown():
  // that is the tidier design, but it makes powering the device off depend on
  // the firmware still running, and a hang would leave the player with no way
  // out short of draining the battery.
  pmu.setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
  pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ | XPOWERS_AXP2101_PKEY_LONG_IRQ |
                XPOWERS_AXP2101_WARNING_LEVEL1_IRQ |
                XPOWERS_AXP2101_WARNING_LEVEL2_IRQ);
  // The gauge's own warning, so a save can be flushed before the battery browns
  // out rather than after. batPercent() was only ever used to draw an icon.
  pmu.setLowBatWarnThreshold(15);
  pmu.clearIrqStatus();
}

// The AXP2101 LATCHES its interrupts and one register read reports all of them,
// so there can be only ONE poller: two callers each doing getIrqStatus() and
// then clearIrqStatus() would clear the bit the other had not looked at yet.
// This reads once, remembers what it saw, and clears; the accessors below
// consume what was remembered.
//
// It also clears UNCONDITIONALLY. The old code cleared only when it had seen a
// short press, which was harmless while short press was the only interrupt
// enabled and is not any more -- an unacknowledged long-press or low-battery bit
// would stay latched forever and every later read would see it again.
static bool sawShort = false, sawLong = false, sawLowBat = false;

void pwrPoll() {
  if (!pmuOk) return;
  pmu.getIrqStatus();
  if (pmu.isPekeyShortPressIrq()) sawShort = true;
  if (pmu.isPekeyLongPressIrq()) sawLong = true;
  if (pmu.isDropWarningLevel1Irq() || pmu.isDropWarningLevel2Irq()) sawLowBat = true;
  pmu.clearIrqStatus();
}

bool pwrShortPressed() { bool hit = sawShort; sawShort = false; return hit; }
bool pwrLongPressed() { bool hit = sawLong; sawLong = false; return hit; }
bool batLowWarning() { bool hit = sawLowBat; sawLowBat = false; return hit; }
