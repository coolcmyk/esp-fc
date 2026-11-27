// Host tests for the emote profiles. These run without hardware: a flip is a
// pure function of time, so its shape, duration and total rotation can be
// checked before anything spins.
#include "Control/Emote.hpp"
#include <unity.h>

using namespace Espfc::Control;

void test_flip_duration()
{
  TEST_ASSERT_EQUAL_UINT32(640, EmoteProfile::durationMs(EMOTE_FLIP_360));
  TEST_ASSERT_EQUAL_UINT32(390, EmoteProfile::durationMs(EMOTE_ROLL_180));
  TEST_ASSERT_EQUAL_UINT32(0, EmoteProfile::durationMs(EMOTE_NONE));
}

void test_flip_starts_and_ends_at_zero()
{
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.f, EmoteProfile::rateDps(EMOTE_FLIP_360, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.f, EmoteProfile::rateDps(EMOTE_FLIP_360, 640));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.f, EmoteProfile::rateDps(EMOTE_FLIP_360, 2000));
}

void test_flip_reaches_peak_in_hold()
{
  TEST_ASSERT_FLOAT_WITHIN(0.01f, EmoteProfile::FLIP_PEAK_DPS, EmoteProfile::rateDps(EMOTE_FLIP_360, 100));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, EmoteProfile::FLIP_PEAK_DPS, EmoteProfile::rateDps(EMOTE_FLIP_360, 300));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, EmoteProfile::FLIP_PEAK_DPS, EmoteProfile::rateDps(EMOTE_FLIP_360, 489));
}

void test_flip_ramp_is_slew_limited()
{
  // the trailing counter-blip is a deliberate step, so only the ramp/hold/decel
  // part has to be slew limited
  const uint32_t slewEnd = EmoteProfile::RAMP_MS + EmoteProfile::holdMs(EMOTE_FLIP_360) +
                           EmoteProfile::DECEL_MS;
  const float maxStep = EmoteProfile::FLIP_PEAK_DPS / float(EmoteProfile::RAMP_MS) + 0.001f;
  float prev = EmoteProfile::rateDps(EMOTE_FLIP_360, 0);
  for (uint32_t t = 1; t < slewEnd; ++t)
  {
    const float now = EmoteProfile::rateDps(EMOTE_FLIP_360, t);
    TEST_ASSERT_TRUE(fabsf(now - prev) <= maxStep);
    prev = now;
  }
}

void test_flip_rotation_is_about_360_degrees()
{
  const float deg = EmoteProfile::totalDegrees(EMOTE_FLIP_360);
  TEST_ASSERT_FLOAT_WITHIN(360.f * 0.05f, 360.f, fabsf(deg));
}

void test_roll180_rotation_is_about_180_degrees()
{
  const float deg = EmoteProfile::totalDegrees(EMOTE_ROLL_180);
  TEST_ASSERT_FLOAT_WITHIN(180.f * 0.08f, 180.f, fabsf(deg));
}

void test_direction_flips_sign()
{
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -EmoteProfile::rateDps(EMOTE_FLIP_360, 300),
                                    EmoteProfile::rateDps(EMOTE_FLIP_360, 300, -1));
  TEST_ASSERT_FLOAT_WITHIN(360.f * 0.05f, -360.f, EmoteProfile::totalDegrees(EMOTE_FLIP_360, -1));
}

void test_thrust_boost_only_while_active()
{
  TEST_ASSERT_FLOAT_WITHIN(0.001f, EmoteProfile::THRUST_SCALE, EmoteProfile::thrustScale(EMOTE_FLIP_360, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, EmoteProfile::THRUST_SCALE, EmoteProfile::thrustScale(EMOTE_FLIP_360, 639));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.f, EmoteProfile::thrustScale(EMOTE_FLIP_360, 640));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.f, EmoteProfile::thrustScale(EMOTE_NONE, 100));
}

void test_blip_opposes_rotation()
{
  const float blip = EmoteProfile::rateDps(EMOTE_FLIP_360, EmoteProfile::durationMs(EMOTE_FLIP_360) - 1);
  TEST_ASSERT_TRUE(blip < 0.f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -EmoteProfile::FLIP_PEAK_DPS * EmoteProfile::BLIP_FACTOR, blip);
}

int main(int argc, char** argv)
{
  UNITY_BEGIN();
  RUN_TEST(test_flip_duration);
  RUN_TEST(test_flip_starts_and_ends_at_zero);
  RUN_TEST(test_flip_reaches_peak_in_hold);
  RUN_TEST(test_flip_ramp_is_slew_limited);
  RUN_TEST(test_flip_rotation_is_about_360_degrees);
  RUN_TEST(test_roll180_rotation_is_about_180_degrees);
  RUN_TEST(test_direction_flips_sign);
  RUN_TEST(test_thrust_boost_only_while_active);
  RUN_TEST(test_blip_opposes_rotation);
  UNITY_END();
  return 0;
}
