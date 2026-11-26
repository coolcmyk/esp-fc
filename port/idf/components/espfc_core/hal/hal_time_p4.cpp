// C time functions normally provided by the Arduino core (Hal/Time.hpp).
#include <cstdint>

#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {

void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

void delayMicroseconds(uint32_t us) { esp_rom_delay_us(us); }

unsigned long micros() { return static_cast<unsigned long>(esp_timer_get_time()); }

unsigned long millis() {
  return static_cast<unsigned long>(esp_timer_get_time() / 1000ULL);
}

} // extern "C"
