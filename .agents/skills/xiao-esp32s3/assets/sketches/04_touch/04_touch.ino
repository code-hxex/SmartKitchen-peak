// 04. Touch — read touch sensor on D1 (GPIO2), light user LED when touched
// XIAO ESP32S3: touch-capable pins include GPIO1..GPIO9 (D0..D8 area)
const int TOUCH_PIN = T2;     // GPIO2 = D1
const int LED = LED_BUILTIN;  // GPIO21, inverted

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);  // off
  delay(3000);
  Serial.println("Touch test: touch pin D1 (GPIO2)");
}

void loop() {
  uint32_t v = touchRead(TOUCH_PIN);
  // ESP32-S3 touch value INCREASES when touched
  bool touched = v > 40000;
  digitalWrite(LED, touched ? LOW : HIGH);
  Serial.printf("touch=%lu %s\n", v, touched ? "TOUCHED" : "");
  delay(200);
}
