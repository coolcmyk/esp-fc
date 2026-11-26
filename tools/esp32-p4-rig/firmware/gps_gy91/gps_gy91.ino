#include <Arduino.h>
#include <Wire.h>
#include <TinyGPS++.h>
#include <Adafruit_BMP280.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <math.h>

// GPS T -> GPIO5. GPS R optional and can be left disconnected.
// GY-91 SDA -> GPIO7. GY-91 SCL -> GPIO8. All grounds are common.
constexpr int GPS_RX = 5;
constexpr int I2C_SDA = 7;
constexpr int I2C_SCL = 8;
constexpr uint32_t GPS_BAUDS[] = {115200, 9600, 38400, 57600, 19200, 4800};
constexpr size_t BAUD_COUNT = sizeof(GPS_BAUDS) / sizeof(GPS_BAUDS[0]);

HardwareSerial gpsUart(1);
TinyGPSPlus gps;
Adafruit_BMP280 bmp(&Wire);

struct GpsSample {
  uint32_t baud, bytes, nmea, failed, satellites;
  bool locked, receiving, fix, satsValid, hdopValid, altitudeValid, raw;
  double lat, lon, hdop, altitude;
};

struct ImuSample {
  uint8_t imuAddress, imuId, bmpAddress;
  bool motionValid, magnetometerFound, magValid, bmpValid;
  float accel[3], gyro[3], mag[3], temperature, pressure;
};

struct RawChunk {
  uint8_t length;
  uint8_t data[96];
};

QueueHandle_t gpsQueue, imuQueue, commandQueue, rawQueue;

TickType_t ticks(uint32_t ms) {
  TickType_t count = pdMS_TO_TICKS(ms);
  return count ? count : 1;
}

bool writeReg(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t address, uint8_t reg, uint8_t *data, uint8_t count) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom(address, count) != count) {
    while (Wire.available()) Wire.read();
    return false;
  }

  for (uint8_t i = 0; i < count; ++i) data[i] = Wire.read();
  return true;
}

int16_t readBigEndian(const uint8_t *data) {
  return static_cast<int16_t>((uint16_t(data[0]) << 8) | data[1]);
}

int16_t readLittleEndian(const uint8_t *data) {
  return static_cast<int16_t>((uint16_t(data[1]) << 8) | data[0]);
}

void gpsTask(void *) {
  GpsSample sample{};
  size_t baudIndex = 0;
  uint32_t lastByte = 0;
  uint32_t lastNmea = 0;
  uint32_t baudStart = millis();

  gpsUart.setRxBufferSize(4096);
  gpsUart.begin(GPS_BAUDS[baudIndex], SERIAL_8N1, GPS_RX, -1);

  for (;;) {
    bool nextBaud = false;
    char command;
    while (xQueueReceive(commandQueue, &command, 0) == pdTRUE) {
      if (command == 'r') sample.raw = !sample.raw;
      if (command == 'b') nextBaud = true;
    }

    RawChunk chunk{};
    const uint32_t oldNmea = gps.passedChecksum();

    for (unsigned i = 0; i < 2048; ++i) {
      int value = gpsUart.read();
      if (value < 0) break;

      ++sample.bytes;
      lastByte = millis();
      gps.encode(static_cast<char>(value));

      if (sample.raw &&
          ((value >= 32 && value <= 126) || value == '\r' || value == '\n')) {
        chunk.data[chunk.length++] = static_cast<uint8_t>(value);
        if (chunk.length == sizeof(chunk.data)) {
          xQueueSend(rawQueue, &chunk, 0);
          chunk.length = 0;
        }
      }
    }
    if (chunk.length) xQueueSend(rawQueue, &chunk, 0);

    if (gps.passedChecksum() != oldNmea) {
      sample.locked = true;
      lastNmea = millis();
    }

    const uint32_t now = millis();
    if (nextBaud || (!sample.locked && now - baudStart >= 5000)) {
      baudIndex = (baudIndex + 1) % BAUD_COUNT;
      gpsUart.end();
      gpsUart.begin(GPS_BAUDS[baudIndex], SERIAL_8N1, GPS_RX, -1);
      sample.locked = false;
      baudStart = millis();
    }

    sample.baud = GPS_BAUDS[baudIndex];
    sample.nmea = gps.passedChecksum();
    sample.failed = gps.failedChecksum();
    sample.receiving = sample.bytes > 0 && now - lastByte < 5000;

    const bool freshNmea = sample.locked && now - lastNmea < 5000;
    sample.fix = freshNmea && gps.location.isValid() && gps.location.age() < 3000;
    sample.satsValid = freshNmea && gps.satellites.isValid() &&
                       gps.satellites.age() < 5000;
    sample.hdopValid = freshNmea && gps.hdop.isValid() && gps.hdop.age() < 5000;
    sample.altitudeValid = sample.fix && gps.altitude.isValid() &&
                           gps.altitude.age() < 3000;

    if (sample.fix) {
      sample.lat = gps.location.lat();
      sample.lon = gps.location.lng();
    }
    if (sample.satsValid) sample.satellites = gps.satellites.value();
    if (sample.hdopValid) sample.hdop = gps.hdop.hdop();
    if (sample.altitudeValid) sample.altitude = gps.altitude.meters();

    xQueueOverwrite(gpsQueue, &sample);
    vTaskDelay(ticks(2));
  }
}

