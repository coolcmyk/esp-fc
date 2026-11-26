// ESP32-P4 implementation of Hal::BusI2C on the IDF i2c_master driver.
#include "Hal/BusI2C.hpp"

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "esp_err.h"

namespace Hal {

namespace {
i2c_master_bus_handle_t s_bus = nullptr;
i2c_master_dev_handle_t s_dev[8] = {nullptr};
uint8_t s_devAddr[8] = {0};
Hal::BusI2C bus0(0);
} // namespace

BusI2C::BusI2C(size_t index) : _index(index) {}

BusType BusI2C::getType() const { return BUS_I2C; }

int BusI2C::begin(int sda, int scl, uint32_t speed) {
  if (s_bus) return 1;
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = I2C_NUM_0;
  cfg.sda_io_num = static_cast<gpio_num_t>(sda);
  cfg.scl_io_num = static_cast<gpio_num_t>(scl);
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;
  if (i2c_new_master_bus(&cfg, &s_bus) != ESP_OK) {
    s_bus = nullptr;
    return 0;
  }
  (void)speed;
  // The IDF i2c driver logs a multi-line error for every absent address while
  // esp-fc scans the bus; keep it quiet and report the scan result instead.
  esp_log_level_set("i2c.master", ESP_LOG_NONE);

  char found[96] = {0};
  for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
    if (i2c_master_probe(s_bus, addr, 20) == ESP_OK) {
      size_t len = strlen(found);
      snprintf(found + len, sizeof(found) - len, " 0x%02X", addr);
    }
  }
  ESP_LOGI("HalI2C", "SDA=%d SCL=%d devices:%s", sda, scl, found[0] ? found : " none");
  return 1;
}

namespace {
i2c_master_dev_handle_t devFor(uint8_t addr) {
  for (int i = 0; i < 8; ++i) {
    if (s_dev[i] && s_devAddr[i] == addr) return s_dev[i];
  }
  for (int i = 0; i < 8; ++i) {
    if (!s_dev[i]) {
      i2c_device_config_t cfg = {};
      cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
      cfg.device_address = addr;
      cfg.scl_speed_hz = 100000;
      if (i2c_master_bus_add_device(s_bus, &cfg, &s_dev[i]) != ESP_OK) return nullptr;
      s_devAddr[i] = addr;
      return s_dev[i];
    }
  }
  return nullptr;
}
} // namespace

int8_t BusI2C::read(uint8_t devAddr, uint8_t regAddr, uint8_t length, uint8_t* data) {
  return readFast(devAddr, regAddr, length, data);
}

int8_t BusI2C::readFast(uint8_t devAddr, uint8_t regAddr, uint8_t length, uint8_t* data) {
  if (!s_bus) return -1;
  i2c_master_dev_handle_t dev = devFor(devAddr);
  if (!dev) return -2;
  if (i2c_master_transmit_receive(dev, &regAddr, 1, data, length, 50) != ESP_OK) return -3;
  return 0;
}

bool BusI2C::write(uint8_t devAddr, uint8_t regAddr, uint8_t length, const uint8_t* data) {
  if (!s_bus) return false;
  i2c_master_dev_handle_t dev = devFor(devAddr);
  if (!dev) return false;
  uint8_t buf[16];
  if (length > sizeof(buf) - 1) return false;
  buf[0] = regAddr;
  for (uint8_t i = 0; i < length; ++i) buf[i + 1] = data[i];
  return i2c_master_transmit(dev, buf, length + 1, 50) == ESP_OK;
}

BusI2C* getBusI2C(size_t index) { return index == 0 ? &bus0 : nullptr; }

} // namespace Hal
