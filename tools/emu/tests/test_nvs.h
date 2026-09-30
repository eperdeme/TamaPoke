#pragma once
#include "Preferences.h"

#ifdef TAMA_NATIVE_TEST
#include <nvs.h>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

using NvsStore = std::map<std::string, std::vector<uint8_t>>;

inline void requireTestNvs(esp_err_t result) {
  if (result == ESP_OK) return;
  std::printf("FAIL native NVS fixture read: %d\n", int(result));
  std::abort();
}

template <typename Value>
inline std::vector<uint8_t> testNvsScalar(nvs_handle_t handle, const char *key,
                                        esp_err_t (*reader)(nvs_handle_t, const char *, Value *)) {
  Value value{};
  requireTestNvs(reader(handle, key, &value));
  const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
  return {bytes, bytes + sizeof(value)};
}

inline NvsStore readTestNvs() {
  NvsStore result;
  nvs_handle_t handle;
  const esp_err_t opened = nvs_open("tamapoke", NVS_READONLY, &handle);
  if (opened == ESP_ERR_NVS_NOT_FOUND) return result;
  requireTestNvs(opened);
  nvs_iterator_t iterator = nullptr;
  esp_err_t found = nvs_entry_find("nvs", "tamapoke", NVS_TYPE_ANY, &iterator);
  while (found == ESP_OK) {
    nvs_entry_info_t info;
    requireTestNvs(nvs_entry_info(iterator, &info));
    std::vector<uint8_t> value;
    switch (info.type) {
      case NVS_TYPE_U8: value = testNvsScalar(handle, info.key, nvs_get_u8); break;
      case NVS_TYPE_I8: value = testNvsScalar(handle, info.key, nvs_get_i8); break;
      case NVS_TYPE_U16: value = testNvsScalar(handle, info.key, nvs_get_u16); break;
      case NVS_TYPE_I16: value = testNvsScalar(handle, info.key, nvs_get_i16); break;
      case NVS_TYPE_U32: value = testNvsScalar(handle, info.key, nvs_get_u32); break;
      case NVS_TYPE_I32: value = testNvsScalar(handle, info.key, nvs_get_i32); break;
      case NVS_TYPE_U64: value = testNvsScalar(handle, info.key, nvs_get_u64); break;
      case NVS_TYPE_I64: value = testNvsScalar(handle, info.key, nvs_get_i64); break;
      case NVS_TYPE_STR:
      case NVS_TYPE_BLOB: {
        size_t size = 0;
        const bool text = info.type == NVS_TYPE_STR;
        requireTestNvs(text ? nvs_get_str(handle, info.key, nullptr, &size) :
                 nvs_get_blob(handle, info.key, nullptr, &size));
        value.resize(size);
        requireTestNvs(text ? nvs_get_str(handle, info.key, reinterpret_cast<char *>(value.data()), &size) :
                 nvs_get_blob(handle, info.key, value.data(), &size));
        break;
      }
      default: requireTestNvs(ESP_ERR_INVALID_ARG);
    }
    value.insert(value.begin(), static_cast<uint8_t>(info.type));
    result[info.key] = value;
    found = nvs_entry_next(&iterator);
  }
  if (found != ESP_ERR_NVS_NOT_FOUND) requireTestNvs(found);
  nvs_release_iterator(iterator);
  nvs_close(handle);
  return result;
}
#else
inline const NvsStore &readTestNvs() { return nvs(); }
#endif