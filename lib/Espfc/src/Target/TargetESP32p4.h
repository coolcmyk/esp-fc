#pragma once

// Waveshare ESP32-P4-NANO as an esp-fc target.
// Pins verified on the bench rig (tools/esp32-p4-rig).
//
// avoid: strapping (0,1), console UART0 (37,38 - used for MSP),
//        USB OTG (24-27), camera/XCLK (15)

// No PPM input on this board; the constant keeps the shared pin tables
// compiling and disables PPM by the -1 pin check.
#define ESPFC_INPUT
#define ESPFC_INPUT_PIN -1

#define ESPFC_OUTPUT_COUNT 4
#define ESPFC_OUTPUT_0 6
#define ESPFC_OUTPUT_1 20
#define ESPFC_OUTPUT_2 21
#define ESPFC_OUTPUT_3 22

// UART0 goes to the on-board CH343 USB bridge: this is the MSP/App port.
#define ESPFC_SERIAL_0
#define ESPFC_SERIAL_0_TX 37
#define ESPFC_SERIAL_0_RX 38
#define ESPFC_SERIAL_0_FN (SERIAL_FUNCTION_MSP)
#define ESPFC_SERIAL_0_BAUD (SERIAL_SPEED_115200)
#define ESPFC_SERIAL_0_BBAUD (SERIAL_SPEED_NONE)

// UART1: FlyFishRC M10 GPS (NMEA 115200), RX only
#define ESPFC_SERIAL_1
#define ESPFC_SERIAL_1_TX -1
#define ESPFC_SERIAL_1_RX 5
#define ESPFC_SERIAL_1_FN (SERIAL_FUNCTION_GPS)
#define ESPFC_SERIAL_1_BAUD (SERIAL_SPEED_115200)
#define ESPFC_SERIAL_1_BBAUD (SERIAL_SPEED_NONE)

#define ESPFC_SERIAL_REMAP_PINS
#define ESPFC_SERIAL_DEBUG_PORT SERIAL_UART_0

// GY-91 on the shared I2C bus
#define ESPFC_I2C_0
#define ESPFC_I2C_0_SCL 8
#define ESPFC_I2C_0_SDA 7

#define ESPFC_BUZZER_PIN -1
#define ESPFC_BUTTON_PIN -1
#define ESPFC_LED_PIN -1

#define ESPFC_GYRO_I2C_RATE_MAX 2000
#define ESPFC_GYRO_SPI_RATE_MAX 4000

#define ESPFC_FEATURE_MASK (FEATURE_GPS)
