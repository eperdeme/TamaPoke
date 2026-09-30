#include "Arduino.h"
#include "../../../audio.cpp"
#include "audio_stubs/runtime.h"

uint32_t g_seed = 17;
FakeSerial Serial;
FakeESP ESP;
FakeWire Wire;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }

static int bad = 0;

static void ck(bool ok, const char *what) {
  printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) bad++;
}

static void runCase(uint8_t music, bool interrupt, uint8_t volume, const char *what) {
  gSyn.allOff();
  audioRuntime.requests.clear();
  size_t writesBefore = audioRuntime.writes;
  size_t waitsBefore = audioRuntime.waits;
  bool stoppedWithVoices = false;
  bool stopOnWrite = interrupt;
  audioRuntime.onWrite = [&] {
    if (stopOnWrite) {
      stopOnWrite = false;
      stoppedWithVoices = gSyn.busy();
      audioMusic(MUS_NONE);
    }
  };
  gVol = volume;
  audioMusic(music);
  bool reachedIdle = false;
  uint32_t budget = 1000;
  if (!interrupt) {
    const MusicTrack &track = MUSIC_TBL[music == MUS_VICTORY ? 3 : 0];
    for (uint16_t index = 0; index < track.n1; index++) budget += track.ch1[index].ms;
  }
  try {
    for (uint32_t ms = 0; ms < budget && !reachedIdle; ms += 16) {
      audioRuntime.advance(16);
      reachedIdle = audioRuntime.idle() && audioRuntime.waits > waitsBefore;
    }
  } catch (const std::exception &failure) {
    printf("FAIL %s\n", failure.what());
  }
  audioRuntime.onWrite = nullptr;
  ck(reachedIdle && audioRuntime.writes > writesBefore, what);
  if (interrupt) ck(stoppedWithVoices, "the stop arrived while real synth voices were still active");
}

int main() {
  audioBegin();
  ck(audioRuntime.taskStarts == 1 && audioEnabled(), "the real audio initializer creates its task");
  if (audioRuntime.taskStarts != 1) return 1;
  runCase(MUS_BATTLE, true, 7, "stopping battle music silences voices and reaches a blocking wait");
  runCase(MUS_VICTORY, true, 7, "dismissing victory music cannot starve the idle task");
  runCase(MUS_VICTORY, false, 7, "the victory fanfare finishes without leaving a spinning task");
  runCase(MUS_VICTORY, false, 0, "a muted victory fanfare still finishes and blocks");
  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}