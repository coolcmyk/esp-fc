# ESP32-P4 bench rig

Bench tooling for an **ESP32-P4** board carrying a GY-91 IMU/baro and a u-blox
M10 GPS, plus four ESCs. This lives in `tools/` because it is a *test rig*, not
part of the flight firmware: esp-fc targets ESP32/S3, and there is no P4 target
yet (see [docs/esp32p4-port.md](../../docs/esp32p4-port.md)).

## Hardware

| Device | Signal | ESP32-P4 pin |
| --- | --- | --- |
| GPS (M10 Mini) | TXD `T` → board RX | **GPIO5** |
| | RXD `R` | *unconnected* |
| | VCC `V` | **5V** |
| | GND `G` | **GND** |
| GY-91 (MPU6500 + BMP280) | SDA | **GPIO7** |
| | SCL | **GPIO8** |
| | VCC | **3V3** |
| | GND | **GND** |
| ESC 1-4 | signal | **GPIO6, 20, 21, 22** |
| | GND | common with the P4 |

All grounds must be common. Power ESCs from the battery, never from the board.

## Firmware

Three Arduino sketches, all built for the P4 with the same board options:

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install TinyGPSPlus "Adafruit BMP280 Library"

FQBN="esp32:esp32:esp32p4:ChipVariant=prev3,CDCOnBoot=default"
arduino-cli compile --fqbn "$FQBN" firmware/sys_check
arduino-cli upload  -p /dev/ttyACM0 --fqbn "$FQBN" firmware/sys_check
```

| Sketch | Purpose |
| --- | --- |
| `firmware/sys_check` | Everything at once: GY-91 accel/gyro, BMP280, GPS status/fix, ESC test keys |
| `firmware/esc_test` | ESC-only bench test with a persistent log in NVS (survives a battery run) |
| `firmware/gps_gy91` | GPS + GY-91 only, auto-baud over 115200/9600/38400/57600/19200/4800 |

The two required board options, in case you build elsewhere:

- `ChipVariant=prev3` — silicon revision **v1.3**. Without it the image
  boot-loops with `Illegal instruction`.
- `CDCOnBoot=default` — puts `Serial` on UART0 (the USB-C console). With the
  default, `Serial` goes to native USB CDC and the console is silent.

## Monitor

`monitor.py` is an interactive console: it streams output and forwards keys
immediately, and reconnects by itself when the board drops off USB.

```bash
./monitor.py            # /dev/ttyACM0 @ 115200
./monitor.py -t         # timestamps
./monitor.py -l run.log # also append to a file
```

Keys (`sys_check` / `esc_test`): `g` all ESCs · `t` sequence 1-4 ·
`1`-`4` single ESC · `s` stop · `p` pause status · `b` next GPS baud.
`gps_gy91`: `r` raw NMEA · `b` next baud.

## ESC signal

400 Hz PWM, 1000 us = stop, 2000 us = full. Measured startup threshold on the
bench ESCs was between 1100 us (no rotation) and 1300 us (full spin), so
`TEST_PERCENT` is set to 15 % (1150 us) for gentle checks. If the motors only
buzz, the ESC throttle range needs calibrating (hold 2000 us while powering the
ESC, then drop to 1000 us).
