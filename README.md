# SmartKitchen-peak

## esp32_cam_webserver

XIAO ESP32-S3 Sense 보드의 카메라 모듈이 정상 동작하는지 확인하는 유닛 테스트 코드.
공식 ESP32 `CameraWebServer` 예제를 XIAO ESP32-S3 Sense용 카메라 핀맵(`CAMERA_MODEL_XIAO_ESP32S3`)으로
구성한 것으로, 보드가 WiFi에 연결된 뒤 자체 IP로 MJPEG 스트리밍/스냅샷 웹 페이지를 띄운다.
사용법은 [`esp32_cam_webserver/README.md`](esp32_cam_webserver/README.md) 참고.