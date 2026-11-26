# ESP32-P4 integration rig

Bench + bring-up tooling for an **ESP32-P4-NANO** (Waveshare) carrying:

- a **GY-91** (MPU6500 + BMP280) on I2C
- a **FlyFishRC M10 Mini** u-blox GPS on UART
- **4 ESCs** on PWM
- a **MIPI-CSI camera** (OV5647) on the camera connector
- a **microSD** card in the TF slot

This lives in `tools/` because it is a bench rig, not the flight firmware. esp-fc
targets ESP32/S3; the P4 is not a build target yet — see
[the port document](../../docs/esp32p4-port.md).

---

## 1. Wiring

| Device | Signal | ESP32-P4 pin | Notes |
| --- | --- | --- | --- |
| GPS M10 Mini | `T` (TXD) | **GPIO5** | the only data wire; `R` stays unconnected |
| | `V` (VCC) | **5V** | 3.3–5 V allowed, 5 V preferred |
| | `G` (GND) | **GND** | common ground, mandatory |
| GY-91 | SDA | **GPIO7** | shared I2C bus |
| | SCL | **GPIO8** | |
| | VCC | **3V3** | not 5 V |
| | GND | **GND** | |
| ESC 1-4 | signal | **GPIO6, 20, 21, 22** | + signal GND each |
| Camera | MIPI-CSI | on-board connector | OV5647, SCCB at 0x36 on I2C 7/8 |
| microSD | TF slot | SDMMC slot 0 | 4-bit, power pin GPIO45 |
| Console | USB-C | CH343 → UART0 | 115200 |

I2C devices expected on 7/8: `0x18` (on-board codec), `0x36` (camera SCCB),
`0x68` (MPU6500), `0x76` (BMP280).

## 2. Firmware inventory

| Sketch | What it proves |
| --- | --- |
| `firmware/sys_check` | Everything live at once: accel/gyro, pressure, GPS status, ESC test, I2C scan |
| `firmware/esc_test` | ESC signal path with an NVS log that survives a battery-only run |
| `firmware/gps_gy91` | GPS + GY-91, auto-baud over six rates |
| `firmware/cam_test` | SCCB probe, ESP-Video MIPI-CSI init, frame capture |
| `firmware/sd_test` | SDMMC mount, root listing, write/read verification |

## 3. Building and flashing

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install TinyGPSPlus "Adafruit BMP280 Library"

