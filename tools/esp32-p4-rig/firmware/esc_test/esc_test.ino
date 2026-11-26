// ESC bench test with persistent logging.
//
// Drives up to 4 ESCs with standard PWM and runs them at low throttle.
// Every event is logged to NVS, so a run performed on battery power (no USB,
// no serial monitor) can be read back the next time you power up with USB.
//
//   Safety: props off. Nothing spins until AUTO_RUN fires or you press a key.
//
// ESC wiring per channel: signal -> GPIO below, plus a common GND between the
// ESC signal ground and the ESP32 GND. Never power the ESCs from the board.
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <stdarg.h>

// ------------------------- configure here -------------------------
constexpr int ESC_PINS[] = {6, 20, 21, 22};  // one free GPIO per ESC
constexpr size_t ESC_COUNT = 4;
constexpr uint32_t PWM_HZ = 400;             // 50-500 Hz is accepted by most ESCs
constexpr uint8_t PWM_BITS = 14;
constexpr uint16_t PULSE_MIN_US = 1000;      // stop
constexpr uint16_t PULSE_MAX_US = 2000;      // full throttle
constexpr float TEST_PERCENT = 15.0f;        // 20 % of the 1000..2000 us band

constexpr uint32_t ARM_MS = 3000;            // ESCs arm while held at 1000 us
constexpr uint32_t RAMP_MS = 500;            // soft ramp up / down
constexpr uint32_t HOLD_MS = 2000;           // 'g' and '1'..'4' hold time
constexpr uint32_t STEP_MS = 1500;           // per ESC in the sequential sweep

constexpr bool AUTO_RUN = true;              // sweep 1->4 automatically at power-up
constexpr uint32_t AUTO_DELAY_MS = 5000;     // time to cancel with any key
// ------------------------------------------------------------------

constexpr uint32_t PERIOD_US = 1000000UL / PWM_HZ;
constexpr uint32_t DUTY_STEPS = (1UL << PWM_BITS);
constexpr uint16_t TEST_US =
    PULSE_MIN_US + uint16_t((PULSE_MAX_US - PULSE_MIN_US) * TEST_PERCENT / 100.0f);
constexpr size_t LOG_MAX = 1600;

Preferences prefs;
String g_log;

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
    setAll(fromUs + (int32_t(toUs) - int32_t(fromUs)) * int32_t(elapsed) /
                       int32_t(ms));
    delay(5);
  }
  setAll(toUs);
}

// Ramps a single channel; the other ESCs are left wherever they are.
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

// Prints to serial and appends to the NVS-bound log buffer.
void logEvent(const char *fmt, ...) {
  char msg[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);

  char line[192];
  snprintf(line, sizeof(line), "[%8lu] %s", (unsigned long)millis(), msg);
  Serial.println(line);

  if (g_log.length() + strlen(line) + 2 < LOG_MAX) {
    g_log += line;
    g_log += '\n';
  }
}

void persistLog() { prefs.putString("log", g_log); }

const char *resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deepsleep";
    default: return "unknown";
  }
}

void runAll() {
  logEvent("ALL: arm %u ESCs at %u us for %lu ms", unsigned(ESC_COUNT),
           unsigned(PULSE_MIN_US), (unsigned long)ARM_MS);
  setAll(PULSE_MIN_US);
  delay(ARM_MS);

  logEvent("ALL: ramp to %u us (%.0f%%)", unsigned(TEST_US), double(TEST_PERCENT));
  rampAll(PULSE_MIN_US, TEST_US, RAMP_MS);
  delay(HOLD_MS);
  rampAll(TEST_US, PULSE_MIN_US, RAMP_MS);
  logEvent("ALL: done, everything at stop");
  persistLog();
}

void runOne(size_t index) {
  logEvent("ONE: ESC %u on GPIO %d, arm at %u us", unsigned(index + 1),
           ESC_PINS[index], unsigned(PULSE_MIN_US));
  setAll(PULSE_MIN_US);
  delay(ARM_MS);

  rampOne(index, PULSE_MIN_US, TEST_US, RAMP_MS);
  delay(HOLD_MS);
  rampOne(index, TEST_US, PULSE_MIN_US, RAMP_MS);
  logEvent("ONE: ESC %u done", unsigned(index + 1));
  persistLog();
}

