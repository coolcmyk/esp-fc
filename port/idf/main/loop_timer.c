#include "loop_timer.h"

#include "driver/gptimer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "loop_timer";

static gptimer_handle_t s_timer;
static TaskHandle_t s_task;
static loop_tick_cb_t s_cb;

static bool IRAM_ATTR on_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata,
                               void *user_ctx) {
    (void)timer;
    (void)edata;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR((TaskHandle_t)user_ctx, &woken);
    return woken == pdTRUE;
}

static void tick_task(void *arg) {
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_cb) s_cb();
    }
}

esp_err_t loop_timer_start(uint32_t freq_hz, loop_tick_cb_t cb) {
    if (freq_hz == 0) return ESP_ERR_INVALID_ARG;
    s_cb = cb;

    if (xTaskCreate(tick_task, "loop_tick", 4096, NULL, 20, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000, /* 1 us per tick */
    };
    esp_err_t err = gptimer_new_timer(&cfg, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gptimer_new_timer: %s", esp_err_to_name(err));
        return err;
    }

    gptimer_event_callbacks_t cbs = {.on_alarm = on_alarm};
    err = gptimer_register_event_callbacks(s_timer, &cbs, s_task);
    if (err != ESP_OK) return err;

    uint64_t period_us = 1000000ULL / freq_hz;
    gptimer_alarm_config_t alarm = {
        .alarm_count = period_us,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    err = gptimer_set_alarm_action(s_timer, &alarm);
    if (err != ESP_OK) return err;

    err = gptimer_enable(s_timer);
    if (err != ESP_OK) return err;

    err = gptimer_start(s_timer);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "loop tick at %lu Hz (%llu us)", (unsigned long)freq_hz,
                 (unsigned long long)period_us);
    }
    return err;
}

esp_err_t loop_timer_stop(void) {
    if (!s_timer) return ESP_OK;
    gptimer_stop(s_timer);
    gptimer_disable(s_timer);
    gptimer_del_timer(s_timer);
    s_timer = NULL;
    return ESP_OK;
}