void imuTask(void *) {
  Wire.begin(I2C_SDA, I2C_SCL, 100000);
  Wire.setTimeOut(50);

  ImuSample sample{};
  const uint8_t imuAddresses[] = {0x68, 0x69};
  const uint8_t bmpAddresses[] = {0x76, 0x77};

  for (uint8_t address : imuAddresses) {
    uint8_t id;
    if (!readRegs(address, 0x75, &id, 1)) continue;
    sample.imuId = id;
    if (id != 0x71 && id != 0x73 && id != 0x70) continue;

    if (!writeReg(address, 0x6B, 0x80)) continue;
    vTaskDelay(ticks(100));
    if (!writeReg(address, 0x6B, 0x01)) continue;
    vTaskDelay(ticks(20));

    if (!writeReg(address, 0x1A, 0x03) || // Gyro low-pass filter
        !writeReg(address, 0x19, 0x09) || // 100 Hz internal sample rate
        !writeReg(address, 0x1B, 0x00) || // Gyro +/-250 deg/s
        !writeReg(address, 0x1C, 0x00)) { // Accelerometer +/-2 g
      continue;
    }

    sample.imuAddress = address;
    break;
  }

  constexpr uint8_t MAG = 0x0C;
  float magScale[3] = {0.15f, 0.15f, 0.15f};
  uint8_t magId;

  if (sample.imuAddress &&
      writeReg(sample.imuAddress, 0x6A, 0x00) &&
      writeReg(sample.imuAddress, 0x37, 0x02) &&
      readRegs(MAG, 0x00, &magId, 1) && magId == 0x48) {
    bool ok = writeReg(MAG, 0x0A, 0x00);
    vTaskDelay(ticks(10));
    ok = writeReg(MAG, 0x0A, 0x0F) && ok;
    vTaskDelay(ticks(10));

    uint8_t adjustment[3];
    ok = readRegs(MAG, 0x10, adjustment, 3) && ok;
    if (ok) {
      for (int i = 0; i < 3; ++i) {
        magScale[i] *= 1.0f + (int(adjustment[i]) - 128) / 256.0f;
      }
    }

    ok = writeReg(MAG, 0x0A, 0x00) && ok;
    vTaskDelay(ticks(10));
    sample.magnetometerFound = writeReg(MAG, 0x0A, 0x16) && ok;
  }

  for (uint8_t address : bmpAddresses) {
    uint8_t id;
    if (readRegs(address, 0xD0, &id, 1) && id == 0x58 &&
        bmp.begin(address)) {
      sample.bmpAddress = address;
      break;
    }
  }

  uint32_t lastBmp = 0;
  uint32_t lastMag = 0;
  TickType_t wake = xTaskGetTickCount();

  for (;;) {
    uint8_t data[14];
    sample.motionValid = sample.imuAddress &&
                         readRegs(sample.imuAddress, 0x3B, data, sizeof(data));
    if (sample.motionValid) {
      for (int i = 0; i < 3; ++i) {
        sample.accel[i] = readBigEndian(data + 2 * i) / 16384.0f;
        sample.gyro[i] = readBigEndian(data + 8 + 2 * i) / 131.0f;
      }
    }

    if (sample.magnetometerFound) {
      uint8_t status;
      uint8_t mag[7]; // Include ST2, which releases the AK8963 data registers.
      if (!readRegs(MAG, 0x02, &status, 1)) {
        sample.magValid = false;
      } else if (status & 1) {
        sample.magValid = readRegs(MAG, 0x03, mag, sizeof(mag)) &&
                          !(mag[6] & 0x08);
        if (sample.magValid) {
          for (int i = 0; i < 3; ++i) {
            sample.mag[i] = readLittleEndian(mag + 2 * i) * magScale[i];
          }
          lastMag = millis();
        }
      }
      if (millis() - lastMag > 500) sample.magValid = false;
    }

    if (sample.bmpAddress && millis() - lastBmp >= 200) {
      lastBmp = millis();
      sample.temperature = bmp.readTemperature();
      sample.pressure = bmp.readPressure() / 100.0f;
      sample.bmpValid = isfinite(sample.temperature) &&
                        isfinite(sample.pressure) && sample.pressure > 0;
    }

    xQueueOverwrite(imuQueue, &sample);
    vTaskDelayUntil(&wake, ticks(20));
  }
}

