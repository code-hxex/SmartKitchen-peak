/*
 * Unit test 3/3 — BNO055 9-DOF absolute orientation IMU  (I2C 0x29)
 *
 * Verified present by the I2C scan: 0x29 (ADDR/COM3 pin high). The Adafruit
 * library defaults to 0x28, so the address is passed explicitly below - that
 * mismatch is the usual reason begin() fails on a perfectly good module.
 *
 * XIAO ESP32S3 I2C:  D4 = GPIO5 = SDA,  D5 = GPIO6 = SCL
 * Libraries:  arduino-cli lib install "Adafruit BNO055"
 *             (pulls in Adafruit Unified Sensor + Adafruit BusIO)
 *
 * Build:  arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 -u -p <PORT> 23_test_bno055
 * Watch:  mon.ps1 -Port <PORT>
 *
 * About calibration: the BNO055 self-calibrates from motion. Right after a
 * cold boot the gyro/accel/mag calibration bytes are 0 and the fused heading
 * drifts. That is normal and is NOT scored as a failure here - the automated
 * checks cover "the chip is alive and fusing", and calibration is reported as
 * a manual step because it needs you to physically move the board.
 */
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BNO055.h>

#define BNO_ADDR    0x29
#define REG_CHIP_ID 0x00
#define ID_BNO055   0xA0

Adafruit_BNO055 bno(55, BNO_ADDR, &Wire);

int testsRun = 0, testsPassed = 0;
bool ready = false;

void check(const char *name, bool ok, const char *detail = "") {
  testsRun++;
  if (ok) testsPassed++;
  Serial.printf("[%s] %-32s %s\n", ok ? "PASS" : "FAIL", name, detail);
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
  Serial.println("=== BNO055 unit test ===");

  Wire.begin(D4, D5);
  Wire.setClock(100000);

  // ---- T1: device answers ----
  bool present = i2cPresent(BNO_ADDR);
  check("i2c ack at 0x29", present);
  if (!present) {
    Serial.println("ABORT: nothing at 0x29. With ADDR/COM3 low it sits at 0x28.");
    Serial.printf("RESULT: %d/%d automated checks passed\n", testsPassed, testsRun);
    return;
  }

  // ---- T2: it is a BNO055 ----
  uint8_t id = readReg(BNO_ADDR, REG_CHIP_ID);
  char d[72];
  snprintf(d, sizeof(d), "reg 0x00 = 0x%02X", id);
  check("chip id is BNO055 (0xA0)", id == ID_BNO055, d);

  // ---- T3: driver initialises ----
  // begin() also waits out the ~650 ms power-on self test.
  bool began = bno.begin();
  check("begin()", began);
  if (!began) {
    Serial.println("HINT: if this fails while T1/T2 passed, the library is");
    Serial.println("      talking to 0x28 - check the address in the constructor.");
    Serial.printf("RESULT: %d/%d automated checks passed\n", testsPassed, testsRun);
    return;
  }
  delay(1000);

  // NOTE: do NOT call bno.setExtCrystalUse(true) blindly. Many clone modules
  // have no 32 kHz crystal fitted; selecting an absent clock source leaves the
  // chip in idle with every output stuck at zero, while the power-on self test
  // still reports 0x0F and system_error stays 0 - so it looks healthy and
  // reads nothing. Verified on this board: with the call, temperature, gravity
  // and the euler angles were all 0.00 and system_status was 0 (idle) instead
  // of 5 (fusion running). Leaving it on the internal oscillator fixes it.

  // ---- T4: power-on self test passed ----
  // ST result bit0..3 = accel / mag / gyro / MCU. 0x0F means all four passed.
  uint8_t selfTest = 0, sysStat = 0, sysErr = 0;
  bno.getSystemStatus(&sysStat, &selfTest, &sysErr);
  snprintf(d, sizeof(d), "ST=0x%02X stat=%u err=%u", selfTest, sysStat, sysErr);
  check("self test all subsystems (0x0F)", (selfTest & 0x0F) == 0x0F, d);

  // ---- T4b: fusion is actually running ----
  // system_status 5 = "sensor fusion algorithm running". Self test passing is
  // not enough - a wrong clock source leaves the chip at 0 (idle) with every
  // output reading zero, which is exactly what the self-test bits will not
  // tell you. Checking this explicitly is what turns a silent dead sensor
  // into a named failure.
  snprintf(d, sizeof(d), "system_status=%u (5=fusion running)", sysStat);
  check("fusion mode active", sysStat == 5, d);

  // ---- T5: no system error ----
  // sysErr != 0 means a real fault (bad init data, self-test fail, etc).
  snprintf(d, sizeof(d), "system_error=%u", sysErr);
  check("no system error", sysErr == 0, d);

  // ---- T6: temperature sane ----
  // The die temp sensor is a cheap liveness probe: an unresponsive chip
  // returns 0 here. Indoors it reads roughly ambient to ambient+15 C.
  int8_t temp = bno.getTemp();
  snprintf(d, sizeof(d), "%d C", temp);
  check("die temperature in 0..70 C", temp > 0 && temp < 70, d);

  // ---- T7: fusion output is finite ----
  // In NDOF mode the Euler vector must produce real numbers immediately,
  // even before calibration converges. NaN means fusion is not running.
  sensors_event_t ev;
  bno.getEvent(&ev);
  float hd = ev.orientation.x, rl = ev.orientation.y, pt = ev.orientation.z;
  snprintf(d, sizeof(d), "H=%.1f R=%.1f P=%.1f", hd, rl, pt);
  check("euler angles are finite", !isnan(hd) && !isnan(rl) && !isnan(pt), d);

  // ---- T8: heading within the valid 0..360 range ----
  check("heading within 0..360 deg", hd >= 0.0f && hd <= 360.0f, d);

  // ---- T9: gravity magnitude is ~9.8 m/s^2 ----
  // This is the strongest single proof the accelerometer is real and scaled
  // correctly: whatever way the board is lying, the gravity vector length
  // must come out near g. A dead axis or wrong range shows up here.
  imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GRAVITY);
  float mag = sqrtf(g.x() * g.x() + g.y() * g.y() + g.z() * g.z());
  snprintf(d, sizeof(d), "|g| = %.2f m/s^2", mag);
  check("gravity magnitude 9.0..10.5", mag > 9.0f && mag < 10.5f, d);

  Serial.println();
  Serial.printf("RESULT: %d/%d automated checks passed\n", testsPassed, testsRun);
  Serial.println();
  Serial.println("Manual check - calibration needs physical motion:");
  Serial.println("  GYRO : leave the board still for a few seconds      -> 3");
  Serial.println("  ACCEL: hold it in 6 different orientations, pausing -> 3");
  Serial.println("  MAG  : draw a figure-8 in the air                   -> 3");
  Serial.println("Heading is only trustworthy once MAG reaches 3.");
  Serial.println();
  ready = true;
}

void loop() {
  if (!ready) { delay(1000); return; }

  uint8_t sys, gyro, accel, mag;
  bno.getCalibration(&sys, &gyro, &accel, &mag);

  sensors_event_t ev;
  bno.getEvent(&ev);

  Serial.printf("H=%6.1f  R=%6.1f  P=%6.1f   cal sys=%u gyro=%u accel=%u mag=%u\n",
                ev.orientation.x, ev.orientation.y, ev.orientation.z,
                sys, gyro, accel, mag);
  delay(500);
}
