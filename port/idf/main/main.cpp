// esp-fc on the ESP32-P4: bring the controller up and run its loop.
#include "Espfc.hpp"

extern "C" {
#include "wifi_bridge.h"
#include "msp_tcp.h"
}

#include "board_p4_nano.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "orion";

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "starting orion-fc on ESP32-P4");
  wifi_bridge_start();
  static Espfc::Espfc fc;
  const int load = fc.load();
  ESP_LOGI(TAG, "config load: %d", load);
  const int begin = fc.begin();
  ESP_LOGI(TAG, "begin: %d", begin);

  // Serve the App's MSP over the WiFi socket, sharing the FC's model.
  msp_tcp_start(&fc.model());

  for (;;) {
    fc.update();
    fc.updateOther();
    vTaskDelay(1);
  }
}
