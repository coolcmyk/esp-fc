/*
 * ESP32-P4 ESC output, new-API implementation (replacement for EscDriverEsp32,
 * which uses the removed legacy rmt_ll.h / RMTMEM path).
 *
 * Four RMT TX channels, 1 MHz resolution (1 tick = 1 us), copy encoder.
 * Each channel continuously re-sends one PWM frame: high for pulse_us,
 * low for the rest of the period. This is the same PWM signal the bench rig
 * produces with LEDC, so the port can be validated against known-good output.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#define ESC_PWM_MAX_CHANNELS 4

typedef struct {
    gpio_num_t pin;
    uint32_t pulse_us;
    uint32_t period_us;
} esc_pwm_config_t;

esp_err_t esc_pwm_init(const esc_pwm_config_t *configs, int count);
esp_err_t esc_pwm_set_pulse_us(int channel, uint32_t pulse_us);
esp_err_t esc_pwm_start(void);
esp_err_t esc_pwm_stop(void);
