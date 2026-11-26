// ESP32-P4 implementation of Hal::SerialUart on the IDF UART driver.
#include "Hal/Serial.hpp"

#include <cstring>

#include "driver/uart.h"
#include "esp_log.h"

namespace Hal {

namespace {

constexpr int RX_BUF = 512;
constexpr int TX_BUF = 0;  // blocking writes, no TX ring buffer

struct UartState {
  bool installed = false;
  int peek = -1;
  uart_port_t port;
};

UartState s_state[3] = {{false, -1, UART_NUM_0},
                        {false, -1, UART_NUM_1},
                        {false, -1, UART_NUM_2}};

Hal::SerialUart uart0(0);
Hal::SerialUart uart1(1);
Hal::SerialUart uart2(2);

uart_port_t portOf(int index) {
  return index == 1 ? UART_NUM_1 : index == 2 ? UART_NUM_2 : UART_NUM_0;
}

} // namespace

void SerialUart::begin(const SerialDeviceConfig& conf) {
  UartState& st = s_state[_index];
  const uart_port_t port = portOf(_index);

  if (!st.installed) {
    uart_config_t cfg = {};
    cfg.baud_rate = conf.baud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = conf.parity == SDC_SERIAL_PARITY_EVEN
                     ? UART_PARITY_EVEN
                     : conf.parity == SDC_SERIAL_PARITY_ODD ? UART_PARITY_ODD
                                                            : UART_PARITY_DISABLE;
    cfg.stop_bits = conf.stop_bits == SDC_SERIAL_STOP_BITS_2 ? UART_STOP_BITS_2
                                                             : UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    uart_driver_install(port, RX_BUF, TX_BUF, 0, nullptr, 0);
    uart_param_config(port, &cfg);
    uart_set_pin(port, conf.tx_pin >= 0 ? conf.tx_pin : UART_PIN_NO_CHANGE,
                 conf.rx_pin >= 0 ? conf.rx_pin : UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    st.installed = true;
  } else {
    uart_set_baudrate(port, conf.baud);
  }
  st.peek = -1;
}

void SerialUart::updateBaudRate(int baud) {
  if (s_state[_index].installed) uart_set_baudrate(portOf(_index), baud);
}

int SerialUart::available() {
  size_t len = 0;
  if (uart_get_buffered_data_len(portOf(_index), &len) != ESP_OK) return 0;
  return int(len) + (s_state[_index].peek >= 0 ? 1 : 0);
}

int SerialUart::read() {
  UartState& st = s_state[_index];
  if (st.peek >= 0) {
    const int c = st.peek;
    st.peek = -1;
    return c;
  }
  uint8_t c = 0;
  if (uart_read_bytes(portOf(_index), &c, 1, 0) == 1) return c;
  return -1;
}

size_t SerialUart::readMany(uint8_t* c, size_t l) {
  UartState& st = s_state[_index];
  size_t offset = 0;
  if (st.peek >= 0 && l > 0) {
    c[0] = uint8_t(st.peek);
    st.peek = -1;
    offset = 1;
  }
  if (offset >= l) return offset;
  const int n = uart_read_bytes(portOf(_index), c + offset, l - offset, 0);
  return offset + (n > 0 ? size_t(n) : 0);
}

int SerialUart::peek() {
  UartState& st = s_state[_index];
  if (st.peek < 0) {
    uint8_t c = 0;
    if (uart_read_bytes(portOf(_index), &c, 1, 0) == 1) st.peek = c;
  }
  return st.peek;
}

void SerialUart::flush() { uart_wait_tx_done(portOf(_index), pdMS_TO_TICKS(100)); }

size_t SerialUart::write(uint8_t c) {
  return uart_write_bytes(portOf(_index), &c, 1) == 1 ? 1 : 0;
}

size_t SerialUart::write(const uint8_t* c, size_t l) {
  const int n = uart_write_bytes(portOf(_index), c, l);
  return n > 0 ? size_t(n) : 0;
}

int SerialUart::availableForWrite() { return 128; }

bool SerialUart::isTxFifoEmpty() {
  return uart_wait_tx_done(portOf(_index), 0) == ESP_OK;
}

SerialUart* getSerialUart(int index) {
  switch (index) {
    case 0: return &uart0;
    case 1: return &uart1;
    case 2: return &uart2;
    default: return nullptr;
  }
}

} // namespace Hal