void consoleTask(void *) {
  uint32_t lastPrint = 0;
  bool raw = false;

  for (;;) {
    while (Serial.available()) {
      char command = static_cast<char>(Serial.read());
      if (command == 'R') command = 'r';
      if (command == 'B') command = 'b';
      if (command == 'r' || command == 'b') {
        if (xQueueSend(commandQueue, &command, 0) != pdTRUE) {
          Serial.println("Command queue full. Try again.");
        }
      }
    }

    GpsSample g{};
    const bool haveGps = xQueuePeek(gpsQueue, &g, 0) == pdTRUE;
    if (haveGps && raw != g.raw) {
      raw = g.raw;
      Serial.println(raw ? "Raw GPS ON" : "Raw GPS OFF");
    }

    RawChunk chunk;
    for (int i = 0; i < 8 && xQueueReceive(rawQueue, &chunk, 0) == pdTRUE; ++i) {
      if (raw) Serial.write(chunk.data, chunk.length);
    }

    if (!raw && millis() - lastPrint >= 1000) {
      lastPrint = millis();
      Serial.println("\n------------------------------");

      ImuSample s{};
      if (xQueuePeek(imuQueue, &s, 0) == pdTRUE) {
        Serial.printf("GY-91: IMU addr=0x%02X ID=0x%02X | BMP addr=0x%02X\n",
                      unsigned(s.imuAddress), unsigned(s.imuId),
                      unsigned(s.bmpAddress));

        if (s.motionValid) {
          Serial.printf("ACC  [g]     X=%+.3f Y=%+.3f Z=%+.3f\n",
                        s.accel[0], s.accel[1], s.accel[2]);
          Serial.printf("GYRO [deg/s] X=%+.2f Y=%+.2f Z=%+.2f\n",
                        s.gyro[0], s.gyro[1], s.gyro[2]);
        } else {
          Serial.println("ACC/GYRO unavailable. Check I2C wiring and power.");
        }

        if (s.magValid) {
          Serial.printf("MAG  [uT]    X=%+.2f Y=%+.2f Z=%+.2f (uncalibrated)\n",
                        s.mag[0], s.mag[1], s.mag[2]);
        } else {
          Serial.println(s.magnetometerFound ? "MAG no valid sample."
                                             : "AK8963 not detected.");
        }

        if (s.bmpValid) {
          Serial.printf("BMP280       %.2f C | %.2f hPa\n",
                        s.temperature, s.pressure);
        } else {
          Serial.println("BMP280 unavailable.");
        }
      } else {
        Serial.println("Waiting for GY-91 task...");
      }

      if (haveGps) {
        Serial.printf("GPS %lu baud %s | bytes=%lu | NMEA OK=%lu | failed=%lu\n",
                      (unsigned long)g.baud, g.locked ? "LOCKED" : "SEARCHING",
                      (unsigned long)g.bytes, (unsigned long)g.nmea,
                      (unsigned long)g.failed);

        if (g.satsValid) {
          Serial.printf("Satellites used: %lu\n", (unsigned long)g.satellites);
        }
        if (g.hdopValid) Serial.printf("HDOP: %.2f\n", g.hdop);

        if (!g.receiving) {
          Serial.println("No recent UART bytes. Check GPS power, GND, T -> GPIO4.");
        } else if (g.fix) {
          Serial.printf("FIX: lat=%.7f lon=%.7f\n", g.lat, g.lon);
          if (g.altitudeValid) Serial.printf("GPS altitude: %.2f m\n", g.altitude);
        } else {
          Serial.println("No fresh position fix. Check NMEA and try outdoors.");
        }
      }
    }

    vTaskDelay(ticks(10));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("ESP32-P4 GPS + GY-91 | r = raw GPS | b = next baud");

  gpsQueue = xQueueCreate(1, sizeof(GpsSample));
  imuQueue = xQueueCreate(1, sizeof(ImuSample));
  commandQueue = xQueueCreate(8, sizeof(char));
  rawQueue = xQueueCreate(16, sizeof(RawChunk));

  if (!gpsQueue || !imuQueue || !commandQueue || !rawQueue) {
    Serial.println("Could not allocate FreeRTOS queues.");
    return;
  }

  if (xTaskCreate(gpsTask, "GPS", 4096, nullptr, 3, nullptr) != pdPASS ||
      xTaskCreate(imuTask, "GY91", 4096, nullptr, 2, nullptr) != pdPASS ||
      xTaskCreate(consoleTask, "Console", 6144, nullptr, 1, nullptr) != pdPASS) {
    Serial.println("Could not start all FreeRTOS tasks.");
  }
}

void loop() {
  vTaskDelay(ticks(1000));
}
