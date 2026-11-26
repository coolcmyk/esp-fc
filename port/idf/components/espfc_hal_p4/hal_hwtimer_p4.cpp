// ESP32-P4 implementation of Hal::HwTimer on the IDF gptimer driver.
#include "Hal/HwTimer.hpp"

#include "driver/gptimer.h"
#include "esp_attr.h"

namespace Hal {

namespace {

struct TimerCtx {
  HwTimer::Callback cb = nullptr;
  void* arg = nullptr;
};

gptimer_handle_t s_timer[2] = {nullptr, nullptr};
TimerCtx s_ctx[2];

bool IRAM_ATTR onAlarm(gptimer_handle_t, const gptimer_alarm_event_data_t*, void* user) {
  TimerCtx* ctx = static_cast<TimerCtx*>(user);
  if (!ctx || !ctx->cb) return false;
  return ctx->cb(ctx->arg);
}

} // namespace

bool HwTimer::begin(uint32_t intervalUs, Callback callback, void* arg) {
  if (_running || intervalUs == 0) return false;
  _callback = callback;
  _arg = arg;
  _interval = intervalUs;

  gptimer_config_t cfg = {};
  cfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
  cfg.direction = GPTIMER_COUNT_UP;
  cfg.resolution_hz = 1000000;
  if (gptimer_new_timer(&cfg, &s_timer[_id]) != ESP_OK) return false;

  s_ctx[_id].cb = callback;
  s_ctx[_id].arg = arg;

  gptimer_event_callbacks_t cbs = {};
  cbs.on_alarm = onAlarm;
  if (gptimer_register_event_callbacks(s_timer[_id], &cbs, &s_ctx[_id]) != ESP_OK) {
    return false;
  }

  gptimer_alarm_config_t alarm = {};
  alarm.alarm_count = intervalUs;
  alarm.reload_count = 0;
  alarm.flags.auto_reload_on_alarm = true;
  if (gptimer_set_alarm_action(s_timer[_id], &alarm) != ESP_OK) return false;
  if (gptimer_enable(s_timer[_id]) != ESP_OK) return false;
  if (gptimer_start(s_timer[_id]) != ESP_OK) return false;

  _running = true;
  return true;
}

void HwTimer::end() {
  if (!_running || !s_timer[_id]) return;
  gptimer_stop(s_timer[_id]);
  gptimer_disable(s_timer[_id]);
  gptimer_del_timer(s_timer[_id]);
  s_timer[_id] = nullptr;
  s_ctx[_id].cb = nullptr;
  _running = false;
}

} // namespace Hal
