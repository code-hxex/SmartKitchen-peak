// 03. Serial — Hello World + echo back whatever is received
void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("Hello World from XIAO ESP32S3!");
  Serial.println("Type something and I will echo it back.");
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last > 2000) {
    last = millis();
    Serial.printf("[%lu ms] alive\n", millis());
  }
  while (Serial.available()) {
    String s = Serial.readStringUntil('\n');
    Serial.print("echo: ");
    Serial.println(s);
  }
}
