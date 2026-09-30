#pragma once
#include "Arduino.h"

using QueueHandle_t = void *;
using TaskFunction_t = void (*)(void *);
constexpr int pdTRUE = 1;
constexpr int pdFALSE = 0;
constexpr int HIGH = 1;
constexpr int LOW = 0;
constexpr int I2S_MODE_STD = 0;
constexpr int I2S_DATA_BIT_WIDTH_16BIT = 16;
constexpr int I2S_SLOT_MODE_STEREO = 2;
constexpr int I2S_STD_SLOT_BOTH = 3;

inline uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
inline void digitalWrite(int, int) {}
QueueHandle_t xQueueCreate(size_t count, size_t itemSize);
int xQueueSend(QueueHandle_t queue, const void *item, uint32_t wait);
int xQueueReceive(QueueHandle_t queue, void *item, uint32_t wait);
int xTaskCreatePinnedToCore(TaskFunction_t task, const char *name, uint32_t stack,
                          void *arg, uint32_t priority, void *handle, int core);
size_t emuAudioWrite(const uint8_t *data, size_t length);

class I2SClass {
public:
  void setPins(int, int, int, int, int) {}
  bool begin(int, int, int, int, int) { return true; }
  size_t write(uint8_t *data, size_t length) { return emuAudioWrite(data, length); }
};