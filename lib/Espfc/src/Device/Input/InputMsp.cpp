#include "Device/Input/InputMsp.hpp"

#include "Hal/Time.hpp"

#include <algorithm>

namespace Espfc::Device::Input {

uint16_t InputMsp::_channels[CHANNELS] = {1500, 1500, 1000, 1500, 1000, 1000, 1000, 1000,
                                          1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};
uint32_t InputMsp::_lastUpdate = 0;
bool InputMsp::_received = false;

int InputMsp::begin() {
  _received = false;
  return 1;
}

void InputMsp::setChannels(const uint16_t* data, size_t len) {
  const size_t n = std::min(len, CHANNELS);
  for (size_t i = 0; i < n; ++i) _channels[i] = data[i];
  _lastUpdate = micros();
  _received = n > 0;
}

InputStatus InputMsp::update() {
  if (!_received) return INPUT_IDLE;
  const uint32_t elapsed = micros() - _lastUpdate;
  return elapsed > TIMEOUT_MS * 1000 ? INPUT_LOST : INPUT_RECEIVED;
}

uint16_t InputMsp::get(uint8_t i) const {
  return i < CHANNELS ? _channels[i] : 1500;
}

void InputMsp::get(uint16_t* data, size_t len) const {
  const size_t n = std::min(len, CHANNELS);
  for (size_t i = 0; i < n; ++i) data[i] = _channels[i];
}

size_t InputMsp::getChannelCount() const { return CHANNELS; }

bool InputMsp::needAverage() const { return false; }

} // namespace Espfc::Device::Input
