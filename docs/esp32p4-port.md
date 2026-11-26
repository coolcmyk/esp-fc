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

## Port progress (IDF 5.5, esp32p4)

The two blocking peripherals are implemented with the new APIs and **verified
running on the Waveshare ESP32-P4-NANO**:

| Blocker | Replacement | Status |
| --- | --- | --- |
| `EscDriverEsp32.cpp` (legacy `rmt_ll.h`, `RMTMEM`, `soc/rmt_periph.h`) | `port/idf/main/esc_pwm_rmt.c` — `driver/rmt_tx` + copy encoder, 4 channels, 1 MHz resolution | builds, runs: GPIO6/20/21/22 at 1000 us / 2500 us |
| `HwTimer.cpp` (legacy `driver/timer.h` timer_group) | `port/idf/main/loop_timer.c` — `driver/gptimer` alarm → task notify | builds, runs: 9996 ticks in 10.0 s (≈1000 Hz) |

Confirmed missing on P4 in IDF 5.5: `hal/rmt_ll.h`, `soc/rmt_periph.h`,
`driver/timer.h`. Present and used instead: `driver/rmt_tx.h`,
`driver/rmt_encoder.h`, `driver/gptimer.h`. P4 RMT capabilities:
8 channels, 4 TX candidates, 48 words/channel, TX sync + loop auto-stop.

Build/flash:

```bash
. $IDF_PATH/export.sh
cd port/idf
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/ttyACM0 flash
```

### Remaining before esp-fc itself runs

1. Port the Arduino-dependent HAL files (`BusI2C`, `BusSPI`, `Gpio`, `Serial`,
   `Adc`) to IDF drivers — 5 files, `Arduino.h` currently included in 19 files
   repo-wide but only the ESP32 HAL ones matter.
2. Board definition for the P4-NANO (I2C 7/8, GPS UART 5, ESC 6/20/21/22,
   console UART0).
3. Wire `EscDriverEsp32` and `HwTimer` to these replacements behind a target
   switch (`ESP32P4`), keeping the existing ESP32/S3 paths untouched.
4. Then the esp-fc core (`lib/Espfc`, AHRS, Gps) builds as a component.

## Verified: the Betaflight App can drive this board

