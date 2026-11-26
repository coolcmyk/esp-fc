// System checker: GY-91 (accel/gyro + BMP280), GPS status, and 4-channel ESC test
// in one firmware. Status prints every 2 s; ESC keys work at any time.
//
// Wiring:  GPS  T->GPIO5, V->5V, G->GND   (R unused)
//          GY-91 SDA->GPIO7, SCL->GPIO8, VCC->3V3, GND->GND
//          ESCs  signal -> GPIO6, 20, 21, 22 (+ common GND)
//
// Keys: g=all  t=sequence  1-4=single  s=stop  p=pause status  b=next GPS baud
#include <Adafruit_BMP280.h>
#include <Arduino.h>
#include <TinyGPS++.h>
#include <Wire.h>

constexpr int GPS_RX = 5;
constexpr int I2C_SDA = 7;
constexpr int I2C_SCL = 8;

constexpr uint32_t GPS_BAUDS[] = {115200, 9600, 38400, 57600, 19200, 4800};
constexpr size_t BAUD_COUNT = sizeof(GPS_BAUDS) / sizeof(GPS_BAUDS[0]);

constexpr int ESC_PINS[] = {6, 20, 21, 22};
constexpr size_t ESC_COUNT = 4;
constexpr uint32_t PWM_HZ = 400;
constexpr uint8_t PWM_BITS = 14;
constexpr uint16_t PULSE_MIN_US = 1000;
constexpr uint16_t PULSE_MAX_US = 2000;
constexpr float TEST_PERCENT = 15.0f;
constexpr uint32_t ARM_MS = 3000;
constexpr uint32_t RAMP_MS = 500;
constexpr uint32_t HOLD_MS = 2000;
constexpr uint32_t STEP_MS = 1500;
constexpr uint32_t STATUS_MS = 2000;

constexpr uint32_t PERIOD_US = 1000000UL / PWM_HZ;
constexpr uint32_t DUTY_STEPS = (1UL << PWM_BITS);
constexpr uint16_t TEST_US =
    PULSE_MIN_US + uint16_t((PULSE_MAX_US - PULSE_MIN_US) * TEST_PERCENT / 100.0f);

HardwareSerial gpsUart(1);
TinyGPSPlus gps;
Adafruit_BMP280 bmp(&Wire);

uint8_t imuAddress = 0;
uint8_t imuId = 0;
uint8_t bmpAddress = 0;

size_t baudIndex = 0;
uint32_t gpsBytes = 0;
uint32_t lastGpsByte = 0;
uint32_t baudStart = 0;

bool showStatus = true;
uint32_t lastStatus = 0;

uint32_t dutyFor(uint16_t us) {
  us = constrain(us, PULSE_MIN_US, PULSE_MAX_US);
  return uint32_t(uint64_t(us) * DUTY_STEPS / PERIOD_US);
}

void setAll(uint16_t us) {
  for (size_t i = 0; i < ESC_COUNT; ++i) ledcWrite(ESC_PINS[i], dutyFor(us));
}

void setOne(size_t index, uint16_t us) {
  if (index < ESC_COUNT) ledcWrite(ESC_PINS[index], dutyFor(us));
}

void rampAll(uint16_t fromUs, uint16_t toUs, uint32_t ms) {
  const uint32_t start = millis();
  for (;;) {
    const uint32_t elapsed = millis() - start;
    if (elapsed >= ms) break;
    setAll(fromUs + (int32_t(toUs) - int32_t(fromUs)) * int32_t(elapsed) / int32_t(ms));
    delay(5);
  }
  setAll(toUs);
}

void rampOne(size_t index, uint16_t fromUs, uint16_t toUs, uint32_t ms) {
  const uint32_t start = millis();
  for (;;) {
    const uint32_t elapsed = millis() - start;
    if (elapsed >= ms) break;
    setOne(index, fromUs + (int32_t(toUs) - int32_t(fromUs)) * int32_t(elapsed) /
                          int32_t(ms));
    delay(5);
  }
  setOne(index, toUs);
}

