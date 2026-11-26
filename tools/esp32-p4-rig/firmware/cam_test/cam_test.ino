// MIPI-CSI camera test for the ESP32-P4-NANO (Waveshare, OV5647-style module).
//
// Probes the sensor over SCCB (I2C 7/8), brings up ESP-Video's MIPI-CSI
// device, then captures frames and reports format/size/fps.
#include <Arduino.h>
#include <ESP_Video.h>
#include <Wire.h>

constexpr int SCCB_PORT = 0;
constexpr int SCCB_SCL = 8;
constexpr int SCCB_SDA = 7;
constexpr int SENSOR_RESET_PIN = -1;  // not wired on the Nano camera header
constexpr int SENSOR_PWDN_PIN = -1;

constexpr size_t CAPTURE_BUFFERS = 2;
constexpr int FRAMES = 10;

ESPVideoClass video;
ESPVideoCaptureDevClass captureDev;

void probeBus() {
  Serial.print("SCCB scan (SDA=7 SCL=8):");
  int found = 0;
  for (uint8_t address = 0x08; address < 0x78; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", unsigned(address));
      ++found;
    }
  }
  Serial.println(found ? "" : " nothing");
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n=== ESP32-P4 MIPI-CSI camera test ===");

  Wire.begin(SCCB_SDA, SCCB_SCL, 100000);
  Wire.setTimeOut(50);
  probeBus();

  ESPVideoCamConfigClass camConfig;
  if (!camConfig.begin(SCCB_PORT, SCCB_SCL, SCCB_SDA, 100000,
                       SENSOR_RESET_PIN, SENSOR_PWDN_PIN)) {
    Serial.println("SCCB init failed");
    return;
  }

  ESPVideoCSIConfigClass csiConfig;
  if (!csiConfig.begin(camConfig)) {
    Serial.println("CSI config failed");
    return;
  }

  if (!video.begin(csiConfig)) {
    Serial.println("camera init failed: no sensor on the CSI connector?");
    return;
  }
  Serial.println("video device initialised");

  if (!captureDev.begin(ESP_VIDEO_MIPI_CSI_DEVICE_NAME, CAPTURE_BUFFERS)) {
    Serial.println("failed to open capture device");
    return;
  }

  const esp_video_format_t formats[] = {ESP_VIDEO_FORMAT_RGB565,
                                        ESP_VIDEO_FORMAT_YUV422_YUYV};
  bool formatSet = false;
  for (esp_video_format_t f : formats) {
    if (captureDev.setFormat(f)) {
      Serial.printf("format set: %s\n", captureDev.getFormatName());
      formatSet = true;
      break;
    }
    Serial.printf("format %u rejected\n", unsigned(f));
  }
  if (!formatSet) Serial.println("using the sensor default format");

  if (!captureDev.startCapture()) {
    Serial.println("failed to start capture");
    return;
  }
  Serial.printf("capture started, %d frames...\n", FRAMES);

  uint32_t start = millis();
  int got = 0;
  while (got < FRAMES && millis() - start < 15000) {
    ESPVideoBufferClass buffer = captureDev.captureBuffer();
    if (!buffer.valid()) {
      Serial.println("capture timeout");
      break;
    }
    ++got;
    Serial.printf("frame %d: %p %lu bytes %s %lux%lu\n", got, buffer.data(),
                  (unsigned long)buffer.size(), buffer.formatName(),
                  (unsigned long)buffer.getWidth(),
                  (unsigned long)buffer.getHeight());
  }

  const uint32_t elapsed = millis() - start;
  if (got) {
    Serial.printf("captured %d frames in %lu ms (%.1f fps)\n", got,
                  (unsigned long)elapsed, got * 1000.0f / elapsed);
  }
}

void loop() { delay(1000); }
