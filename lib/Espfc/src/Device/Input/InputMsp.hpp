#pragma once

#include "Device/InputDevice.hpp"

#include <cstddef>
#include <cstdint>

namespace Espfc::Device::Input {

// Receiver fed by MSP_SET_RAW_RC (command 200): lets a host such as the
// Betaflight App inject stick input when no physical receiver is present.
class InputMsp : public InputDevice
{
public:
  static constexpr size_t CHANNELS = 16;
  static constexpr uint32_t TIMEOUT_MS = 500;

  int begin();

  InputStatus update() override;
  uint16_t get(uint8_t i) const override;
  void get(uint16_t* data, size_t len) const override;
  size_t getChannelCount() const override;
  bool needAverage() const override;

  // Called from the MSP command handler; shared across the single instance.
  static void setChannels(const uint16_t* data, size_t len);

private:
  static uint16_t _channels[CHANNELS];
  static uint32_t _lastUpdate;
  static bool _received;
};

} // namespace Espfc::Device::Input
