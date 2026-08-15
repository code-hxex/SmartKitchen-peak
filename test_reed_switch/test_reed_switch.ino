/*
 * Unit test - HAM4318 reed switch (냉장고 문 개폐 감지용)
 *
 * 배선: 리드스위치를 XIAO ESP32S3 Sense 보드 헤더 라벨 D10 (칩 GPIO9) <-> GND에 연결,
 *       핀은 INPUT_PULLUP으로 설정한다 (esp32_cam_serial.ino와 동일한 배선/극성 기준).
 *       자석이 가까워져 스위치가 닫히면 핀이 LOW, 멀어져 스위치가 열리면 핀이 HIGH.
 *       (칩의 실제 GPIO10은 카메라 XCLK로 이미 사용 중이라 리드스위치용으로 쓸 수 없다.)
 *
 * Build:  arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 test_reed_switch
 * Upload: arduino-cli upload -p COM11 --fqbn esp32:esp32:XIAO_ESP32S3 test_reed_switch
 * Watch:  read_serial.ps1 -Port COM11 -Seconds 15
 *
 * Serial output is English on purpose: the Windows console reads the port as
 * the ANSI codepage, so Korean text would come back as mojibake in logs.
 */

#define REED_GPIO_NUM 9   // board silkscreen D10 = chip GPIO9
#define REED_SAMPLE_COUNT 20
#define REED_SAMPLE_INTERVAL_MS 10
#define REED_ACTIVE_LOW_MEANS_OPEN false  // flip if wiring makes LOW = open

int testsRun = 0, testsPassed = 0;

void check(const char *name, bool ok, const char *detail = "") {
  testsRun++;
  if (ok) testsPassed++;
  Serial.printf("[%s] %-28s %s\n", ok ? "PASS" : "FAIL", name, detail);
}

bool readIsOpenRaw() {
  bool pinHigh = (digitalRead(REED_GPIO_NUM) == HIGH);
  return REED_ACTIVE_LOW_MEANS_OPEN ? !pinHigh : pinHigh;
}

void setup() {
  Serial.begin(115200);
  delay(3000);  // USB CDC enumeration - without this the first lines vanish

  Serial.println();
  Serial.println("=== HAM4318 reed switch unit test (GPIO9 / D10) ===");

  pinMode(REED_GPIO_NUM, INPUT_PULLUP);
  delay(20);  // let the pull-up settle before the first read

  // ---- T1: pin reads a defined digital level, not floating garbage ----
  int first = digitalRead(REED_GPIO_NUM);
  check("digitalRead returns 0 or 1", first == LOW || first == HIGH);

  // ---- T2: N rapid reads agree with each other (no bounce/noise at rest) ----
  int mismatches = 0;
  for (int i = 0; i < REED_SAMPLE_COUNT; i++) {
    if (digitalRead(REED_GPIO_NUM) != first) mismatches++;
    delay(REED_SAMPLE_INTERVAL_MS);
  }
  char d[48];
  snprintf(d, sizeof(d), "%d/%d mismatches", mismatches, REED_SAMPLE_COUNT);
  check("stable across rapid reads", mismatches == 0, d);

  Serial.println();
  Serial.printf("RESULT: %d/%d automated checks passed\n", testsPassed, testsRun);
  Serial.println();
  Serial.println("Manual check - bring a magnet close to the reed switch and");
  Serial.println("watch the state below flip to CLOSED, then pull it away and");
  Serial.println("watch it flip back to OPEN. This needs your eyes, so it is");
  Serial.println("not scored above.");
  Serial.println();
}

void loop() {
  bool isOpen = readIsOpenRaw();

  // 상태 변화 여부와 상관없이 매 주기마다 값을 출력한다 - 시리얼 모니터를
  // 언제 열어도 바로 현재 값을 볼 수 있어야 하므로.
  Serial.printf("door = %-6s (raw pin = %s)\n",
                isOpen ? "OPEN" : "CLOSED",
                digitalRead(REED_GPIO_NUM) == HIGH ? "HIGH" : "LOW");

  delay(300);
}
