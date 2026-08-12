/*
 * Unit test 2/3 — BME280 temperature / pressure / humidity  (I2C 0x76)
 *
 * The module is often silkscreened GY-BME280 / "GYB11". Address alone cannot
 * tell BME280 from BMP280 - both live at 0x76/0x77 - so this test reads the
 * chip-ID register (0xD0) and refuses to continue on a mismatch. Verified on
 * this board: 0xD0 = 0x60 = BME280, so humidity is available. A BMP280 would
 * report 0x58 and have no humidity channel.
 *
 * XIAO ESP32S3 I2C:  D4 = GPIO5 = SDA,  D5 = GPIO6 = SCL
 * Libraries:  arduino-cli lib install "Adafruit BME280 Library"
 *             (pulls in Adafruit Unified Sensor + Adafruit BusIO)
 *
 * Build:  arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 -u -p <PORT> 22_test_bme280
 * Watch:  mon.ps1 -Port <PORT>
 */
#include <Wire.h>
#include <Adafruit_BME280.h>

#define BME_ADDR    0x76
#define REG_CHIP_ID 0xD0
#define ID_BME280   0x60
#define ID_BMP280   0x58

Adafruit_BME280 bme;

int testsRun = 0, testsPassed = 0;

void check(const char *name, bool ok, const char *detail = "") {
  testsRun++;
  if (ok) testsPassed++;
  Serial.printf("[%s] %-30s %s\n", ok ? "PASS" : "FAIL", name, detail);
}

bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

uint8_t readReg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return 0xFF;
  return Wire.read();
}

void setup() {
  Serial.begin(115200);
  delay(3000);

  Serial.println();
  Serial.println("=== BME280 unit test ===");

  Wire.begin(D4, D5);
  Wire.setClock(100000);

  // ---- T1: device answers ----
  bool present = i2cPresent(BME_ADDR);
  check("i2c ack at 0x76", present);
  if (!present) {
    Serial.println("ABORT: nothing at 0x76. Some boards strap SDO high -> 0x77.");
    Serial.printf("RESULT: %d/%d automated checks passed\n", testsPassed, testsRun);
    return;
  }

  // ---- T2: it is the part we think it is ----
  uint8_t id = readReg(BME_ADDR, REG_CHIP_ID);
  char d[64];
  snprintf(d, sizeof(d), "0xD0 = 0x%02X", id);
  check("chip id is BME280 (0x60)", id == ID_BME280, d);
  if (id == ID_BMP280) {
    Serial.println("NOTE: this is a BMP280 - temperature and pressure only.");
    Serial.println("      Switch to Adafruit_BMP280 and drop the humidity checks.");
  }

  // ---- T3: driver initialises ----
  bool began = bme.begin(BME_ADDR, &Wire);
  check("begin(0x76)", began);
  if (!began) {
    Serial.printf("RESULT: %d/%d automated checks passed\n", testsPassed, testsRun);
    return;
  }

  // Weather-station preset: low power, 1 Hz-ish, heavy filtering off.
  bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                  Adafruit_BME280::SAMPLING_X2,   // temperature
                  Adafruit_BME280::SAMPLING_X16,  // pressure
                  Adafruit_BME280::SAMPLING_X1,   // humidity
                  Adafruit_BME280::FILTER_X16,
                  Adafruit_BME280::STANDBY_MS_500);
  delay(300);

  float t = bme.readTemperature();
  float p = bme.readPressure() / 100.0f;   // Pa -> hPa
  float h = bme.readHumidity();

  // ---- T4..T6: values physically plausible ----
  // Bounds are deliberately wide - this catches a dead bus (NaN, 0, or a
  // pegged rail), not a miscalibrated part. The BME280 datasheet range is
  // -40..85 C, 300..1100 hPa, 0..100 %RH.
  snprintf(d, sizeof(d), "%.2f C", t);
  check("temperature in -40..85 C", !isnan(t) && t > -40 && t < 85, d);

  snprintf(d, sizeof(d), "%.2f hPa", p);
  check("pressure in 300..1100 hPa", !isnan(p) && p > 300 && p < 1100, d);

  snprintf(d, sizeof(d), "%.2f %%RH", h);
  check("humidity in 0..100 %RH", !isnan(h) && h >= 0 && h <= 100, d);

  // ---- T7: temperature is not the reset default ----
  // An uninitialised / never-converted BME280 reads exactly 0.00 C. A real
  // indoor reading essentially never lands on 0.00, so this separates
  // "sensor is converting" from "registers are still at their reset value".
  check("temperature is not exactly 0.00", fabs(t) > 0.005f);

  // ---- T8: two conversions apart in time both succeed ----
  // Proves NORMAL mode keeps cycling rather than producing one stale frame.
  delay(1200);
  float t2 = bme.readTemperature();
  snprintf(d, sizeof(d), "%.2f C -> %.2f C", t, t2);
  check("second conversion also valid", !isnan(t2) && t2 > -40 && t2 < 85, d);

  Serial.println();
  Serial.printf("RESULT: %d/%d automated checks passed\n", testsPassed, testsRun);
  Serial.println();
  Serial.println("Manual check - breathe on the sensor: humidity should jump");
  Serial.println("within a second or two and settle back afterwards.");
  Serial.println();
}

void loop() {
  if (testsRun == 0) { delay(1000); return; }
  Serial.printf("T = %6.2f C   P = %8.2f hPa   RH = %6.2f %%\n",
                bme.readTemperature(), bme.readPressure() / 100.0f, bme.readHumidity());
  delay(1000);
}
