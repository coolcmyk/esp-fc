// MSP over TCP: lets the App's TCP transport talk to the flight controller.
#include "msp_tcp.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <vector>

#include "Connect/MspProcessor.hpp"
#include "Connect/Cli.hpp"
#include "Stream/ReadWritable.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_bridge.h"

extern "C" {
// Provided by wifi_bridge: the accepted client socket (or -1).
int wifi_bridge_client_fd(void);
void wifi_bridge_release_client(void);
}

namespace {

const char *TAG = "msp_tcp";

// Minimal ReadWritable backed by the socket: MSP only writes responses here,
// incoming bytes are fed to the parser directly.
class SocketStream : public Espfc::Stream::ReadWritable {
public:
  explicit SocketStream(int fd) : _fd(fd) {}

  void begin(const Hal::SerialDeviceConfig &) override {}
  void updateBaudRate(int) override {}
  int available() override { return 0; }
  int read() override { return -1; }
  size_t readMany(uint8_t *, size_t) override { return 0; }
  int peek() override { return -1; }

  size_t write(uint8_t c) override {
    _tx.push_back(c);
    return 1;
  }
  size_t write(const uint8_t *data, size_t len) override {
    _tx.insert(_tx.end(), data, data + len);
    return len;
  }
  int availableForWrite() override { return 512; }
  void flush() override { send_now(); }
  bool isTxFifoEmpty() override { return _tx.empty(); }

  void send_now() {
    if (_tx.empty()) return;
    size_t off = 0;
    while (off < _tx.size()) {
      ssize_t n = ::send(_fd, _tx.data() + off, _tx.size() - off, 0);
      if (n <= 0) break;
      off += size_t(n);
    }
    _tx.clear();
  }

private:
  int _fd;
  std::vector<uint8_t> _tx;
};

} // namespace

void msp_tcp_start(Espfc::Model *model) {
  static Espfc::Connect::MspProcessor processor(*model);
  static Espfc::Connect::Cli cli(*model);

  xTaskCreatePinnedToCore(
      [](void *) {
        for (;;) {
          int fd = wifi_bridge_client_fd();
          if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
          }
          ESP_LOGI(TAG, "serving MSP over the TCP client");

          SocketStream stream(fd);
          Espfc::Connect::MspMessage msg;
          Espfc::Connect::MspResponse resp;
          Espfc::CliCmd cmd;

          uint8_t buf[128];
          while (fd == wifi_bridge_client_fd()) {
            const int n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            for (int i = 0; i < n; ++i) {
              const char c = char(buf[i]);
              const bool consumed = processor.parse(c, msg);
              if (consumed) {
                if (msg.isReady()) {
                  if (msg.isCmd()) {
                    processor.processCommand(msg, resp, stream);
                    processor.sendResponse(resp, stream);
                    processor.postCommand();
                  }
                  msg = {};
                  resp = {};
                }
              } else {
                // CLI sessions (waypoints) share the same socket.
                Espfc::Stream::Printer printer(stream);
                cli.process(c, cmd, printer);
              }
            }
            stream.send_now();
          }
          ESP_LOGI(TAG, "client gone");
          wifi_bridge_release_client();
          vTaskDelay(pdMS_TO_TICKS(50));
        }
      },
      "msp_tcp", 6144, nullptr, 10, nullptr, 0);
}