Flashed `port/idf` on the Waveshare ESP32-P4-NANO and exercised the same MSP
traffic the [Betaflight App](https://betaflight.com/docs/wiki/app) uses:

| App function | Command | Result on hardware |
| --- | --- | --- |
| Identify | `MSP_API_VERSION` | `24 4d 3e 03 01 00 01 30` → protocol 0, **API 1.48** |
| | `MSP_FC_VARIANT` | `BTFL` |
| | `MSP_FC_VERSION` | `26 06 00` + `"2026.6.0"` |
| | `MSP_BOARD_INFO` | `ESPF` + `"ESP32P4"` |
| Sensors tab | `MSP_RAW_IMU` | `acc=(0,0,2048)` = 1 g on Z, gyro 0 at rest |
| Motors tab | `MSP_SET_MOTOR` | `ch0..3 pulse 1000 -> 1200 us` on GPIO 6/20/21/22, back to 1000 on stop |
| Setup tab | `MSP_STATUS` | flags `0x271e` |

The motor path is the full stack: MSP → `Espfc` → `Actuator` →
`Hal::EscDriver` → RMT TX → GPIO.

### Known gaps on this target

- **No `MSP_CLI`** (cmd 217) in esp-fc: the App's CLI tab answers `$M!`.
  Configuration is done through the settings tabs, not the CLI.
- **No RC input configured**: `FEATURE_MASK` is `FEATURE_GPS` only, so the board
  cannot arm or fly without a receiver added to `FEATURE_RX_SERIAL` on a free
  UART.
- **DShot not implemented** on the P4 ESC driver (PWM 400 Hz only).
- **FFT is a stub** in `Hal::Dsp`: the dynamic notch filter is inert.
- ESCs were not powered during the motor test; the pulses were verified at the
  pin.

## End-to-end test against the App protocol

`tools/e2e_app_test.py` exercises every App-facing feature with the exact
framings the Betaflight App uses (MSP v1 `$M<`, MSP v2 `$X<` with CRC-8 DVB-S2
poly 0xD5, CLI sessions with STX/ETX):

```
PASS  handshake MSP_API_VERSION  [protocol=0 api=1.48]
PASS  handshake MSP_FC_VARIANT  [BTFL]
PASS  handshake MSP_BOARD_INFO  [4553504600000000]
PASS  sensors MSP_RAW_IMU  [|a|=1.00 g]
PASS  motors MSP_SET_MOTOR 1200  [(1200, 1200, 1200, 1200)]
PASS  motors stop
PASS  joystick MSP_SET_RAW_RC  [roll=1697]
PASS  waypoints insert + list  [2 waypoints]
PASS  waypoints clear
PASS  video start  [01]
PASS  video status while recording  [frames=17]
PASS  video stop + list clips  [... e2e_clip.mjpeg 1375248]

12/12 checks passed
```

That run also caught a real bug: a second recording produced 0 frames, because
`STREAMOFF` releases the V4L2 buffers and they were never re-queued. `video_rec_stop`
now re-queues them, and the test passes on repeat runs.

## WiFi (ESP32-C6 over SDIO) — and a pin conflict with the camera

`components/wifi_bridge` joins the AP through the on-board C6 (esp-hosted /
esp_wifi_remote) and opens a TCP listener for the App's TCP transport:

```
I eh_sdio: transport[host]: SDIO 4-bit 40000 kHz CLK=18 CMD=19 D0=14 D1=15 D2=16 D3=17 RESET=54
I eh_init_evt: slave chip id: 0x0d (esp32c6)
I wifi_bridge: ip 10.163.184.113, MSP over TCP on port 5760
```

The App's Android build already has `BetaflightTcpPlugin`, so this is the
transport for a cable-free connection.

### GPIO15 is shared

The SDIO log shows **D1 = GPIO15**, and the camera is driven with its XCLK on
**GPIO15** as well (`video_rec.c`, `CAM_XCLK_PIN`). Measured on hardware:

| Test | Result |
| --- | --- |
| camera with XCLK on GPIO15, WiFi off | captures frames |
| camera with **no** XCLK at all | sensor detected (PID 0x5647) but **never streams** |
| WiFi up, then start recording | task watchdog fires (`MCAUSE 0xdeadc0de`) |

So the camera genuinely needs that pin, and the C6 SDIO link occupies it. WiFi
and video recording cannot run in the same firmware image until the board's
camera XCLK pin is confirmed from the schematic (the Olimex reference uses
GPIO40; this port uses 15). Until then: pick one at build time.

### Build notes

Adding WiFi + camera outgrew the default partition table and flash setting:

- `partitions.csv` with a 4 MB `factory` app
- `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`
- `CONFIG_PARTITION_TABLE_CUSTOM=y`

### Resolution: no XCLK from the P4

Removing the P4-generated XCLK fixes the sharing: GPIO15 belongs to the C6 SDIO
link, and the OV5647 free-runs from the board. Measured with both active:

```
I wifi_bridge: ip 10.163.184.113, MSP over TCP on port 5760
I video: camera ready (800x800 RGB565)
I video: recording to /sdcard/both2.mjpeg
I video: stopped: 16 frames, 1215255 bytes
```

So WiFi, the TCP transport and SD recording coexist in one image. The earlier
"camera needs GPIO15" conclusion came from a standalone camera project with no
C6 running; with SDIO alive the camera streams without any P4 clock.

### MSP over WiFi (App TCP transport)

`components/msp_tcp` feeds the socket bytes into the flight controller's own
`MspProcessor` (a new `Espfc::model()` accessor shares the Model with the serial
MSP port) and writes the responses back. The CLI path is wired too, so waypoints
work over the same socket.

Verified from the laptop, no USB attached:

```
API_VERSION over WiFi: 000130          (protocol 0, API 1.48)
FC_VARIANT  over WiFi: BTFL
BOARD_INFO  over WiFi: 4553504600000000 (ESPF)
RAW_IMU     over WiFi: (0, 0, 2048)     (1 g on Z)
MOTOR 1300  over WiFi: readback (1200,1200,1200,1200)
```

The socket is owned by the MSP task: `wifi_bridge` only accepts and waits for
`wifi_bridge_release_client()`; its earlier echo loop was swallowing the frames.
