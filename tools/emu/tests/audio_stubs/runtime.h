#pragma once
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

struct AudioStopped {};

struct AudioRuntime {
  std::deque<std::vector<uint8_t>> requests;
  size_t itemSize = 0, capacity = 0;
  size_t writes = 0, samples = 0, audible = 0;
  size_t received = 0, dropped = 0, waits = 0;
  size_t battleWrites = 0, victoryWrites = 0;
  unsigned emptyPolls = 0, taskStarts = 0;
  bool effect = false, dma = false;
  std::function<void()> onWrite;
  std::thread worker;
  std::mutex mutex;
  std::condition_variable changed;
  bool parked = false, permit = false, stopping = false, finished = false;
  uint64_t deadline = 0, now = 0;
  std::string error;

  ~AudioRuntime() { stop(); }

  void boundary(uint64_t us, bool isDma) {
    std::unique_lock<std::mutex> lock(mutex);
    deadline += us;
    dma = isDma;
    parked = true;
    changed.notify_all();
    changed.wait(lock, [&] { return permit || stopping; });
    if (stopping) throw AudioStopped();
    permit = false;
    parked = false;
  }

  void waitBoundary(std::unique_lock<std::mutex> &lock) {
    if (!changed.wait_for(lock, std::chrono::seconds(2), [&] { return parked || finished; })) {
      fprintf(stderr, "FAIL audio task did not reach a yielding boundary\n");
      std::_Exit(86);
    }
    if (!error.empty()) throw std::runtime_error(error);
    if (finished) throw std::runtime_error("audio task exited unexpectedly");
  }

  void start(TaskFunction_t entry, void *arg) {
    if (worker.joinable()) throw std::runtime_error("audio task initialized twice");
    taskStarts++;
    worker = std::thread([&, entry, arg] {
      try {
        boundary(0, false);
        entry(arg);
        error = "audio task returned unexpectedly";
      } catch (const AudioStopped &) {
      } catch (const std::exception &failure) {
        error = failure.what();
      }
      std::lock_guard<std::mutex> lock(mutex);
      parked = false;
      finished = true;
      changed.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    waitBoundary(lock);
  }

  void advance(uint32_t ms) {
    std::unique_lock<std::mutex> lock(mutex);
    if (!worker.joinable()) throw std::runtime_error("mandatory audio task was not initialized");
    now += (uint64_t)ms * 1000;
    waitBoundary(lock);
    while (deadline <= now) {
      parked = false;
      permit = true;
      changed.notify_all();
      waitBoundary(lock);
    }
  }

  bool idle() const { return !dma && requests.empty() && gMusic == MUS_NONE && !gSyn.busy(); }
  bool musicNote() const { return dma && !effect && gMusic != MUS_NONE && gSyn.busy(); }

  void stop() {
    if (!worker.joinable()) return;
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopping = true;
      changed.notify_all();
    }
    worker.join();
  }
};

inline AudioRuntime audioRuntime;

inline QueueHandle_t xQueueCreate(size_t count, size_t itemSize) {
  audioRuntime.capacity = count;
  audioRuntime.itemSize = itemSize;
  audioRuntime.requests.clear();
  return &audioRuntime.requests;
}

inline int xQueueSend(QueueHandle_t queue, const void *item, uint32_t) {
  if (queue != &audioRuntime.requests) throw std::runtime_error("invalid audio queue");
  if (audioRuntime.requests.size() >= audioRuntime.capacity) {
    audioRuntime.dropped++;
    return pdFALSE;
  }
  const auto *bytes = static_cast<const uint8_t *>(item);
  audioRuntime.requests.emplace_back(bytes, bytes + audioRuntime.itemSize);
  return pdTRUE;
}

inline int xQueueReceive(QueueHandle_t queue, void *item, uint32_t wait) {
  if (queue != &audioRuntime.requests) throw std::runtime_error("invalid audio queue");
  if (audioRuntime.requests.empty() && wait) {
    audioRuntime.emptyPolls = 0;
    audioRuntime.waits++;
    audioRuntime.boundary((uint64_t)wait * 1000, false);
  }
  audioRuntime.effect = !audioRuntime.requests.empty();
  if (audioRuntime.effect) {
    memcpy(item, audioRuntime.requests.front().data(), audioRuntime.itemSize);
    audioRuntime.requests.pop_front();
    audioRuntime.received++;
    audioRuntime.emptyPolls = 0;
    return pdTRUE;
  }
  if (!wait && ++audioRuntime.emptyPolls > 100)
    throw std::runtime_error("audio watchdog starvation: 100 nonblocking polls without DMA or a wait");
  return pdFALSE;
}

inline int xTaskCreatePinnedToCore(TaskFunction_t task, const char *, uint32_t,
                                  void *arg, uint32_t, void *, int) {
  audioRuntime.start(task, arg);
  return pdTRUE;
}

inline size_t emuAudioWrite(const uint8_t *data, size_t length) {
  if (!length || length % 4) throw std::runtime_error("invalid stereo DMA buffer");
  audioRuntime.emptyPolls = 0;
  audioRuntime.writes++;
  audioRuntime.samples += length / 4;
  const auto *pcm = reinterpret_cast<const int16_t *>(data);
  for (size_t index = 0; index < length / 2; index++)
    if (pcm[index]) audioRuntime.audible++;
  if (!audioRuntime.effect && gSyn.busy()) {
    if (gMusic == MUS_BATTLE) audioRuntime.battleWrites++;
    if (gMusic == MUS_VICTORY) audioRuntime.victoryWrites++;
  }
  if (audioRuntime.onWrite) audioRuntime.onWrite();
  audioRuntime.boundary((uint64_t)(length / 4) * 1000000 / SAMPLE_RATE, true);
  return length;
}