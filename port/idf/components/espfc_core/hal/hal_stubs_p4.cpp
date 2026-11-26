// Deliberately minimal implementations of Hal pieces this board does not use.
// SPI gyro: none (GY-91 is on I2C). No battery ADC. No addressable LED.
#include "Hal/Adc.hpp"
#include "Hal/BusSPI.hpp"
#include "Hal/RgbLed.hpp"

namespace Hal {

BusSPI::BusSPI(size_t index) : _index(index) {}
BusType BusSPI::getType() const { return BUS_SPI; }
int BusSPI::begin(int8_t, int8_t, int8_t, int8_t) { return 0; }
int8_t BusSPI::read(uint8_t, uint8_t, uint8_t, uint8_t*) { return -1; }
int8_t BusSPI::readFast(uint8_t, uint8_t, uint8_t, uint8_t*) { return -1; }
bool BusSPI::write(uint8_t, uint8_t, uint8_t, const uint8_t*) { return false; }
BusSPI* getBusSPI(size_t) { return nullptr; }

void Adc::begin(uint8_t) {}
uint16_t Adc::read(uint8_t) { return 0; }

} // namespace Hal

namespace Hal {
// No addressable LED on this board.
void RgbLed::begin(int8_t) {}
void RgbLed::write(const RgbColor&) {}
} // namespace Hal
