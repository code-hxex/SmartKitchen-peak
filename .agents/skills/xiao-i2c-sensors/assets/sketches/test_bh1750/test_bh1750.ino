/*
 * Unit test 1/3 — BH1750 ambient light sensor  (I2C 0x23)
 *
 * Verified present by the I2C scan: 0x23, ADDR pin low.
 *
 * XIAO ESP32S3 I2C:  D4 = GPIO5 = SDA,  D5 = GPIO6 = SCL
 * Library:  arduino-cli lib install "BH1750"
 *
 * Build:  arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 -u -p <PORT> 21_test_bh1750
 * Watch:  mon.ps1 -Port <PORT>        (or -Seconds 15 for a bounded read)
 *
 * Serial output is English on purpose: the Windows console reads the port as
 * the ANSI codepage, so Korean text comes back as mojibake in logs.
 */
#include <Wire.h>
#include <BH1750.h>

#define BH1750_ADDR 0x23

BH1750 lux(BH1750_ADDR);

int testsRun = 0, testsPassed = 0;

void check(const char *name, bool ok, const char *detail = "") {
  testsRun++;
  if (ok) testsPassed++;
  Serial.printf("[%s] %-28s %s\n", ok ? "PASS" : "FAIL", name, detail);
}

// Does the device acknowledge its address at all?
bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void setup() {
  Serial.begin(115200);
  delay(3000);  // USB CDC enumeration - without this the first lines vanish

  Serial.println();
  Serial.println("=== BH1750 unit test ===");

  Wire.begin(D4, D5);
  Wire.setClock(100000);

  // ---- T1: the chip answers on the bus ----
  bool present = i2cPresent(BH1750_ADDR);
  check("i2c ack at 0x23", present);
  if (!present) {
    Serial.println("ABORT: nothing at 0x23. Check SDA=D4, SCL=D5, 3V3, GND.");
    Serial.println("RESULT: 1 of 1 checks failed");
    return;
  }

  // ---- T2: driver initialises ----
  // CONTINUOUS_HIGH_RES_MODE = 1 lx resolution, ~120 ms per conversion.
  bool began = lux.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  check("begin(CONT_HIGH_RES)", began);

  delay(200);  // let the first conversion complete
}

void loop() {
  static bool reported = false;

  if (testsRun == 0) { delay(1000); return; }   // aborted in setup

  float v = lux.readLightLevel();

  if (!reported) {
    // ---- T3: a reading is actually produced ----
    // The library returns -1 on read error and -2 on timeout.
    check("readLightLevel() returns", v >= 0,
          v < 0 ? "driver returned an error code" : "");

    // ---- T4: the value is physically plausible ----
    // BH1750 range is 0..54612 lx. A stuck bus typically yields exactly 0
    // forever or a pegged max, so bounds-check rather than trust one read.
    char d[48];
    snprintf(d, sizeof(d), "%.1f lx", v);
    check("value within 0..54612 lx", v >= 0 && v <= 54612.0f, d);

    // ---- T5: the chip still answers after a mode change ----
    // Deliberately NOT "the value changed" - under steady light an unchanging
    // reading is correct behaviour, so that assertion would fail on a healthy
    // sensor unless a human happens to be waving at it. Instead, re-issue a
    // mode command and confirm the device still produces a valid measurement:
    // that exercises a write and a read, which is what a wedged bus breaks.
    lux.configure(BH1750::CONTINUOUS_HIGH_RES_MODE_2);  // 0.5 lx resolution
    delay(200);
    float v2 = lux.readLightLevel();
    snprintf(d, sizeof(d), "%.1f lx @0.5lx-res", v2);
    check("valid read after mode switch", v2 >= 0 && v2 <= 54612.0f, d);

    Serial.println();
    Serial.printf("RESULT: %d/%d automated checks passed\n", testsPassed, testsRun);
    Serial.println();
    Serial.println("Manual check - cover the sensor with your hand and watch");
    Serial.println("the stream below drop, then uncover and watch it recover.");
    Serial.println("This one needs your eyes, so it is not scored above.");
    Serial.println();
    reported = true;
  }

  Serial.printf("lux = %8.1f\n", v);
  delay(1000);
}
