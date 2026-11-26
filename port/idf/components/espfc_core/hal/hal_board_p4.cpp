// ESP32-P4 implementation of Hal::Board.
#include "Hal/Board.hpp"

#include "esp_chip_info.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"

namespace Hal {

uint32_t Board::getId0() {
  uint32_t id = 0;
  esp_efuse_mac_get_default(reinterpret_cast<uint8_t*>(&id));
  return id;
}

uint32_t Board::getId1() {
  uint64_t mac = 0;
  esp_efuse_mac_get_default(reinterpret_cast<uint8_t*>(&mac));
  return uint32_t(mac >> 32);
}

uint32_t Board::getId2() {
  uint64_t mac = 0;
  esp_efuse_mac_get_default(reinterpret_cast<uint8_t*>(&mac));
  return uint32_t(mac >> 16);
}

void Board::reset() { esp_restart(); }

Hal::ResetReason Board::getResetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return ResetReason::POWER_ON;
    case ESP_RST_EXT: return ResetReason::EXTERNAL_RESET;
    case ESP_RST_SW: return ResetReason::SOFTWARE;
    case ESP_RST_PANIC: return ResetReason::PANIC;
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return ResetReason::WATCHDOG;
    case ESP_RST_BROWNOUT: return ResetReason::BROWNOUT;
    case ESP_RST_DEEPSLEEP: return ResetReason::DEEP_SLEEP;
    default: return ResetReason::OTHER;
  }
}

uint32_t Board::getCpuFreq() { return 360000000; }

uint32_t Board::getFreeHeap() { return static_cast<uint32_t>(esp_get_free_heap_size()); }

} // namespace Hal
