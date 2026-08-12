// 05. WiFi scan — list nearby access points
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(3000);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  Serial.println("WiFi scan start...");
}

void loop() {
  int n = WiFi.scanNetworks();
  Serial.printf("--- %d networks found ---\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("%2d: %-32s RSSI=%d ch=%d %s\n",
                  i + 1,
                  WiFi.SSID(i).c_str(),
                  WiFi.RSSI(i),
                  WiFi.channel(i),
                  WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured");
  }
  WiFi.scanDelete();
  delay(5000);
}
