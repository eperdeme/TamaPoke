#include "Arduino.h"
#include <deque>
#include <vector>
#include "../../../audio.cpp"

uint32_t g_seed = 17;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

struct AudioIdle {};
struct AudioStarved {};
static TaskFunction_t taskEntry = nullptr;
static std::deque<std::vector<uint8_t>> requests;
static size_t requestSize = 0;
static size_t writes = 0;
static unsigned emptyPolls = 0;
static bool stopOnWrite = false;
static bool stoppedWithVoices = false;
static int bad = 0;

static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

QueueHandle_t xQueueCreate(size_t, size_t itemSize) {
  requestSize = itemSize;
  requests.clear();
  return &requests;
}

int xQueueSend(QueueHandle_t, const void *item, uint32_t) {
  const auto *bytes = static_cast<const uint8_t *>(item);
  requests.emplace_back(bytes, bytes + requestSize);
  return pdTRUE;
}

int xQueueReceive(QueueHandle_t, void *item, uint32_t wait) {
  if (!requests.empty()) {
    memcpy(item, requests.front().data(), requestSize);
    requests.pop_front();
    return pdTRUE;
  }
  if (wait) throw AudioIdle();
  if (++emptyPolls > 100) throw AudioStarved();
  return pdFALSE;
}

int xTaskCreatePinnedToCore(TaskFunction_t task, const char *, uint32_t,
                          void *, uint32_t, void *, int) {
  taskEntry = task;
  return pdTRUE;
}

size_t emuAudioWrite(const uint8_t *, size_t length) {
  emptyPolls = 0;
  if (++writes > 10000) throw AudioStarved();
  if (stopOnWrite) {
    stopOnWrite = false;
    stoppedWithVoices = gSyn.busy();
    audioMusic(MUS_NONE);
  }
  return length;
}

static void runCase(uint8_t music, bool interrupt, uint8_t volume, const char *what) {
  gSyn.allOff();
  requests.clear();
  writes = emptyPolls = 0;
  stopOnWrite = interrupt;
  stoppedWithVoices = false;
  gVol = volume;
  audioMusic(music);
  bool reachedIdle = false;
  try {
    taskEntry(nullptr);
  } catch (const AudioIdle &) {
    reachedIdle = true;
  } catch (const AudioStarved &) {
  }
  ck(reachedIdle && writes > 0 && !gSyn.busy() && gMusic == MUS_NONE, what);
  if (interrupt) ck(stoppedWithVoices, "the stop arrived while real synth voices were still active");
}

int main() {
  audioBegin();
  ck(taskEntry != nullptr && audioEnabled(), "the real audio initializer creates its task");
  if (!taskEntry) return 1;
  runCase(MUS_BATTLE, true, 7, "stopping battle music silences voices and reaches a blocking wait");
  runCase(MUS_VICTORY, true, 7, "dismissing victory music cannot starve the idle task");
  runCase(MUS_VICTORY, false, 7, "the victory fanfare finishes without leaving a spinning task");
  runCase(MUS_VICTORY, false, 0, "a muted victory fanfare still finishes and blocks");
  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}