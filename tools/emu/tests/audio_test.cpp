#include "Arduino.h"
#include "../../../sdmon.h"
#include "../../../adpcm.h"
#include <cmath>
#include <cstring>
#include <vector>

bool sdReady = true;
uint32_t sdTransferAt = 0;
static int16_t g_cryTestRequestedDex = 0;
static std::vector<uint8_t> g_cryTestBlob;
bool sdLoadCry(int16_t dexNum, uint8_t **outBuf, uint32_t *outLen) {
  g_cryTestRequestedDex = dexNum;
  if (g_cryTestBlob.empty() || !outBuf || !outLen) return false;
  uint8_t *copy = (uint8_t *)malloc(g_cryTestBlob.size());
  if (!copy) return false;
  memcpy(copy, g_cryTestBlob.data(), g_cryTestBlob.size());
  *outBuf = copy;
  *outLen = (uint32_t)g_cryTestBlob.size();
  return true;
}

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
  int paHighsBeforeAudible = g_emuPaHighs;
  runCase(MUS_VICTORY, false, 7, "the victory fanfare finishes without leaving a spinning task");
  ck(g_emuPaHighs > paHighsBeforeAudible && g_emuPaVal == LOW,
     "audible playback raises PA while playing and drops it after finishing");
  int paHighsBeforeMute = g_emuPaHighs;
  runCase(MUS_VICTORY, false, 0, "a muted victory fanfare still finishes and blocks");
  ck(g_emuPaHighs == paHighsBeforeMute && g_emuPaVal == LOW,
     "master volume 0 keeps PA powered off while pacing silent frames");

  // --- Three-tier audioCry() execution in real audioTask ---
  auto runCry = [&](int16_t dex, uint8_t volume, uint32_t budgetMs, const char *what) {
    gSyn.allOff();
    audioRuntime.requests.clear();
    size_t writesBefore = audioRuntime.writes;
    size_t audibleBefore = audioRuntime.audible;
    size_t waitsBefore = audioRuntime.waits;
    gVol = volume;
    audioCry(dex);
    bool reachedIdle = false;
    for (uint32_t ms = 0; ms < budgetMs && !reachedIdle; ms += 16) {
      audioRuntime.advance(16);
      reachedIdle = audioRuntime.idle() && audioRuntime.waits > waitsBefore;
    }
    ck(reachedIdle && audioRuntime.writes > writesBefore && g_emuPaVal == LOW, what);
    return audioRuntime.audible - audibleBefore;
  };

  {
    const size_t N = 640;
    std::vector<int16_t> pcm(N);
    for (size_t i = 0; i < N; i++)
      pcm[i] = (int16_t)(10000.0 * sin(2.0 * 3.14159265 * 440.0 * (double)i / 16000.0));
    g_cryTestBlob.resize(CRY_ADPCM_HDR + (N + 1) / 2);
    size_t n = adpcmEncode(pcm.data(), N, g_cryTestBlob.data(), g_cryTestBlob.size());
    g_cryTestBlob.resize(n);
    size_t aud = runCry(25, 7, 1500, "Tier 1 SD ADPCM cry plays cleanly and drops PA");
    ck(aud > 0 && g_cryTestRequestedDex == 25, "Tier 1 SD ADPCM cry outputs audible PCM samples");

    size_t audMute = runCry(25, 0, 1500, "Tier 1 SD ADPCM cry at volume 0 completes cleanly");
    ck(audMute == 0, "Tier 1 SD ADPCM cry at volume 0 outputs zero audible samples");
    g_cryTestBlob.clear();
  }

  {
    size_t audGb = runCry(25, 7, 2500, "Tier 2 Gen 1-2 GB cry falls back cleanly and drops PA");
    ck(audGb > 0, "Tier 2 Gen 1-2 GB cry produces audible samples");
  }

  {
    size_t audProc = runCry(384, 7, 1500, "Tier 3 Gen 3+ procedural chirp falls back cleanly and drops PA");
    ck(audProc > 0, "Tier 3 Gen 3+ procedural chirp produces audible samples");
  }

  printf("%s\n", bad ? "FAILURES" : "all good");
  return bad ? 1 : 0;
}