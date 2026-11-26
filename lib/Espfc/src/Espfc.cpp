#include "Espfc.hpp"
#include "Debug_Espfc.hpp"
#include "Hal/FastCode.hpp"
#include "Hal/Platform.hpp"
#include "Hal/Time.hpp"

namespace Espfc {

// The App's motor test (MSP_SET_MOTOR) is stopped this long after the last
// command, so a forgotten host cannot leave the ESCs spinning.
static constexpr uint32_t MOTOR_TEST_TIMEOUT_MS = 5000;

Espfc::Espfc()
    : _hardware{_model}, _controller{_model}, _telemetry{_model}, _input{_model, _telemetry}, _actuator{_model},
      _sensor{_model}, _mixer{_model}, _blackbox{_model}, _buzzer{_model}, _serial{_model, _telemetry}
{
}

int Espfc::load()
{
  PIN_DEBUG_INIT();
  _model.load();
  _model.appQueue.begin();
  return 1;
}

int Espfc::begin()
{
  _model.state.led.begin(_model.config.pin[PIN_LED_BLINK], _model.config.led.type, _model.config.led.invert);

  _serial.begin();   // requires _model.load()
  _hardware.begin(); // requires _model.load()
  _model.begin();    // requires _hardware.begin()
  _mixer.begin();
  _sensor.begin();   // requires _hardware.begin()
  _input.begin();    // requires _serial.begin()
  _actuator.begin(); // requires _model.begin()
  _controller.begin();
  _blackbox.begin(); // requires _serial.begin(), _actuator.begin()
  _buzzer.begin();

  _model.state.buzzer.push(BUZZER_SYSTEM_INIT);

  _model.setConfigChangeListener([this](ModelChangeEvent event) {
    _serial.reload(event);
    _sensor.reload(event);
    _input.reload(event);
    _controller.reload(event);
  });

  return 1;
}

int FAST_CODE_ATTR Espfc::update(bool externalTrigger)
{
  if (externalTrigger)
  {
    _model.state.gyro.timer.update();
  }
  else
  {
    if (!_model.state.gyro.timer.check()) return 0;
  }
  Utils::Stats::Measure measure(_model.state.stats, COUNTER_CPU_0);

  // Motor-test safety: MSP_SET_MOTOR is sticky, so stop it 5 s after the last
  // command. Without this a forgotten host leaves the ESCs spinning.
  if (_model.state.output.disarmedTime && (millis() - _model.state.output.disarmedTime) > MOTOR_TEST_TIMEOUT_MS)
  {
    for (size_t i = 0; i < OUTPUT_CHANNELS; i++)
    {
      _model.state.output.disarmed[i] =
          _model.config.output.channel[i].servo ? _model.config.output.channel[i].neutral : _model.config.output.minCommand;
    }
    _model.state.output.disarmedTime = 0;
  }

  if constexpr (Hal::MULTI_CORE)
  {
    _sensor.read();
    if (_model.state.input.timer.syncTo(_model.state.gyro.timer, 1u))
    {
      _input.update();
    }
    if (_model.state.actuatorTimer.check())
    {
      _actuator.update();
    }
  }
  else
  {
    _sensor.update();
    if (_model.state.loopTimer.syncTo(_model.state.gyro.timer))
    {
      _controller.update();
      if (_model.state.mixer.timer.syncTo(_model.state.loopTimer))
      {
        _mixer.update();
      }
      _blackbox.update();
      if (_model.state.input.timer.syncTo(_model.state.gyro.timer, 1u))
      {
        _input.update();
      }
      if (_model.state.actuatorTimer.check())
      {
        _actuator.update();
      }
    }
    _sensor.updateDelayed();
  }

  _serial.update();
  _buzzer.update();
  _model.state.led.update();
  _model.state.stats.update();

  return 1;
}

// other task
int FAST_CODE_ATTR Espfc::updateOther()
{
  if constexpr (Hal::MULTI_CORE)
  {
    Event e;
    if (!_model.appQueue.pop(e))
    {
      return 0;
    }

    Utils::Stats::Measure measure(_model.state.stats, COUNTER_CPU_1);

    switch (e.type)
    {
      case EVENT_GYRO_READ:
        _sensor.preLoop();
        _controller.update();
        // skip mixer and bb if earlier than half cycle, possible delay in previous iteration,
        // to keep space to receive dshot erpm frame, but process rest
        if (_loop_next < micros())
        {
          _loop_next = micros() + _model.state.loopTimer.interval / 2;
          _mixer.update();
          _blackbox.update();
        }
        _sensor.postLoop();
        break;
      case EVENT_ACCEL_READ:
        _sensor.fusion();
        break;
      default:
        break;
        // nothing
    }
  }

  return 1;
}

} // namespace Espfc