// Walks ESC 1 -> 4, one at a time. This is the mode to use on battery power.
void sequence() {
  logEvent("SEQ: begin, arm %lu ms", (unsigned long)ARM_MS);
  setAll(PULSE_MIN_US);
  delay(ARM_MS);

  for (size_t i = 0; i < ESC_COUNT; ++i) {
    logEvent("SEQ: ESC %u (GPIO %d) -> %u us for %lu ms", unsigned(i + 1),
             ESC_PINS[i], unsigned(TEST_US), (unsigned long)STEP_MS);
    rampOne(i, PULSE_MIN_US, TEST_US, RAMP_MS);
    delay(STEP_MS);
    rampOne(i, TEST_US, PULSE_MIN_US, RAMP_MS);
    logEvent("SEQ: ESC %u stop", unsigned(i + 1));
    persistLog();  // survive a power cut mid-sequence
  }
  logEvent("SEQ: complete, all at stop");
  persistLog();
}

void setup() {
  Serial.begin(115200);
  delay(1200);

  for (size_t i = 0; i < ESC_COUNT; ++i) {
    ledcAttach(ESC_PINS[i], PWM_HZ, PWM_BITS);
    ledcWrite(ESC_PINS[i], dutyFor(PULSE_MIN_US));
  }

  prefs.begin("esctest", false);

  Serial.println("\n=== ESC test (PWM) with persistent log ===");
  Serial.printf("ESCs: %u | %lu Hz | %u us stop, %u us full | test %.0f%% = %u us\n",
                unsigned(ESC_COUNT), (unsigned long)PWM_HZ,
                unsigned(PULSE_MIN_US), unsigned(PULSE_MAX_US),
                double(TEST_PERCENT), unsigned(TEST_US));
  for (size_t i = 0; i < ESC_COUNT; ++i) {
    Serial.printf("  ESC %u -> GPIO %d\n", unsigned(i + 1), ESC_PINS[i]);
  }

  const String previous = prefs.getString("log", "");
  if (previous.length()) {
    Serial.println("--- log from the previous run ---");
    Serial.print(previous);
    Serial.println("--- end of previous log ---");
    prefs.putString("log", "");
  } else {
    Serial.println("(no previous log stored)");
  }

  g_log = "";
  logEvent("BOOT: reset reason=%s", resetReason());
  persistLog();

  Serial.println("All ESCs at STOP. Remove propellers.");
  Serial.println("  g = all four, t = sequential 1-4, 1-4 = single, s = stop,");
  Serial.println("  l = show stored log, c = clear stored log");

  if (AUTO_RUN) {
    Serial.printf("AUTO-RUN in %lu ms: send 'x' to cancel.\n",
                  (unsigned long)AUTO_DELAY_MS);
    // Boot noise on the UART is not a keypress: drop it before listening.
    delay(150);
    while (Serial.available()) Serial.read();

    bool cancel = false;
    const uint32_t start = millis();
    while (millis() - start < AUTO_DELAY_MS) {
      while (Serial.available()) {
        const char c = char(Serial.read());
        if (c == 'x' || c == 'X' || c == 's' || c == 'S') cancel = true;
      }
      if (cancel) break;
      delay(20);
    }
    if (cancel) {
      Serial.println("AUTO-RUN cancelled.");
      return;
    }
    sequence();
  }
}

void loop() {
  if (!Serial.available()) {
    delay(10);
    return;
  }

  const char c = char(Serial.read());
  if (c == 'g' || c == 'G') {
    runAll();
  } else if (c == 't' || c == 'T') {
    sequence();
  } else if (c >= '1' && c <= '4') {
    runOne(size_t(c - '1'));
  } else if (c == 's' || c == 'S') {
    setAll(PULSE_MIN_US);
    logEvent("STOP: all ESCs at %u us", unsigned(PULSE_MIN_US));
    persistLog();
  } else if (c == 'l' || c == 'L') {
    const String stored = prefs.getString("log", "");
    Serial.println("--- stored log ---");
    Serial.print(stored);
    Serial.println("--- end ---");
  } else if (c == 'c' || c == 'C') {
    prefs.putString("log", "");
    g_log = "";
    Serial.println("Stored log cleared.");
  }
}
