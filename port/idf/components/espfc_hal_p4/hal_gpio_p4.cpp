// ESP32-P4 implementation of Hal::Gpio (IDF gpio driver).
#include "Hal/Gpio.hpp"

#include <cstring>

#include "driver/gpio.h"
#include "esp_attr.h"

namespace Hal {

namespace {
gpio_isr_t s_isr = nullptr;
void* s_isrArg = nullptr;
} // namespace

void Gpio::digitalWrite(uint8_t pin, PinStatus val) {
  gpio_set_level(static_cast<gpio_num_t>(pin), val == High ? 1 : 0);
}

Gpio::PinStatus Gpio::digitalRead(uint8_t pin) {
  return gpio_get_level(static_cast<gpio_num_t>(pin)) ? High : Low;
}

void Gpio::pinMode(uint8_t pin, PinMode mode) {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << pin;
  switch (mode) {
    case Output:
      cfg.mode = GPIO_MODE_OUTPUT;
      break;
    case InputPullup:
      cfg.mode = GPIO_MODE_INPUT;
      cfg.pull_up_en = GPIO_PULLUP_ENABLE;
      break;
    default:
      cfg.mode = GPIO_MODE_INPUT;
      break;
  }
  gpio_config(&cfg);
}

void IRAM_ATTR gpioIsrTrampoline(void* arg) {
  if (s_isr) s_isr(arg);
}

void Gpio::attachInterrupt(uint8_t pin, InterruptHandler handler, void* arg,
                           InterruptMode mode) {
  const gpio_num_t gpio = static_cast<gpio_num_t>(pin);
  s_isr = reinterpret_cast<gpio_isr_t>(handler);
  s_isrArg = arg;

  gpio_config_t cfg = {};
  cfg.pin_bit_mask = 1ULL << pin;
  cfg.mode = GPIO_MODE_INPUT;
  cfg.intr_type = mode == Rising    ? GPIO_INTR_POSEDGE
                  : mode == Falling ? GPIO_INTR_NEGEDGE
                                    : GPIO_INTR_ANYEDGE;
  gpio_config(&cfg);

  static bool isrServiceInstalled = false;
  if (!isrServiceInstalled) {
    gpio_install_isr_service(0);
    isrServiceInstalled = true;
  }
  gpio_isr_handler_add(gpio, gpioIsrTrampoline, arg);
}

void Gpio::detachInterrupt(uint8_t pin) {
  gpio_isr_handler_remove(static_cast<gpio_num_t>(pin));
}

// Input capture is not needed on this target yet (RPM/ESC telemetry input).
bool Gpio::attachCaptureInterrupt(uint8_t, CaptureHandler, void*,
                                  InterruptMode) {
  return false;
}

void Gpio::detachCaptureInterrupt(uint8_t) {}

} // namespace Hal
