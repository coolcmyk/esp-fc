/*
 * ESP32-P4 portability proof for esp-fc's blocking peripherals.
 *
 * Both drivers below replace code that cannot compile on the P4 anymore:
 *   - esc_pwm_rmt.c : EscDriverEsp32.cpp (legacy RMT private API)
 *   - loop_timer.c  : HwTimer.cpp (legacy timer_group API)
 */
#include <stdio.h>

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esc_pwm_rmt.h"
#include "esp_log.h"
#include "loop_timer.h"

static const char *TAG = "p4port";

static volatile uint32_t s_ticks;

static void on_tick(void) { ++s_ticks; }

void app_main(void) {
    ESP_LOGI(TAG, "esp-fc P4 port: ESC PWM + loop timer");

    const esc_pwm_config_t escs[4] = {
        {.pin = GPIO_NUM_6, .pulse_us = 1000, .period_us = 2500},
        {.pin = GPIO_NUM_20, .pulse_us = 1000, .period_us = 2500},
        {.pin = GPIO_NUM_21, .pulse_us = 1000, .period_us = 2500},
        {.pin = GPIO_NUM_22, .pulse_us = 1000, .period_us = 2500},
    };

    ESP_ERROR_CHECK(esc_pwm_init(escs, 4));
    ESP_ERROR_CHECK(loop_timer_start(1000, on_tick));
    ESP_ERROR_CHECK(esc_pwm_start());

    /* Disarmed: all channels at 1000 us. */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "loop ticks so far: %lu", (unsigned long)s_ticks);
    }
}
