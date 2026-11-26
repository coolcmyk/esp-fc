#pragma once

#if defined(ESP32P4)

#include "Hal/EscDriver.hpp"

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"

namespace Hal::Detail {

// RMT TX implementation of the ESC output for the ESP32-P4.
// PWM only for now: DShot needs a different symbol encoder.
class EscDriverImpl {
public:
  EscDriverImpl() = default;

  bool begin(const EscConfig& conf);
  void end();
  bool attach(size_t channel, int pin, int pulse);
  void write(size_t channel, int pulse);
  void apply();
  int pin(size_t channel) const;
  uint32_t telemetry(size_t channel) const;

private:
  struct Channel {
    gpio_num_t pin = GPIO_NUM_NC;
    rmt_channel_handle_t chan = nullptr;
    rmt_encoder_handle_t enc = nullptr;
    rmt_symbol_word_t frame[2] = {};
    int pulse = 1000;
    bool enabled = false;
  };

  static void makeFrame(Channel& ch);

  Channel _ch[EscDriver::CHANNEL_COUNT];
  int64_t _lastTx = 0;
};

} // namespace Hal::Detail

#endif // ESP32P4
