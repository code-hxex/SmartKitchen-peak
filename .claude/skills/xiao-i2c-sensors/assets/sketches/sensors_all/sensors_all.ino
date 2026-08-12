/*
 * All three I2C sensors on one bus, streaming to serial.
 *
 *   BH1750  0x23   ambient light                      lx
 *   BME280  0x76   temperature / pressure / humidity  C / hPa / %RH
 *   BNO055  0x29   9-DOF fused orientation            heading / roll / pitch
 *
 * XIAO ESP32S3 I2C:  D4 = GPIO5 = SDA,  D5 = GPIO6 = SCL
 *
 * Libraries:
 *   arduino-cli lib install "BH1750"
 *   arduino-cli lib install "Adafruit BME280 Library"
 *   arduino-cli lib install "Adafruit BNO055"
 *
 * Build:  arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 -u -p <PORT> 24_sensors_all
 * Watch:  mon.ps1 -Port <PORT>          (Ctrl+C to stop)
 *
 * Serial output is English on purpose - the Windows console reads the port as
 * the ANSI codepage, so Korean text arrives as mojibake in captured logs.
 *
 * Design notes worth keeping:
 *  - A missing sensor is reported once and then skipped, so one unplugged
 *    module does not stop the other two from streaming. Losing all output
 *    because of one loose wire is the failure mode this avoids.
 *  - 0x76 is shared by BME280 and BMP280, so the chip-ID register decides
 *    whether humidity exists rather than the silkscreen on the module.
 *  - setExtCrystalUse(true) is deliberately NOT called on the BNO055. See
 *    the long comment at initBno() - it silently zeroes every output on
 *    modules with no 32 kHz crystal fitted.
 */
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BNO055.h>

#define BH1750_ADDR 0x23
#define BME_ADDR    0x76
#define BNO_ADDR    0x29

#define BME_CHIP_ID_REG 0xD0
#define ID_BME280       0x60   // has humidity
#define ID_BMP280       0x58   // no humidity

const unsigned long PERIOD_MS = 1000;

BH1750 lightMeter(BH1750_ADDR);
Adafruit_BME280 bme;
Adafruit_BNO055 bno(55, BNO_ADDR, &Wire);

bool haveLight = false, haveEnv = false, haveImu = false;
bool envHasHumidity = false;
unsigned long lastRead = 0;

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

void initLight() {
  if (!i2cPresent(BH1750_ADDR)) {
    Serial.println("  BH1750  0x23  MISSING (check wiring; ADDR high moves it to 0x5C)");
    return;
  }
  haveLight = lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);
  Serial.printf("  BH1750  0x23  %s\n", haveLight ? "ok" : "present but begin() failed");
}

void initEnv() {
  if (!i2cPresent(BME_ADDR)) {
    Serial.println("  BME280  0x76  MISSING (SDO high moves it to 0x77)");
    return;
  }
  // The address is shared with the BMP280, which has no humidity channel.
  // Read the ID rather than trusting the module's label.
  uint8_t id = readReg(BME_ADDR, BME_CHIP_ID_REG);
  envHasHumidity = (id == ID_BME280);
  if (id != ID_BME280 && id != ID_BMP280) {
    Serial.printf("  BME280  0x76  unexpected chip id 0x%02X\n", id);
    return;
  }
  haveEnv = bme.begin(BME_ADDR, &Wire);
  if (haveEnv) {
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                    Adafruit_BME280::SAMPLING_X2,
                    Adafruit_BME280::SAMPLING_X16,
                    Adafruit_BME280::SAMPLING_X1,
                    Adafruit_BME280::FILTER_X16,
                    Adafruit_BME280::STANDBY_MS_500);
  }
  Serial.printf("  %s  0x76  %s\n",
                envHasHumidity ? "BME280" : "BMP280",
                haveEnv ? (envHasHumidity ? "ok (T/P/RH)" : "ok (T/P only)")
                        : "present but begin() failed");
}

void initBno() {
  if (!i2cPresent(BNO_ADDR)) {
    Serial.println("  BNO055  0x29  MISSING (ADDR/COM3 low puts it at 0x28)");
    return;
  }
  // The Adafruit library defaults to 0x28. This module strapped ADDR high, so
  // the address is passed in the constructor - a mismatch there is the usual
  // reason begin() fails on a module that answers the scan just fine.
  haveImu = bno.begin();
  if (!haveImu) {
    Serial.println("  BNO055  0x29  begin() failed (address mismatch in ctor?)");
    return;
  }
  delay(1000);

  // Do NOT call bno.setExtCrystalUse(true) here. Many clone modules have no
  // 32 kHz crystal fitted; selecting an absent clock source parks the chip in
  // idle with every output stuck at 0.00 - while the power-on self test still
  // reports 0x0F and system_error stays 0, so it looks perfectly healthy and
  // reads nothing. Verified on this board.
  uint8_t sysStat = 0, selfTest = 0, sysErr = 0;
  bno.getSystemStatus(&sysStat, &selfTest, &sysErr);
  // system_status 5 = fusion algorithm running. Anything else means the
  // orientation numbers below are not real, so say so at startup.
  Serial.printf("  BNO055  0x29  %s (self_test=0x%02X status=%u err=%u)\n",
                sysStat == 5 ? "ok, fusion running" : "NOT FUSING - values will read 0",
                selfTest, sysStat, sysErr);
}

void setup() {
  Serial.begin(115200);
  delay(3000);  // USB CDC enumeration - without this the banner is lost

  Wire.begin(D4, D5);
  Wire.setClock(100000);

  Serial.println();
  Serial.println("=== XIAO ESP32S3 - I2C sensor stream ===");
  Serial.println("bus: SDA=D4/GPIO5  SCL=D5/GPIO6");
  initLight();
  initEnv();
  initBno();

  if (!haveLight && !haveEnv && !haveImu) {
    Serial.println("No sensors responded. Check 3V3, GND, SDA, SCL.");
  }

  Serial.println();
  Serial.println("BNO055 calibration climbs to 3 as you move the board:");
  Serial.println("  gyro: hold still | accel: 6 orientations | mag: figure-8");
  Serial.println("Heading is only trustworthy once mag reaches 3.");
  Serial.println();
}

void loop() {
  if (millis() - lastRead < PERIOD_MS) return;
  lastRead = millis();

  String line = "";
  char buf[96];

  if (haveLight) {
    snprintf(buf, sizeof(buf), "lux %8.1f | ", lightMeter.readLightLevel());
    line += buf;
  }

  if (haveEnv) {
    if (envHasHumidity) {
      snprintf(buf, sizeof(buf), "%5.2fC %7.2fhPa %5.1f%%RH | ",
               bme.readTemperature(), bme.readPressure() / 100.0f, bme.readHumidity());
    } else {
      snprintf(buf, sizeof(buf), "%5.2fC %7.2fhPa | ",
               bme.readTemperature(), bme.readPressure() / 100.0f);
    }
    line += buf;
  }

  if (haveImu) {
    sensors_event_t ev;
    bno.getEvent(&ev);
    uint8_t sys, gyro, accel, mag;
    bno.getCalibration(&sys, &gyro, &accel, &mag);
    snprintf(buf, sizeof(buf), "H %6.1f R %6.1f P %6.1f | cal %u%u%u%u",
             ev.orientation.x, ev.orientation.y, ev.orientation.z,
             sys, gyro, accel, mag);
    line += buf;
  }

  if (line.length()) Serial.println(line);
}
