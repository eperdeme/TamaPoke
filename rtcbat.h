#pragma once
#include <Arduino.h>

// RTC PCF85063: hora persistente mientras la placa tenga alimentacion
bool rtcBegin();
uint32_t rtcEpoch();             // segundos unix; 0 si el RTC no es valido
void rtcSetEpoch(uint32_t e);

// PMU AXP2101: estado de la bateria
bool batBegin();
void pmuEnablePanel();           // enciende BLDO1 (OLED VDD 3.3V); llamar antes de gfx->begin()
int batPercent();                // 0-100, -1 si no hay bateria conectada
bool batCharging();
bool usbPresent();

// boton PWR del AXP2101: pulsacion larga 4s = apagado fisico (RTC sigue vivo);
// la pulsacion corta la captura el firmware (pantalla on/off)
void pwrSetup();
// Reads the PMU's latched interrupts ONCE and remembers them. Call this exactly
// once per loop pass, before the accessors below -- they consume what it saw and
// do no I2C of their own. See the note in rtcbat.cpp for why there can only be
// one poller.
void pwrPoll();
bool pwrShortPressed();  // screen on/off
// The hold is on its way to the AXP2101's own power-off. This is the only
// warning the firmware gets that the rails are about to drop, so it is the one
// chance to flush a pending save.
bool pwrLongPressed();
// The gauge has dropped past its low-battery warning level. Same purpose: flush
// before a brownout rather than after it.
bool batLowWarning();