void runAll() {
  Serial.printf("ESC ALL: arm %lu ms then %.0f%% (%u us) for %lu ms\n",
                (unsigned long)ARM_MS, double(TEST_PERCENT), unsigned(TEST_US),
                (unsigned long)HOLD_MS);
  setAll(PULSE_MIN_US);
  delay(ARM_MS);
  rampAll(PULSE_MIN_US, TEST_US, RAMP_MS);
  delay(HOLD_MS);
  rampAll(TEST_US, PULSE_MIN_US, RAMP_MS);
  Serial.println("ESC ALL: done at stop");
}

void runOne(size_t index) {
  Serial.printf("ESC %u (GPIO %d): %.0f%% (%u us) for %lu ms\n", unsigned(index + 1),
                ESC_PINS[index], double(TEST_PERCENT), unsigned(TEST_US),
                (unsigned long)HOLD_MS);
  setAll(PULSE_MIN_US);
  delay(ARM_MS);
  rampOne(index, PULSE_MIN_US, TEST_US, RAMP_MS);
  delay(HOLD_MS);
  rampOne(index, TEST_US, PULSE_MIN_US, RAMP_MS);
  Serial.printf("ESC %u: done\n", unsigned(index + 1));
}

void sequence() {
  Serial.printf("ESC SEQ: arm %lu ms, then 1->4 at %u us\n", (unsigned long)ARM_MS,
                unsigned(TEST_US));
  setAll(PULSE_MIN_US);
  delay(ARM_MS);
  for (size_t i = 0; i < ESC_COUNT; ++i) {
    Serial.printf("ESC SEQ: ESC %u (GPIO %d)\n", unsigned(i + 1), ESC_PINS[i]);
    rampOne(i, PULSE_MIN_US, TEST_US, RAMP_MS);
    delay(STEP_MS);
    rampOne(i, TEST_US, PULSE_MIN_US, RAMP_MS);
  }
  Serial.println("ESC SEQ: complete at stop");
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

void initImu() {
  const uint8_t addresses[] = {0x68, 0x69};
  for (uint8_t address : addresses) {
    uint8_t id;
    if (!readRegs(address, 0x75, &id, 1)) continue;
    if (id != 0x70 && id != 0x71 && id != 0x73) continue;

    writeReg(address, 0x6B, 0x80);
    delay(100);
    if (!writeReg(address, 0x6B, 0x01)) continue;
    delay(20);
    if (!writeReg(address, 0x1A, 0x03) || !writeReg(address, 0x19, 0x09) ||
        !writeReg(address, 0x1B, 0x00) || !writeReg(address, 0x1C, 0x00)) {
      continue;
    }
    imuAddress = address;
    imuId = id;
    return;
  }
}

void initBmp() {
  const uint8_t addresses[] = {0x76, 0x77};
  for (uint8_t address : addresses) {
    uint8_t id;
    if (readRegs(address, 0xD0, &id, 1) && id == 0x58 && bmp.begin(address)) {
      bmpAddress = address;
      return;
    }
  }
}

void serviceGps() {
  for (unsigned i = 0; i < 512; ++i) {
    const int value = gpsUart.read();
    if (value < 0) break;
    ++gpsBytes;
    lastGpsByte = millis();
    gps.encode(char(value));
  }
}

void printStatus() {
  Serial.println("\n--------------- status ---------------");

  if (imuAddress) {
    uint8_t data[14];
    if (readRegs(imuAddress, 0x3B, data, sizeof(data))) {
      Serial.printf("GY-91 0x%02X id=0x%02X | ACC %+.2f %+.2f %+.2f g | GYRO %+.1f %+.1f %+.1f dps\n",
                    unsigned(imuAddress), unsigned(imuId),
                    readBigEndian(data) / 16384.0f, readBigEndian(data + 2) / 16384.0f,
                    readBigEndian(data + 4) / 16384.0f,
                    readBigEndian(data + 8) / 131.0f, readBigEndian(data + 10) / 131.0f,
                    readBigEndian(data + 12) / 131.0f);
    } else {
      Serial.println("GY-91: read failed (I2C)");
    }
  } else {
    Serial.println("GY-91: not found on 0x68/0x69 (check SDA=7 SCL=8, 3V3, GND)");
  }

  if (bmpAddress) {
    const float t = bmp.readTemperature();
    const float p = bmp.readPressure() / 100.0f;
    if (isfinite(t) && isfinite(p) && p > 0) {
      Serial.printf("BMP280 0x%02X | %.2f C | %.2f hPa\n", unsigned(bmpAddress),
                    double(t), double(p));
    } else {
      Serial.println("BMP280: no valid reading");
    }
  } else {
    Serial.println("BMP280: not found on 0x76/0x77");
  }

  const bool receiving = gpsBytes > 0 && millis() - lastGpsByte < 5000;
  Serial.printf("GPS %lu baud | bytes=%lu | NMEA OK=%lu failed=%lu | %s\n",
                (unsigned long)GPS_BAUDS[baudIndex], (unsigned long)gpsBytes,
                (unsigned long)gps.passedChecksum(), (unsigned long)gps.failedChecksum(),
                receiving ? "receiving" : "NO SIGNAL");
  if (receiving && gps.location.isValid() && gps.location.age() < 3000) {
    Serial.printf("GPS FIX %.7f, %.7f | %.0f m | %lu sats | HDOP %.2f\n",
                  gps.location.lat(), gps.location.lng(), gps.altitude.meters(),
                  (unsigned long)gps.satellites.value(), gps.hdop.hdop());
  } else if (receiving) {
    Serial.println("GPS: data but no fix yet (needs sky view)");
  }
  Serial.printf("ESC: %.0f%% = %u us | keys g t 1-4 s p b\n", double(TEST_PERCENT),
                unsigned(TEST_US));
}

void setup() {
  Serial.begin(115200);
  delay(1200);

  for (size_t i = 0; i < ESC_COUNT; ++i) {
    ledcAttach(ESC_PINS[i], PWM_HZ, PWM_BITS);
    ledcWrite(ESC_PINS[i], dutyFor(PULSE_MIN_US));
  }

  Wire.begin(I2C_SDA, I2C_SCL, 100000);
  Wire.setTimeOut(50);
  initImu();
  initBmp();

  gpsUart.setRxBufferSize(4096);
  gpsUart.begin(GPS_BAUDS[baudIndex], SERIAL_8N1, GPS_RX, -1);
  baudStart = millis();

  Serial.println("\n=== system check: GY-91 + GPS + 4x ESC ===");
  Serial.printf("ESCs on");
  for (size_t i = 0; i < ESC_COUNT; ++i) Serial.printf(" %d", ESC_PINS[i]);
  Serial.printf(" | %.0f%% = %u us\n", double(TEST_PERCENT), unsigned(TEST_US));
  Serial.println("All ESCs at STOP. Props off.");
}

void loop() {
  serviceGps();

  // Auto-baud while nothing valid arrives.
  if (gps.passedChecksum() == 0 && millis() - baudStart >= 5000) {
    baudIndex = (baudIndex + 1) % BAUD_COUNT;
    gpsUart.end();
    gpsUart.begin(GPS_BAUDS[baudIndex], SERIAL_8N1, GPS_RX, -1);
    baudStart = millis();
  }

  if (showStatus && millis() - lastStatus >= STATUS_MS) {
    lastStatus = millis();
    printStatus();
  }

  if (Serial.available()) {
    const char c = char(Serial.read());
    if (c == 'g' || c == 'G') {
      runAll();
    } else if (c == 't' || c == 'T') {
      sequence();
    } else if (c >= '1' && c <= '4') {
      runOne(size_t(c - '1'));
    } else if (c == 's' || c == 'S') {
      setAll(PULSE_MIN_US);
      Serial.println("STOP: all ESCs at 1000 us");
    } else if (c == 'p' || c == 'P') {
      showStatus = !showStatus;
      Serial.println(showStatus ? "status ON" : "status OFF");
    } else if (c == 'b' || c == 'B') {
      baudIndex = (baudIndex + 1) % BAUD_COUNT;
      gpsUart.end();
      gpsUart.begin(GPS_BAUDS[baudIndex], SERIAL_8N1, GPS_RX, -1);
      baudStart = millis();
      Serial.printf("GPS baud -> %lu\n", (unsigned long)GPS_BAUDS[baudIndex]);
    }
  }
}
