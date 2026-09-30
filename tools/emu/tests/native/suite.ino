#include <Arduino.h>
#include <Preferences.h>
#include "pet.h"
#include "i18n.h"

struct NativeSuite {
  const char *name;
  int (*run)();
  uint32_t seed;
};
#include "suite_config.h"

SET_LOOP_TASK_STACK_SIZE(64 * 1024);
void sfxPlay(uint8_t) {}

void setup() {
  Serial0.begin(115200);
  setvbuf(stdout, nullptr, _IONBF, 0);
  const unsigned total = sizeof(NATIVE_SUITES) / sizeof(NATIVE_SUITES[0]);
  Serial0.setTimeout(2000);
  Serial0.println("MILESTONE native-suite-ready");
  const String selection = Serial0.readStringUntil('\n');
  char *end = nullptr;
  const unsigned index = strtoul(selection.c_str(), &end, 10);
  if (!selection.length() || *end || index >= total) {
    Serial0.println("FAIL native suite selection invalid");
    return;
  }
  const auto &suite = NATIVE_SUITES[index];
  Preferences fixture;
  if (!fixture.begin("tamapoke", false) || !fixture.clear()) {
    Serial0.println("FAIL native NVS fixture isolation");
    return;
  }
  fixture.end();
  setLang(LANG_DEFAULT);
  randomSeed(suite.seed);
  Serial0.printf("MILESTONE native-suite-started %s\n", suite.name);
  const int result = suite.run();
  Serial0.printf("%s native-suite-complete %s result=%d\n", result == 0 ? "PASS" : "FAIL",
                 suite.name, result);
  Serial0.flush();
}

void loop() { delay(1000); }