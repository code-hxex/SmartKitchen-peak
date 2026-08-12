# Verified example sketches (assets/sketches/)

All 11 compiled, uploaded, and ran successfully on a real XIAO ESP32S3
(esp32 core 3.3.10, 2026-07). Copy the closest one into the user's workspace
as a starting point.

| # | Folder | What it proves | Verified output |
|---|--------|----------------|-----------------|
| 01 | `01_chip_info` | Board identity: `ESP.getChipModel()`, flash, PSRAM, MAC | ESP32-S3 rev2, 2×240 MHz, 8 MB flash, PSRAM 0 (option off) |
| 02 | `02_blink` | GPIO out on inverted user LED (GPIO21) | `LED ON`/`LED OFF` every 500 ms |
| 03 | `03_serial_echo` | USB CDC serial in+out | `echo: <sent text>` round-trip |
| 04 | `04_touch` | `touchRead(T2)` on D1 | baseline ≈ 17,535; rises when touched |
| 05 | `05_wifi_scan` | `WiFi.scanNetworks()` | numbered AP list with SSID/RSSI/channel |
| 06 | `06_ble_scan` | BLE active scan (BLEDevice/BLEScan) | 10+ devices per 5 s sweep |
| 07 | `07_pwm_fade` | `analogWrite` PWM on LED | smooth breathing fade |
| 08 | `08_analog_read` | 12-bit ADC + `analogReadMilliVolts` on A0 | floating noise 60–160 raw (normal) |
| 09 | `09_softap_led` | SoftAP + `NetworkServer` HTTP LED control | `AP IP address: 192.168.4.1`; join `XIAO_ESP32S3_AP` / `12345678` from a phone, open http://192.168.4.1 |
| 10 | `10_deep_sleep` | Timer deep sleep, 10 s | COM port disappears 10 s / reappears ~3 s, exact cycle |
| 11 | `11_ble_server` | BLE GATT server, read/write characteristic | advertises `XIAO_ESP32S3`; test with nRF Connect |

## Sketch skeleton that always works

```cpp
void setup() {
  Serial.begin(115200);
  delay(3000);            // USB CDC enumeration — do not skip
  // ... setup ...
}
void loop() {
  // print state changes so logs alone can verify behavior
}
```

## API notes for esp32 core 3.x (differ from old tutorials)

- `NetworkClient` / `NetworkServer` replace `WiFiClient` / `WiFiServer`;
  `server.accept()` replaces `server.available()`.
- BLE scan: `BLEScan::start(sec, false)` returns `BLEScanResults*` (pointer).
- Deep sleep: `esp_sleep_enable_timer_wakeup(us)` + `esp_deep_sleep_start()`;
  `RTC_DATA_ATTR` survives sleep wakes but NOT the reset caused by opening
  the serial port from the PC.

## Needs extra hardware/info (not bundled)

- Router WiFi connect + MQTT — needs SSID/password from the user.
- I2C/SPI OLED — needs the module / expansion board.
- ESP-NOW — needs a second XIAO board.
- Camera / mic / microSD — Sense variant only.
