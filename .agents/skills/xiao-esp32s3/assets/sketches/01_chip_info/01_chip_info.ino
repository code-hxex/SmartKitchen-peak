// 01. Chip info — identify the board (model, PSRAM, flash, MAC)
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  delay(3000);  // wait for USB CDC

  Serial.println("=== XIAO ESP32S3 Chip Info ===");
  Serial.printf("Chip model    : %s rev%d\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("CPU cores     : %d @ %d MHz\n", ESP.getChipCores(), ESP.getCpuFreqMHz());
  Serial.printf("Flash size    : %u MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  Serial.printf("PSRAM size    : %u bytes\n", ESP.getPsramSize());
  Serial.printf("Free heap     : %u bytes\n", ESP.getFreeHeap());
  Serial.printf("SDK version   : %s\n", ESP.getSdkVersion());
  Serial.printf("WiFi MAC      : %s\n", WiFi.macAddress().c_str());
  Serial.println("=== done ===");
}

void loop() {
  delay(1000);
}
