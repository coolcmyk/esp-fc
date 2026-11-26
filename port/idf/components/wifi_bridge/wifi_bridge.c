// WiFi + TCP transport for orion-fc.
//
// The P4 has no radio of its own: WiFi goes through the on-board ESP32-C6 over
// SDIO (ESP-Hosted, exposed to the app as esp_wifi_remote).
#include "wifi_bridge.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

#define WIFI_SSID "galaxy hp"
#define WIFI_PASSWORD "galaxy12300000"
#define MSP_TCP_PORT 5760

static const char *TAG = "wifi_bridge";
static EventGroupHandle_t s_events;
static int s_client = -1;

#define GOT_IP BIT0

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    s_client = -1;
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    const ip_event_got_ip_t *ev = (const ip_event_got_ip_t *)data;
    ESP_LOGI(TAG, "ip " IPSTR ", MSP over TCP on port %d", IP2STR(&ev->ip_info.ip), MSP_TCP_PORT);
    xEventGroupSetBits(s_events, GOT_IP);
  }
}

static void tcp_task(void *arg) {
  (void)arg;
  int srv = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (srv < 0) {
    ESP_LOGE(TAG, "socket failed");
    vTaskDelete(NULL);
    return;
  }
  int yes = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons(MSP_TCP_PORT),
      .sin_addr.s_addr = htonl(INADDR_ANY),
  };
  if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(srv, 1) != 0) {
    ESP_LOGE(TAG, "bind/listen failed");
    close(srv);
    vTaskDelete(NULL);
    return;
  }
  ESP_LOGI(TAG, "listening on %d", MSP_TCP_PORT);

  for (;;) {
    struct sockaddr_in peer;
    socklen_t len = sizeof(peer);
    int fd = accept(srv, (struct sockaddr *)&peer, &len);
    if (fd < 0) continue;
    s_client = fd;
    ESP_LOGI(TAG, "client %s:%d connected", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));

    // Placeholder transport loop: echo until the peer closes. The MSP stream is
    // wired in here next.
    uint8_t buf[256];
    for (;;) {
      int n = recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) break;
      send(fd, buf, n, 0);
    }
    ESP_LOGI(TAG, "client disconnected");
    close(fd);
    s_client = -1;
  }
}

void wifi_bridge_start(void) {
  s_events = xEventGroupCreate();
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    err = nvs_flash_init();
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs: %s", esp_err_to_name(err));
    return;
  }

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL, NULL));

  wifi_config_t cfg = {0};
  strncpy((char *)cfg.sta.ssid, WIFI_SSID, sizeof(cfg.sta.ssid) - 1);
  strncpy((char *)cfg.sta.password, WIFI_PASSWORD, sizeof(cfg.sta.password) - 1);
  cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  ESP_ERROR_CHECK(esp_wifi_start());

  // Modem sleep adds 100-500 ms of latency to TCP round trips, which is
  // unusable for stick input. Keep the radio awake.
  ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

  xTaskCreatePinnedToCore(tcp_task, "msp_tcp", 4096, NULL, 12, NULL, 0);
}
