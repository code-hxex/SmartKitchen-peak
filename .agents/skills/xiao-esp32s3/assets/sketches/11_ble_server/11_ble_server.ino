// 11. BLE server — wiki "Bluetooth Usage" example: advertise a BLE service
// with a readable/writable characteristic. Connect with a phone app
// (nRF Connect / LightBlue) and read or write the value.
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

class WriteCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = c->getValue();
    Serial.print("Received via BLE: ");
    Serial.println(v);
  }
};

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println("Starting BLE server...");

  BLEDevice::init("XIAO_ESP32S3");
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *ch = service->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  ch->setValue("Hello from XIAO ESP32S3");
  ch->setCallbacks(new WriteCB());
  service->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("Advertising as 'XIAO_ESP32S3'. Connect with nRF Connect.");
}

void loop() {
  delay(2000);
}
