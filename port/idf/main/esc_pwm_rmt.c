#include "esc_pwm_rmt.h"

#include <string.h>

#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "esc_pwm";

typedef struct {
    gpio_num_t pin;
    rmt_channel_handle_t chan;
    rmt_encoder_handle_t encoder;
    rmt_symbol_word_t frame[2];
    uint32_t period_us;
    bool active;
} esc_channel_t;

static esc_channel_t s_channels[ESC_PWM_MAX_CHANNELS];
static int s_count;
static bool s_running;
static TaskHandle_t s_task;

static void make_frame(esc_channel_t *ch, uint32_t pulse_us) {
    if (pulse_us > ch->period_us) pulse_us = ch->period_us;
    ch->frame[0] = (rmt_symbol_word_t){
        .level0 = 1,
        .duration0 = (uint16_t)pulse_us,
        .level1 = 0,
        .duration1 = (uint16_t)(ch->period_us - pulse_us),
    };
    /* Unused second symbol keeps the copy encoder happy without extra data. */
    ch->frame[1] = (rmt_symbol_word_t){0};
}

esp_err_t esc_pwm_init(const esc_pwm_config_t *configs, int count) {
    ESP_RETURN_ON_FALSE(configs && count > 0 && count <= ESC_PWM_MAX_CHANNELS,
                        ESP_ERR_INVALID_ARG, TAG, "bad config");

    s_count = count;
    for (int i = 0; i < count; ++i) {
        esc_channel_t *ch = &s_channels[i];
        ch->pin = configs[i].pin;
        ch->period_us = configs[i].period_us ? configs[i].period_us : 2500;

        rmt_tx_channel_config_t tx_cfg = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .gpio_num = ch->pin,
            .mem_block_symbols = 48,
            .resolution_hz = 1000000, /* 1 tick = 1 us */
            .trans_queue_depth = 2,
        };
        ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&tx_cfg, &ch->chan), TAG,
                            "channel %d create failed", i);

        rmt_copy_encoder_config_t enc_cfg = {};
        ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&enc_cfg, &ch->encoder), TAG,
                            "encoder %d create failed", i);

        make_frame(ch, configs[i].pulse_us ? configs[i].pulse_us : 1000);
        ch->active = true;
        ESP_LOGI(TAG, "ESC %d: GPIO%d, %lu us high, %lu us period", i, (int)ch->pin,
                 (unsigned long)ch->frame[0].duration0,
                 (unsigned long)ch->period_us);
    }
    return ESP_OK;
}

esp_err_t esc_pwm_set_pulse_us(int channel, uint32_t pulse_us) {
    ESP_RETURN_ON_FALSE(channel >= 0 && channel < s_count, ESP_ERR_INVALID_ARG, TAG,
                        "bad channel");
    make_frame(&s_channels[channel], pulse_us);
    return ESP_OK;
}

static void transmit_all(void) {
    for (int i = 0; i < s_count; ++i) {
        esc_channel_t *ch = &s_channels[i];
        if (!ch->active) continue;
        rmt_transmit_config_t tx = {.loop_count = 0};
        /* If the previous frame is still queued this returns ESP_ERR_INVALID_STATE;
         * dropping a frame is harmless for an ESC, so the error is ignored. */
        rmt_transmit(ch->chan, ch->encoder, ch->frame, sizeof(ch->frame), &tx);
    }
}

static void esc_task(void *arg) {
    (void)arg;
    /* 400 Hz frame rate, matching the bench rig. */
    TickType_t period = pdMS_TO_TICKS(2);
    if (period == 0) period = 1;
    TickType_t wake = xTaskGetTickCount();
    while (s_running) {
        transmit_all();
        vTaskDelayUntil(&wake, period);
    }
    vTaskDelete(NULL);
}

esp_err_t esc_pwm_start(void) {
    ESP_RETURN_ON_FALSE(s_count > 0, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    if (s_running) return ESP_OK;
    for (int i = 0; i < s_count; ++i) {
        ESP_RETURN_ON_ERROR(rmt_enable(s_channels[i].chan), TAG, "enable %d failed", i);
    }
    s_running = true;
    if (xTaskCreate(esc_task, "esc_pwm", 3072, NULL, 10, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t esc_pwm_stop(void) {
    if (!s_running) return ESP_OK;
    s_running = false;
    vTaskDelay(pdMS_TO_TICKS(10));
    for (int i = 0; i < s_count; ++i) {
        rmt_disable(s_channels[i].chan);
    }
    return ESP_OK;
}
