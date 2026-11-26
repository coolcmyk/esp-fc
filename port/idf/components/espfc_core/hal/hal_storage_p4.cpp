// ESP32-P4 implementation of Hal::ConfigStorage (NVS) and Hal::Flash (partition API).
#include "Hal/ConfigStorage.hpp"
#include "Hal/Flash.hpp"

#include <cstring>

#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace Hal {

namespace {
uint8_t* s_buffer = nullptr;
size_t s_bufferSize = 0;
nvs_handle_t s_nvs = 0;
bool s_nvsReady = false;
constexpr const char* NVS_NS = "espfc";
constexpr const char* NVS_KEY = "config";
} // namespace

bool ConfigStorage::begin(size_t size) {
  _size = size;
  free(s_buffer);
  s_buffer = static_cast<uint8_t*>(calloc(1, size));
  s_bufferSize = size;
  if (!s_buffer) return false;

  if (!s_nvsReady) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      nvs_flash_erase();
      err = nvs_flash_init();
    }
    if (err != ESP_OK) return false;
    if (nvs_open(NVS_NS, NVS_READWRITE, &s_nvs) != ESP_OK) return false;
    s_nvsReady = true;
  }

  size_t len = size;
  if (nvs_get_blob(s_nvs, NVS_KEY, s_buffer, &len) == ESP_OK && len == size) {
    return true;
  }
  return true; /* blank config: zeros are a valid starting point */
}

size_t ConfigStorage::size() const { return _size; }

uint8_t ConfigStorage::readByte(size_t addr) const {
  return (s_buffer && addr < s_bufferSize) ? s_buffer[addr] : 0;
}

void ConfigStorage::writeByte(size_t addr, uint8_t value) {
  if (s_buffer && addr < s_bufferSize) s_buffer[addr] = value;
}

size_t ConfigStorage::read(size_t addr, uint8_t* data, size_t len) const {
  if (!s_buffer || addr >= s_bufferSize) return 0;
  const size_t n = (addr + len <= s_bufferSize) ? len : s_bufferSize - addr;
  memcpy(data, s_buffer + addr, n);
  return n;
}

size_t ConfigStorage::write(size_t addr, const uint8_t* data, size_t len) {
  if (!s_buffer || addr >= s_bufferSize) return 0;
  const size_t n = (addr + len <= s_bufferSize) ? len : s_bufferSize - addr;
  memcpy(s_buffer + addr, data, n);
  return n;
}

bool ConfigStorage::commit() {
  if (!s_nvsReady || !s_buffer) return false;
  if (nvs_set_blob(s_nvs, NVS_KEY, s_buffer, s_bufferSize) != ESP_OK) return false;
  return nvs_commit(s_nvs) == ESP_OK;
}

FlashPartition Flash::findPartition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, nullptr);
}

uint32_t Flash::getSize(FlashPartition partition) {
  const auto* p = static_cast<const esp_partition_t*>(partition);
  return p ? p->size : 0;
}

uint32_t Flash::getSectorSize() { return 4096; }

size_t Flash::read(FlashPartition partition, uint32_t address, uint8_t* data, size_t len) {
  const auto* p = static_cast<const esp_partition_t*>(partition);
  if (!p) return 0;
  return esp_partition_read(p, address, data, len) == ESP_OK ? len : 0;
}

size_t Flash::write(FlashPartition partition, uint32_t address, const uint8_t* data, size_t len) {
  const auto* p = static_cast<const esp_partition_t*>(partition);
  if (!p) return 0;
  return esp_partition_write(p, address, data, len) == ESP_OK ? len : 0;
}

bool Flash::erase(FlashPartition partition, uint32_t address, uint32_t len) {
  const auto* p = static_cast<const esp_partition_t*>(partition);
  if (!p) return false;
  return esp_partition_erase_range(p, address, len) == ESP_OK;
}

size_t Flash::getPartitions(FlashPartitionInfo* list, size_t max) {
  (void)list;
  (void)max;
  return 0;
}

} // namespace Hal
