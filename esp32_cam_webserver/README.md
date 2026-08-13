# esp32_cam_webserver

XIAO ESP32-S3 Sense 카메라 유닛 테스트 - 공식 ESP32 `CameraWebServer` 예제를 XIAO ESP32-S3 Sense용으로 구성한 것.
WiFi에 연결해 내장 카메라의 MJPEG 스트리밍/스냅샷을 웹 브라우저에서 확인할 수 있다.

## 보드 설정 (Arduino CLI)

```
FQBN: esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=default_8MB
```

- `PSRAM=opi` 필수 (Sense 모듈 OPI PSRAM 사용, UXGA 해상도/JPEG 버퍼링에 필요)
- Partition Scheme은 APP 영역 3MB 이상 필요 (`default_8MB`)

## 사용 전 설정

`CameraWebServer.ino`의 WiFi 자격 증명을 실제 값으로 교체:

```cpp
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";
```

`board_config.h`에서 카메라 모델은 `CAMERA_MODEL_XIAO_ESP32S3`로 지정되어 있다.

## 컴파일 / 업로드

```
arduino-cli compile --fqbn "esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=default_8MB" esp32_cam_webserver
arduino-cli upload -p COM9 --fqbn "esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=default_8MB" esp32_cam_webserver
```

## 동작 확인

업로드 후 시리얼 모니터(115200bps)에서 WiFi 연결과 함께 아래와 같은 로그가 출력된다.

```
WiFi connected
Camera Ready! Use 'http://<device-ip>' to connect
```

출력된 IP로 브라우저에서 접속하면 스트리밍 페이지가 뜬다.
