// I2C 버스 스캐너 — 어떤 센서가 어느 주소에 붙어 있는지 먼저 확인한다.
// 모듈 이름으로 추측하지 말고 이 결과를 근거로 드라이버를 고를 것.
//
// XIAO ESP32S3 I2C: D4 = GPIO5 = SDA, D5 = GPIO6 = SCL
#include <Wire.h>

// 알려진 주소 → 후보 장치 (여러 개면 전부 나열)
const char *guess(uint8_t a) {
  switch (a) {
    case 0x23: return "BH1750 (ADDR=L)";
    case 0x5C: return "BH1750 (ADDR=H)";
    case 0x28: return "BNO055 (ADDR=L)";
    case 0x29: return "BNO055 (ADDR=H)";
    case 0x76: return "BME280/BMP280 (SDO=L)";
    case 0x77: return "BME280/BMP280 (SDO=H)";
    case 0x68: return "MPU6050/MPU9250/DS3231";
    case 0x69: return "MPU6050/MPU9250 (AD0=H)";
    case 0x38: return "AHT10/AHT20";
    case 0x44: return "SHT3x";
    case 0x45: return "SHT3x (ADDR=H)";
    case 0x40: return "HTU21/SI7021/INA219";
    case 0x1E: return "HMC5883L";
    case 0x0D: return "QMC5883L";
    case 0x3C: return "SSD1306 OLED";
    default:   return "?";
  }
}

void setup() {
  Serial.begin(115200);
  delay(3000);  // USB CDC 열거 대기 — 없으면 초반 출력이 사라진다

  Wire.begin(D4, D5);   // SDA=GPIO5, SCL=GPIO6
  Wire.setClock(100000);

  Serial.println();
  Serial.println("=== I2C scan (SDA=D4/GPIO5, SCL=D5/GPIO6) ===");
}

void loop() {
  int found = 0;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  0x%02X  %s\n", addr, guess(addr));
      found++;
    }
    delay(2);
  }
  Serial.printf("total: %d device(s)\n", found);

  // 주소만으로는 BME280/BMP280 을 못 가른다. ID 레지스터를 직접 읽어 확정한다.
  probeId(0x76, 0xD0, "0x76 chip id");   // 0x58=BMP280 0x60=BME280 0x61=BME680 0x55=BMP180
  probeId(0x29, 0x00, "0x29 chip id");   // BNO055 = 0xA0
  Serial.println("---");
  delay(3000);
}

void probeId(uint8_t addr, uint8_t reg, const char *what) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) { Serial.printf("  %s: no ack\n", what); return; }
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) { Serial.printf("  %s: no data\n", what); return; }
  uint8_t id = Wire.read();
  const char *name = "?";
  if (reg == 0xD0) {
    if (id == 0x58) name = "BMP280 (온도+기압)";
    else if (id == 0x60) name = "BME280 (온도+기압+습도)";
    else if (id == 0x61) name = "BME680";
    else if (id == 0x55) name = "BMP180";
  } else if (id == 0xA0) {
    name = "BNO055";
  }
  Serial.printf("  %s = 0x%02X  -> %s\n", what, id, name);
}
