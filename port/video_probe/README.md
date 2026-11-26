# Video probe (ESP-IDF)

OV5647 over MIPI-CSI -> hardware JPEG encoder -> motion-JPEG file on the microSD.

Working on the Waveshare ESP32-P4-NANO with ESP-IDF 5.5:

```
I orion_cam: sensor detected, /dev/video0 ready
I p4cam:     fmt[1] RGB 5-6-5 LE (0x50424752)
I p4cam:     frame 0: index=0 seq=0 bytesused=640000
```

Required config (`sdkconfig.defaults`):

- `CONFIG_CAMERA_OV5647=y` (the driver is off by default; without it
  `esp_video_init` reports no sensor)
- `CONFIG_SPIRAM=y` with `CONFIG_SPIRAM_MODE_HEX=y`, `CONFIG_SPIRAM_SPEED_200M=y`
  (an 800x800 frame does not fit in internal SRAM)
- `CONFIG_FATFS_LFN_STACK=y` for the clip filename

This is the path that failed under the Arduino core
(`dw-gdma ... user context not in internal RAM`) and works under ESP-IDF.

## Status

Camera capture and JPEG encoding are verified. The SD mount currently fails at
card init (`sdmmc_init_ocr ... 0x107`, timeout) with 4-bit and 1-bit and with
internal pull-ups, on a board whose slot mounted a card earlier under the
Arduino `SD_MMC` driver - check that a card is inserted before suspecting the
driver.
