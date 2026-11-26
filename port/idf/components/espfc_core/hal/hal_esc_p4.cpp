// ESP32-P4 implementation of Hal::Detail::EscDriverImpl on the new RMT TX driver.
//
// Emits standard PWM (1000-2000 us, 400 Hz) on the configured pins. DShot is not
// implemented yet; the frame encoder would use the same channel with a different
// symbol stream, so the structure is ready for it.
#include "EscDriverP4.hpp"

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_timer.h"

namespace Hal::Detail {

namespace {
constexpr uint32_t PERIOD_US = 2500;  // 400 Hz
}

bool EscDriverImpl::begin(const EscConfig& conf) {
  (void)conf;
  return true;
}

void EscDriverImpl::makeFrame(Channel& ch) {
  uint32_t pulse = ch.pulse < 500 ? 500 : (ch.pulse > 2500 ? 2500 : uint32_t(ch.pulse));
  const uint32_t rest = PERIOD_US - pulse;
  ch.frame[0] = rmt_symbol_word_t{.duration0 = static_cast<uint16_t>(pulse),
                                  .level0 = 1,
                                  .duration1 = static_cast<uint16_t>(rest),
                                  .level1 = 0};
  ch.frame[1] = rmt_symbol_word_t{};
}

void EscDriverImpl::end() {
  for (auto& ch : _ch) {
    if (ch.chan) {
      rmt_disable(ch.chan);
      rmt_del_channel(ch.chan);
      ch.chan = nullptr;
    }
    ch.enabled = false;
  }
}

bool EscDriverImpl::attach(size_t channel, int pin, int pulse) {
  if (channel >= EscDriver::CHANNEL_COUNT || pin < 0) return false;
  Channel& ch = _ch[channel];
  ch.pin = static_cast<gpio_num_t>(pin);
  ch.pulse = pulse;
  makeFrame(ch);

  if (!ch.chan) {
    rmt_tx_channel_config_t cfg = {};
    cfg.clk_src = RMT_CLK_SRC_DEFAULT;
    cfg.gpio_num = ch.pin;
    cfg.mem_block_symbols = 48;
    cfg.resolution_hz = 1000000;
    cfg.trans_queue_depth = 2;
    if (rmt_new_tx_channel(&cfg, &ch.chan) != ESP_OK) {
      ch.chan = nullptr;
      return false;
    }
    rmt_copy_encoder_config_t enc = {};
    if (rmt_new_copy_encoder(&enc, &ch.enc) != ESP_OK) return false;
    if (rmt_enable(ch.chan) != ESP_OK) return false;
  }
  ch.enabled = true;
  return true;
}

void EscDriverImpl::write(size_t channel, int pulse) {
  if (channel < EscDriver::CHANNEL_COUNT) _ch[channel].pulse = pulse;
}

void EscDriverImpl::apply() {
  const int64_t now = esp_timer_get_time();
  if (now - _lastTx < int64_t(PERIOD_US)) return;  // 400 Hz frame rate
  _lastTx = now;
  for (auto& ch : _ch) {
    if (!ch.enabled || !ch.chan) continue;
    makeFrame(ch);
    rmt_transmit_config_t tx = {};
    tx.loop_count = 0;
    rmt_transmit(ch.chan, ch.enc, ch.frame, sizeof(ch.frame), &tx);
  }
}

int EscDriverImpl::pin(size_t channel) const {
  return channel < EscDriver::CHANNEL_COUNT ? static_cast<int>(_ch[channel].pin) : -1;
}

uint32_t EscDriverImpl::telemetry(size_t) const {
  return EscDriver::INVALID_TELEMETRY_VALUE;
}

} // namespace Hal::Detail
