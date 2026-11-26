/*
 * Board map for the Waveshare ESP32-P4-NANO used as an esp-fc target.
 * Mirrors the bench rig wiring verified earlier in tools/esp32-p4-rig.
 */
#pragma once

#include "driver/gpio.h"

/* ESC outputs (DShot/PWM), see esc_pwm_rmt.c */
#define BOARD_ESC_GPIO_1 GPIO_NUM_6
#define BOARD_ESC_GPIO_2 GPIO_NUM_20
#define BOARD_ESC_GPIO_3 GPIO_NUM_21
#define BOARD_ESC_GPIO_4 GPIO_NUM_22

/* GY-91 (MPU6500 + BMP280) on the shared I2C bus */
#define BOARD_I2C_PORT 0
#define BOARD_I2C_SDA GPIO_NUM_7
#define BOARD_I2C_SCL GPIO_NUM_8

/* FlyFishRC M10 GPS, NMEA at 115200 */
#define BOARD_GPS_UART 1
#define BOARD_GPS_RX GPIO_NUM_5
#define BOARD_GPS_TX GPIO_NUM_NC
#define BOARD_GPS_BAUD 115200

/* Console: UART0 through the on-board CH343 USB bridge */
#define BOARD_CONSOLE_UART 0
#define BOARD_CONSOLE_BAUD 115200

/* microSD (SDMMC slot 0, 4-bit) */
#define BOARD_SD_POWER GPIO_NUM_45
