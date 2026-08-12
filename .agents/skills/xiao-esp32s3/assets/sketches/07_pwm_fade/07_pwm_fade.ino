// 07. PWM fade — wiki "Digital as PWM" example adapted to the onboard user LED
// (wiki uses Grove LED on D0; we use GPIO21 user LED, inverted logic)
const int LED = LED_BUILTIN;

void setup() {
  Serial.begin(115200);
}

void loop() {
  // fade in (LED inverted: 255 = off, 0 = full on)
  for (int v = 255; v >= 0; v -= 5) {
    analogWrite(LED, v);
    delay(30);
  }
  for (int v = 0; v <= 255; v += 5) {
    analogWrite(LED, v);
    delay(30);
  }
  Serial.println("fade cycle done");
}
