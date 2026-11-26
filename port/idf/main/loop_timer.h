/*
 * Control-loop tick for ESP32-P4, new-API replacement for HwTimer
 * (the legacy driver/timer.h timer_group API is gone in IDF 5.5).
 *
 * gptimer alarm -> ISR -> task notification -> user callback, so the callback
 * runs in task context (esp-fc's loop expects that).
 */
#pragma once

#include <stdint.h>

#include "esp_err.h"

typedef void (*loop_tick_cb_t)(void);

esp_err_t loop_timer_start(uint32_t freq_hz, loop_tick_cb_t cb);
esp_err_t loop_timer_stop(void);
