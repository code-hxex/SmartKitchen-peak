// 08. Analog read — wiki "Analog" example (oxygen sensor) adapted:
// read raw ADC on A0 and print value + voltage. Works with nothing attached
// (floating values) or any analog sensor on A0.
const int ANALOG_PIN = A0;

void setup() {
  Serial.begin(115200);
  delay(3000);
  analogReadResolution(12);  // 0..4095
  Serial.println("Analog read on A0 (12-bit)");
}

void loop() {
  int raw = analogRead(ANALOG_PIN);
  float mv = analogReadMilliVolts(ANALOG_PIN);
  Serial.printf("raw=%4d  voltage=%.1f mV\n", raw, mv);
  delay(500);
}
