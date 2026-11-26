# ESP32-P4 target: gap analysis

Status: **not supported**. esp-fc currently builds for `esp32`, `esp32s2`,
`esp32s3`, `esp32c3`, `esp8266`, `rp2040`, `rp2350` (see `platformio.ini`).
There is no `esp32p4` environment, no board variant and no HAL path that
compiles for the P4.

This document exists so the port can be done deliberately instead of by
guessing, and so the bench rig in `tools/esp32-p4-rig/` has a clear relationship
to the flight firmware.

## What already works for this hardware

The sensors on the bench rig do **not** need new code:

| Part | Driver | Note |
| --- | --- | --- |
| MPU6500 (GY-91) | `GyroMPU6500` | matches `WHO_AM_I == 0x70` |
| BMP280 (GY-91) | `BaroBMP280` | I2C 0x76/0x77 |
| u-blox M10 GPS | `GpsSensor` | NMEA over a UART |

So the missing piece is the MCU target, not the sensor support.

## Gaps, in dependency order

1. **Build target.** PlatformIO's `espressif32` platform needs a P4 board
   definition and an Arduino core that supports the P4 (arduino-esp32 4.x does).
   Until that exists, a P4 env cannot even resolve. Verify with
   `pio boards esp32p4` before writing the env.

2. **ESC output (DShot).** `lib/Hal/src/ESP32/EscDriverEsp32.cpp` talks to the
   RMT peripheral through the **legacy** private API:

   ```c
   #include "hal/rmt_ll.h"
   #include "soc/rmt_periph.h"
   #define RMT_RX_CHANNEL_ENCODING_START (SOC_RMT_CHANNELS_PER_GROUP - ...)
   rmt_ll_write_memory(&RMTMEM, channel, ...);
   ```

   On the P4 the supported route is the new `driver/rmt_tx.h` encoder API, or
   MCPWM. Either way this file needs a P4 variant rather than a recompile.

3. **Timer / control loop.** `lib/Hal/src/ESP32/HwTimer.cpp` uses the legacy
   `driver/timer.h` group API. Check it against the P4's timer groups and,
   importantly, measure the achievable loop rate: the P4 is dual-core RISC-V at
   up to 400 MHz, but the 4 kHz loop claim in the README was measured on Xtensa.

4. **Board variant.** Pin map for this rig: I2C SDA=7 / SCL=8, GPS UART RX=5,
   ESCs on 6/20/21/22, console on UART0 (GPIO37/38 through the USB bridge).

5. **Build options.** The P4 on this board is silicon revision **v1.3** and
   needs the pre-v3.00 code path (`ChipVariant=prev3` in Arduino terms), plus
   `CDCOnBoot=disabled` so `Serial` stays on UART0 — otherwise the console is
   silent. The equivalent flags must be set in the PlatformIO env.

## Proposed environment (to be validated, not yet working)

```ini
[env:esp32p4]
board = esp32-p4-devkit      ; name to be confirmed against `pio boards`
platform = espressif32@^7.1.0
framework = arduino
board_build.f_cpu = 360000000L
board_build.partitions = partitions_4M_nota.csv
build_flags =
  ${env.build_flags}
  -DESP32P4
  -DARDUINO_USB_CDC_ON_BOOT=0
```

## Verification ladder

Do not skip straight to a flight test. The bench rig exists for this:

1. `tools/esp32-p4-rig/firmware/sys_check` — proves I2C (GY-91, BMP280), UART
   (GPS NMEA) and PWM output on the four ESC pins.
2. `tools/esp32-p4-rig/firmware/esc_test` — proves the ESC signal path,
   including direction and startup threshold, with a log that survives a
   battery-only run.
3. Only then enable the DShot path in esp-fc and compare against the PWM
   baseline from step 2.
4. Props off for every one of these steps.

## Bench status (measured on the P4-NANO, 2026-11)

| Subsystem | Result |
| --- | --- |
| Console / upload | works, with `ChipVariant=prev3` + `CDCOnBoot=disabled` |
| MPU6500 (GY-91) | works, `WHO_AM_I 0x70` at I2C 0x68 |
| BMP280 (GY-91) | works, I2C 0x76 |
| GPS (M10) | clean NMEA at 115200 |
| ESC PWM x4 | works, startup threshold ~1200-1300 us |
| microSD | works, SDMMC slot 0, 4-bit |
| MIPI-CSI camera | **blocked**: OV5647 detected (PID 0x5647 at 0x36) but CSI start fails |

The camera failure is the only hardware blocker and it is a library problem, not
a board problem:

```
E dw-gdma: dw_gdma_channel_register_event_callbacks: user context not in internal RAM
E CSI: failed to register dwgdma callback
```

- with PSRAM enabled: the GDMA callback context is not in internal RAM
- without PSRAM: `esp_video_buffer: Failed to malloc for video buffer`

A frame needs PSRAM; the callback context needs internal RAM; the prebuilt
Arduino ESP-Video stack cannot satisfy both. Fixing it means building esp_video
from source with an internal-RAM allocation for the CSI context, or moving the
camera path to ESP-IDF.

## Suggested port order

1. Add a `esp32p4` PlatformIO env and confirm a minimal esp-fc sketch links.
2. Replace the legacy RMT ESC driver with a `driver/rmt_tx` implementation,
   validating against the PWM baseline measured by `tools/esp32-p4-rig`.
3. Port `HwTimer` to the P4 timer groups and measure the achievable loop rate.
4. Keep the camera out of the flight firmware entirely: it belongs on a
   companion path (or an ESP-IDF co-processor), not in the control loop.
