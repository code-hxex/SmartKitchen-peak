// 06. BLE scan — scan nearby BLE devices for 5 seconds, repeat
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

BLEScan* pScan;

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("BLE scan start...");
  BLEDevice::init("XIAO_ESP32S3");
  pScan = BLEDevice::getScan();
  pScan->setActiveScan(true);
}

void loop() {
  BLEScanResults* results = pScan->start(5, false);
  int n = results->getCount();
  Serial.printf("--- %d BLE devices found ---\n", n);
  for (int i = 0; i < n; i++) {
    BLEAdvertisedDevice d = results->getDevice(i);
    Serial.printf("%2d: %s RSSI=%d %s\n",
                  i + 1,
                  d.getAddress().toString().c_str(),
                  d.getRSSI(),
                  d.haveName() ? d.getName().c_str() : "(no name)");
  }
  pScan->clearResults();
  delay(2000);
}
