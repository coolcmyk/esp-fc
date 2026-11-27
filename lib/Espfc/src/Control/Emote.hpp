#pragma once

#include <cstdint>

namespace Espfc::Control {

// Scripted flight manoeuvres ("emotes"). A profile is a pure function of
// elapsed time: it only produces rate setpoints, the normal rate PID does the
// flying. Keeping it pure is what makes it testable on the host.
enum EmoteKind : uint8_t
{
  EMOTE_NONE = 0,
  EMOTE_FLIP_360 = 1,  // one full roll, ~360 deg
  EMOTE_ROLL_180 = 2,  // half roll, the safe first attempt
};

struct EmoteProfile
{
  static constexpr uint32_t RAMP_MS = 100;     // slew in, no step into the PID
  static constexpr uint32_t DECEL_MS = 120;    // gentle enough not to overshoot
  static constexpr uint32_t BLIP_MS = 30;      // counter pulse to stop the spin
  static constexpr float THRUST_SCALE = 1.1f;  // make up for lost thrust upside down
  static constexpr float BLIP_FACTOR = 0.15f;  // fraction of peak, opposite sign

  static constexpr uint32_t FLIP_HOLD_MS = 390;
  static constexpr float FLIP_PEAK_DPS = 720.f;
  static constexpr uint32_t ROLL180_HOLD_MS = 140;
  static constexpr float ROLL180_PEAK_DPS = 720.f;

  static uint32_t holdMs(EmoteKind k)
  {
    return k == EMOTE_ROLL_180 ? ROLL180_HOLD_MS : FLIP_HOLD_MS;
  }

  static float peakDps(EmoteKind k)
  {
    return k == EMOTE_ROLL_180 ? ROLL180_PEAK_DPS : FLIP_PEAK_DPS;
  }

  static uint32_t durationMs(EmoteKind k)
  {
    if (k == EMOTE_NONE) return 0;
    return RAMP_MS + holdMs(k) + DECEL_MS + BLIP_MS;
  }

  // Signed rate setpoint in deg/s for the roll axis at `elapsedMs`.
  static float rateDps(EmoteKind k, uint32_t elapsedMs, int8_t direction = 1)
  {
    if (k == EMOTE_NONE) return 0.f;
    if (elapsedMs >= durationMs(k)) return 0.f;

    const float peak = peakDps(k);
    const uint32_t holdEnd = RAMP_MS + holdMs(k);
    const uint32_t decelEnd = holdEnd + DECEL_MS;
    const float dir = direction < 0 ? -1.f : 1.f;

    float rate = 0.f;
    if (elapsedMs < RAMP_MS)
    {
      rate = peak * (float(elapsedMs) / float(RAMP_MS));
    }
    else if (elapsedMs < holdEnd)
    {
      rate = peak;
    }
    else if (elapsedMs < decelEnd)
    {
      const float t = float(elapsedMs - holdEnd) / float(DECEL_MS);
      rate = peak * (1.f - t);
    }
    else
    {
      rate = -peak * BLIP_FACTOR;
    }
    return rate * dir;
  }

  // Thrust multiplier while the emote is flying.
  static float thrustScale(EmoteKind k, uint32_t elapsedMs)
  {
    if (k == EMOTE_NONE || elapsedMs >= durationMs(k)) return 1.f;
    return THRUST_SCALE;
  }

  // Degrees travelled if the setpoint were tracked exactly; used by tests and
  // by the pre-flight sanity check.
  static float totalDegrees(EmoteKind k, int8_t direction = 1)
  {
    const uint32_t dt = 1;  // 1 ms integration step
    float sum = 0.f;
    for (uint32_t t = 0; t < durationMs(k); t += dt)
    {
      sum += rateDps(k, t, direction) * (float(dt) / 1000.f);
    }
    return sum;
  }
};

} // namespace Espfc::Control