FQBN="esp32:esp32:esp32p4:ChipVariant=prev3,CDCOnBoot=default"
arduino-cli compile --fqbn "$FQBN" firmware/sys_check
arduino-cli upload  -p /dev/ttyACM0 --fqbn "$FQBN" firmware/sys_check
```

**Two board options are mandatory on this hardware**, both learned the hard way:

| Option | Why |
| --- | --- |
| `ChipVariant=prev3` | the silicon is revision **v1.3**. Built for the default (v3.00+) it boot-loops with `Guru Meditation ... Illegal instruction` at `0x4ffab2c0` |
| `CDCOnBoot=default` | puts `Serial` on UART0 (the USB-C console). With the default the console is completely silent because `Serial` goes to native USB CDC |

The camera sketch additionally needs PSRAM, since a frame does not fit in the
P4's 512 KB of internal SRAM:

```bash
arduino-cli compile --fqbn "$FQBN,PSRAM=enabled" firmware/cam_test
```

## 4. Monitor

`monitor.py` streams output, forwards keypresses immediately and reconnects by
itself when the board drops off USB (frequent when motors start).

```bash
./monitor.py                 # /dev/ttyACM0 @ 115200
./monitor.py -t              # timestamps
./monitor.py -l run.log      # append to a file
```

| Key | Action |
| --- | --- |
| `g` | all ESCs (needs two presses within 800 ms) |
| `t` | sequence ESC 1→4 (two presses) |
| `1`-`4` | single ESC (two presses) |
| `s` | stop everything |
| `p` | pause status printing |
| `i` | I2C scan and re-detect devices |
| `b` | next GPS baud |

**Motion commands require a double press.** A stray byte on the console UART was
once interpreted as `g` and spun all four motors; single-press motion is no
longer accepted.

## 5. ESC signal

400 Hz PWM, 1000 µs = stop, 2000 µs = full. Measured on the bench ESCs:

| Pulse | Result |
| --- | --- |
| 1050–1100 µs | no rotation (below the ESC start threshold) |
| 1200 µs | some motors buzz, some turn |
| 1250 µs | turns |
| 1300 µs | spins hard |

`TEST_PERCENT` defaults to 15 % (1150 µs) for gentle checks. If motors only buzz,
the ESC throttle range needs calibrating: hold 2000 µs while powering the ESC,
wait for the beep sequence, then drop to 1000 µs.

## 6. Verified status on this hardware

| Subsystem | Status | Evidence |
| --- | --- | --- |
| Console / upload | ✅ | esptool hash verification, stable `/dev/ttyACM0` |
| I2C bus | ✅ | scan returns `0x18 0x36 0x68 0x76` |
| MPU6500 | ✅ | `id=0x70`, live accel/gyro |
| BMP280 | ✅ | `0x76`, ~32 °C, ~1008 hPa |
| GPS | ✅ data | clean NMEA at **115200**, `failed=0`, ~6.9 kB/s |
| GPS fix | ⏳ | needs sky view; indoors only "data but no fix" |
| ESC PWM | ✅ | all four channels, logged 3 s bursts per channel |
| microSD | ✅ | mounts 4-bit, 512 GB card, write/read verified |
| Camera | ❌ blocked | OV5647 answers at 0x36 (`PID=0x5647`) but capture fails, see below |

## 7. Known problems

### Camera (blocked by the prebuilt libraries)

```
E dw-gdma: dw_gdma_channel_register_event_callbacks: user context not in internal RAM
E CSI: failed to register dwgdma callback
E csi_video: failed to start CSI ctlr
```

With `PSRAM=enabled` the CSI controller fails to register its GDMA callback
because the callback context is not in internal RAM. Without PSRAM the same
sketch fails earlier with `esp_video_buffer: Failed to malloc for video buffer`,
because a frame cannot fit in internal SRAM. So the prebuilt ESP-Video stack
needs both, and this build satisfies neither.

Tried and did **not** help: forcing the `ESPVideoClass`/`ESPVideoCaptureDevClass`
objects into internal RAM with `DRAM_ATTR`. The offending context is allocated
inside the ESP-Video CSI driver, not in the sketch, so it cannot be influenced
from application code.

The sensor itself is fine and identified. Options, in order of effort:

1. Build `esp_video` from source with the CSI context allocated in internal RAM
   (`heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`).
2. Use an ESP-IDF project instead of the Arduino core, where the same fix is a
   sdkconfig/menuconfig change and the OV5647 example exists.
3. Accept the camera is not usable from Arduino on this board for now.

### GPS baud

The module runs at **115200** (u-blox Betaflight default). The sketch cycles
through 115200/9600/38400/57600/19200/4800 and locks on the first valid
sentence. Note the counters (`bytes`, `NMEA OK`) are cumulative — a line saying
"9600 baud" does not mean the data was received at 9600.

### USB link is marginal

Repeated `Invalid head of packet (0x80)` upload failures and dropped characters
in the monitor indicate the USB-C cable/connector is marginal. Swap the cable
before trusting long captures.

## 8. Why this is not part of the flight firmware

See [docs/esp32p4-port.md](../../docs/esp32p4-port.md): no PlatformIO board
definition for the P4, `EscDriverEsp32.cpp` uses the legacy private RMT API
(`hal/rmt_ll.h`, `RMTMEM`) instead of `driver/rmt_tx`, and `HwTimer.cpp` uses the
legacy `driver/timer.h` group API. The rig exists to verify each piece of
hardware before any of that porting starts.
