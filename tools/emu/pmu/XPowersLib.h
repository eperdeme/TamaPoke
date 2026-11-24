// Stand-in for XPowersLib's AXP2101, for pmu_test only: it compiles the REAL
// rtcbat.cpp. What matters is reproduced as upstream XPowersLib does it:
//
//   int readRegister(reg)          -> -1 on a failed I2C transaction
//   uint8_t statusRegister[3]      <- getIrqStatus() stores that int raw, so a
//                                     failed read becomes 0xFF: EVERY bit set
//   isPekeyShortPressIrq() / Long / DropWarning -> masked bits of statusRegister
//
// rtcbat.cpp keeps its XPowersPMU file-static, so chip state lives in these
// globals where the test can drive it without adding hooks to the firmware.
#pragma once
#include "Arduino.h"
#define AXP2101_SLAVE_ADDRESS 0x34
#define XPOWERS_POWEROFF_4S 0
#define XPOWERS_AXP2101_ALL_IRQ 0xFFFFFFFFUL
#define XPOWERS_AXP2101_WARNING_LEVEL2_IRQ (1UL << 0)
#define XPOWERS_AXP2101_WARNING_LEVEL1_IRQ (1UL << 1)
#define XPOWERS_AXP2101_PKEY_LONG_IRQ (1UL << 10)
#define XPOWERS_AXP2101_PKEY_SHORT_IRQ (1UL << 11)

inline uint8_t g_pmuStatus[3] = { 0, 0, 0 };   // the chip's three INTSTS registers
inline int g_pmuFailNext = 0;                  // this many register reads fail next
inline int g_pmuClears = 0;                    // clearIrqStatus() call count
inline bool g_pmuBatConn = true;
inline int g_pmuBatPct = 80;
inline bool g_pmuCharging = false;
inline bool g_pmuVbusIn = true;
inline uint16_t g_pmuBattMv = 3900;
inline bool g_pmuBattMeasEnabled = false;
inline uint16_t g_pmuSysPowerDownMv = 0;
inline int g_pmuShutdowns = 0;

class XPowersPMU {
public:
  bool begin(FakeWire &, uint8_t, int, int) { return true; }
  void setBLDO1Voltage(int) {}
  void enableBLDO1() {}
  void enableBattVoltageMeasure() { g_pmuBattMeasEnabled = true; }
  void setSysPowerDownVoltage(uint16_t mv) { g_pmuSysPowerDownMv = mv; }
  bool isBatteryConnect() { return g_pmuBatConn; }
  int getBatteryPercent() { return g_pmuBatPct; }
  uint16_t getBattVoltage() { return g_pmuBattMv; }
  bool isCharging() { return g_pmuCharging; }
  bool isVbusIn() { return g_pmuVbusIn; }
  void shutdown() { g_pmuShutdowns++; }
  void setPowerKeyPressOffTime(int) {}
  void setLowBatWarnThreshold(uint8_t) {}
  void disableIRQ(uint32_t m) {
    intRegister[0] &= ~(m & 0xFF);
    intRegister[1] &= ~((m >> 8) & 0xFF);
    intRegister[2] &= ~((m >> 16) & 0xFF);
  }
  void enableIRQ(uint32_t m) {
    intRegister[0] |= m & 0xFF;
    intRegister[1] |= (m >> 8) & 0xFF;
    intRegister[2] |= (m >> 16) & 0xFF;
  }
  void clearIrqStatus() {
    g_pmuClears++;
    for (int i = 0; i < 3; i++) { g_pmuStatus[i] = 0; statusRegister[i] = 0; }
  }
  uint64_t getIrqStatus() {
    statusRegister[0] = (uint8_t)readRegister(0);
    statusRegister[1] = (uint8_t)readRegister(1);
    statusRegister[2] = (uint8_t)readRegister(2);
    return ((uint64_t)statusRegister[2] << 16) |
           ((uint64_t)statusRegister[1] << 8) |
           (uint64_t)statusRegister[0];
  }
  bool isPekeyShortPressIrq() {
    uint8_t mask = (uint8_t)(XPOWERS_AXP2101_PKEY_SHORT_IRQ >> 8);
    return (intRegister[1] & mask) && (statusRegister[1] & mask);
  }
  bool isPekeyLongPressIrq() {
    uint8_t mask = (uint8_t)(XPOWERS_AXP2101_PKEY_LONG_IRQ >> 8);
    return (intRegister[1] & mask) && (statusRegister[1] & mask);
  }
  bool isDropWarningLevel1Irq() {
    uint8_t mask = (uint8_t)XPOWERS_AXP2101_WARNING_LEVEL1_IRQ;
    return (intRegister[0] & mask) && (statusRegister[0] & mask);
  }
  bool isDropWarningLevel2Irq() {
    uint8_t mask = (uint8_t)XPOWERS_AXP2101_WARNING_LEVEL2_IRQ;
    return (intRegister[0] & mask) && (statusRegister[0] & mask);
  }
private:
  uint8_t statusRegister[3] = { 0, 0, 0 };
  uint8_t intRegister[3] = { 0, 0, 0 };
  int readRegister(int reg) {
    if (g_pmuFailNext > 0) { g_pmuFailNext--; return -1; }
    return g_pmuStatus[reg];
  }
};
