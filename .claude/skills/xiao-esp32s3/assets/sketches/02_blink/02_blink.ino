// 02. Blink — XIAO ESP32S3 user LED on GPIO21, inverted logic (LOW = on)
const int LED = LED_BUILTIN;  // GPIO21 on XIAO ESP32S3

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
}

void loop() {
  digitalWrite(LED, LOW);   // LED on
  Serial.println("LED ON");
  delay(500);
  digitalWrite(LED, HIGH);  // LED off
  Serial.println("LED OFF");
  delay(500);
}
