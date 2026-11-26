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

## Verified end to end

```
I orion_cam: SD mounted: 512001 MB
I orion_cam: capture: 800x800 RGB565
I orion_cam: verify: 2779863 bytes, 45 JPEG SOI markers
```

45 frames, 2.78 MB, every frame a real JPEG (SOI marker per frame).

Two things were required beyond the obvious:

1. **SD power is an on-chip LDO, not just the GPIO.** `BOARD_SDMMC_POWER_CHANNEL`
   is 4, i.e. `LDO_VO4`. Driving GPIO45 low is only the load switch; without
   `sd_pwr_ctrl_new_on_chip_ldo({.ldo_chan_id = 4})` the card never answers and
   init times out with `0x107`.
2. **The JPEG output buffer must come from `jpeg_alloc_encoder_mem()`** — a
   plain `heap_caps_malloc` fails with "bit stream is not aligned".

Throughput at 800x800 RGB565 / q70 is ~3 fps on the bench; recording holds the
camera, so a lower resolution or quality would raise it.
