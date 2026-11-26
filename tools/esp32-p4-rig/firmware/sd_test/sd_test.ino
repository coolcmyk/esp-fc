// SD card test for the ESP32-P4-NANO TF slot (SDMMC slot 0, 4-bit).
#include <Arduino.h>
#include <SD_MMC.h>
#include <FS.h>

void listRoot(int limit = 10) {
  File root = SD_MMC.open("/");
  if (!root) {
    Serial.println("cannot open root");
    return;
  }
  int shown = 0;
  for (File f = root.openNextFile(); f && shown < limit; f = root.openNextFile()) {
    Serial.printf("  %-24s %8lu%s\n", f.name(), (unsigned long)f.size(),
                  f.isDirectory() ? "/" : "");
    ++shown;
    f.close();
  }
  if (!shown) Serial.println("  (empty)");
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n=== SD card test (SDMMC slot 0) ===");

  bool ok = SD_MMC.begin("/sdcard", false);  // 4-bit
  if (!ok) {
    Serial.println("4-bit mount failed, retrying 1-bit");
    ok = SD_MMC.begin("/sdcard", true);
  }
  if (!ok) {
    Serial.println("mount failed: no card, or the slot is not powered");
    return;
  }

  const uint8_t type = SD_MMC.cardType();
  const char *name = "unknown";
  switch (type) {
    case CARD_MMC: name = "MMC"; break;
    case CARD_SD: name = "SDSC"; break;
    case CARD_SDHC: name = "SDHC"; break;
    default: break;
  }
  Serial.printf("mounted: %s, %llu MB, %u-bit bus\n", name,
                SD_MMC.cardSize() / (1024ULL * 1024ULL), SD_MMC.numSectors() ? 4 : 1);

  Serial.println("root:");
  listRoot();

  const char *path = "/esp32p4_test.txt";
  File out = SD_MMC.open(path, FILE_WRITE);
  if (!out) {
    Serial.println("cannot write test file");
    return;
  }
  out.println("hello from esp32-p4-nano");
  out.close();

  File in = SD_MMC.open(path, FILE_READ);
  if (!in) {
    Serial.println("cannot read back test file");
    return;
  }
  Serial.printf("read back: %s", in.readString().c_str());
  in.close();
  Serial.println("(write/read OK)");
}

void loop() { delay(1000); }
