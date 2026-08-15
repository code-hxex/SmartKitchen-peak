// XIAO ESP32S3 Sense - WiFi HTTP로 카메라 이미지 제공 (USB 시리얼 불필요)
//                        + WiFi로 백엔드(FastAPI)에 주기적으로 하드웨어 이벤트 전송
//                        + 리드스위치(보드 헤더 라벨 D10 = 칩 GPIO9)로 냉장고 문 개폐 감지
//
// HTTP 엔드포인트 (보드 자신의 IP:80):
//   GET /jpg     : 저해상도(VGA) 미리보기 프레임 1장 (PC가 짧은 주기로 반복 호출해서
//                  실시간 미리보기처럼 사용)
//   GET /capture : 고화질(HD) 프레임 1장
//
// 부팅 시 카메라 초기화에 성공하면 "READY\n"를, 실패하면 "ERR:CAMERA_INIT_FAILED\n"를
// 시리얼(디버그용, 더 이상 프로토콜로 쓰이지 않음)에 출력한다. 카메라 초기화가 실패해도
// WiFi/리드스위치는 계속 동작한다 - /jpg, /capture는 그때그때 503으로 응답한다.
//
// 리드스위치(HAM4318, 보드 헤더 라벨 D10 = 칩 GPIO9): INPUT_PULLUP으로 연결.
// 문이 닫혀 자석이 가까이 오면 스위치가 닫혀 핀이 LOW(문 닫힘), 문이 열려 자석이
// 멀어지면 핀이 HIGH(문 열림)가 되는 배선을 기준으로 한다. 반대로 배선되었다면
// DOOR_ACTIVE_LOW_MEANS_OPEN 값만 뒤집으면 된다.
// (칩의 실제 GPIO10은 카메라 XCLK로 이미 사용 중이라 리드스위치용으로 쓸 수 없다.)

#include "esp_camera.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "wifi_credentials.h"  // WIFI_SSID / WIFI_PASSWORD 정의 (git에 커밋 안 됨, .gitignore 참고)

#define BACKEND_EVENT_URL  "http://172.30.1.84:8000/api/events"
#define BACKEND_DOOR_URL   "http://172.30.1.84:8000/api/door"
#define DEVICE_ID          "xiao-cam-01"
#define EVENT_POST_INTERVAL_MS 20000UL   // 20초마다 한 번씩 하드웨어 이벤트 전송
#define EVENT_FRAMESIZE    FRAMESIZE_QVGA  // 이벤트 페이로드를 작게 유지 (메모리/전송시간 절약)
#define EVENT_QUALITY      15

static unsigned long lastEventPostMs = 0;
static bool cameraReady = false;

WebServer httpServer(80);

// ---- 리드스위치 (문 개폐 감지, 보드 실크스크린 표기 D10 = 칩 GPIO9) ----
#define DOOR_GPIO_NUM 9
#define DOOR_CHECK_INTERVAL_MS 100UL   // 핀 상태를 확인하는 주기
#define DOOR_STABLE_COUNT 3            // 이만큼 연속으로 같은 값이 나와야 "안정된 상태 변화"로 인정 (디바운스)
#define DOOR_ACTIVE_LOW_MEANS_OPEN false  // true로 바꾸면 LOW=열림/HIGH=닫힘으로 판정 반전

static unsigned long lastDoorCheckMs = 0;
static int doorCandidateReading = -1;  // 디바운스 중인 후보 값 (0/1)
static int doorStableCount = 0;
static int doorLastSentState = -1;     // -1: 아직 전송 안 함, 0: 닫힘 전송됨, 1: 열림 전송됨

#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39
#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13

static const char B64_TABLE[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 미리보기 해상도/품질 - 실시간 미리보기용, fps와 화질의 균형점
#define PREVIEW_FRAMESIZE FRAMESIZE_VGA   // 640x480
#define PREVIEW_QUALITY   10              // 숫자가 작을수록 고화질

// 촬영 설정 - 사용자 지정값
#define CAPTURE_FRAMESIZE FRAMESIZE_HD    // 1280x720
#define CAPTURE_QUALITY   4               // 숫자가 작을수록 고화질
#define XCLK_FREQ_HZ      20000000        // 20MHz

// 카메라 센서는 한 번에 하나의 해상도/품질만 가질 수 있어서, /jpg(미리보기)와
// /capture(HD)가 매번 재설정하면 느려진다. 마지막으로 설정한 모드를 기억해뒀다가
// 실제로 바뀔 때만 센서를 재설정한다. postHardwareEventSilently()도 이 상태를
// 공유하므로 임시로 EVENT_FRAMESIZE로 바꿨다가 반드시 원래 모드로 되돌린다.
enum CamMode { CAM_MODE_CAPTURE, CAM_MODE_PREVIEW };
static CamMode camMode = CAM_MODE_CAPTURE;  // initCamera()가 CAPTURE_FRAMESIZE로 초기화함

// 해상도 전환 후 AE/AWB가 새 프레임 크기에 맞춰 재계산되도록 몇 프레임 버린다.
static void settleExposure(int frames) {
  for (int i = 0; i < frames; i++) {
    camera_fb_t *w = esp_camera_fb_get();
    if (w) esp_camera_fb_return(w);
    delay(60);
  }
}

static void setCamMode(CamMode mode) {
  if (camMode == mode) return;
  sensor_t *s = esp_camera_sensor_get();
  if (mode == CAM_MODE_PREVIEW) {
    s->set_framesize(s, PREVIEW_FRAMESIZE);
    s->set_quality(s, PREVIEW_QUALITY);
  } else {
    s->set_framesize(s, CAPTURE_FRAMESIZE);
    s->set_quality(s, CAPTURE_QUALITY);
  }
  settleExposure(2);
  camMode = mode;
}

// OV2640 센서 이미지 처리 파라미터 튜닝 (사용자 지정값만 설정, 나머지는 센서 기본값 유지)
static void applyImageTuning(sensor_t *s) {
  if (!s) return;
  s->set_brightness(s, 2);   // -2..2
  s->set_contrast(s, -1);    // -2..2
  s->set_sharpness(s, 3);    // -2..2 (OV2640은 미지원 기능이라 실제로는 무시될 수 있음 - OV3660/OV5640 전용)
  // De-Noise: Auto -> 별도로 설정하지 않고 센서 기본(자동) 동작에 맡김
  // (OV2640 드라이버는 set_denoise API를 구현하지 않아 수동으로 켜고 끌 수 없음)
}

bool initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;   config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = -1; config.pin_reset = -1;
  config.xclk_freq_hz = XCLK_FREQ_HZ;
  config.frame_size = CAPTURE_FRAMESIZE;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = CAPTURE_QUALITY;
  config.fb_count = 2;

  if (esp_camera_init(&config) != ESP_OK) {
    return false;
  }

  applyImageTuning(esp_camera_sensor_get());

  // 자동 노출/화이트밸런스 워밍업
  settleExposure(8);
  return true;
}

// streamBase64와 동일한 인코딩이지만 Serial이 아니라 메모리 버퍼에 쓴다.
// 반환값은 기록한 바이트 수. out은 최소 4*((len+2)/3) 바이트여야 한다.
size_t base64EncodeInto(const uint8_t *data, size_t len, char *out) {
  size_t i = 0, o = 0;
  while (i + 3 <= len) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
    out[o++] = B64_TABLE[(n >> 18) & 0x3F];
    out[o++] = B64_TABLE[(n >> 12) & 0x3F];
    out[o++] = B64_TABLE[(n >> 6) & 0x3F];
    out[o++] = B64_TABLE[n & 0x3F];
    i += 3;
  }
  size_t rem = len - i;
  if (rem == 1) {
    uint32_t n = ((uint32_t)data[i]) << 16;
    out[o++] = B64_TABLE[(n >> 18) & 0x3F];
    out[o++] = B64_TABLE[(n >> 12) & 0x3F];
    out[o++] = '=';
    out[o++] = '=';
  } else if (rem == 2) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
    out[o++] = B64_TABLE[(n >> 18) & 0x3F];
    out[o++] = B64_TABLE[(n >> 12) & 0x3F];
    out[o++] = B64_TABLE[(n >> 6) & 0x3F];
    out[o++] = '=';
  }
  return o;
}

// 작은 프레임을 캡처해서 백엔드 POST /api/events 로 보낸다.
// (센서가 아직 없으므로 frame_base64만 채우고 sensor 필드는 생략)
void postHardwareEventSilently() {
  if (!cameraReady) return;
  if (WiFi.status() != WL_CONNECTED) return;

  CamMode modeBeforeEvent = camMode;  // 이벤트 캡처 후 원래 모드로 복귀하기 위해 기억
  sensor_t *s = esp_camera_sensor_get();
  s->set_framesize(s, EVENT_FRAMESIZE);
  s->set_quality(s, EVENT_QUALITY);
  settleExposure(1);

  camera_fb_t *fb = esp_camera_fb_get();

  if (modeBeforeEvent == CAM_MODE_PREVIEW) {
    s->set_framesize(s, PREVIEW_FRAMESIZE);
    s->set_quality(s, PREVIEW_QUALITY);
  } else {
    s->set_framesize(s, CAPTURE_FRAMESIZE);
    s->set_quality(s, CAPTURE_QUALITY);
  }
  camMode = modeBeforeEvent;

  if (!fb) return;

  static const char PREFIX[] = "{\"device_id\":\"" DEVICE_ID "\",\"frame_base64\":\"";
  static const char SUFFIX[] = "\"}";
  const size_t prefixLen = sizeof(PREFIX) - 1;
  const size_t suffixLen = sizeof(SUFFIX) - 1;
  const size_t b64Cap = 4 * ((fb->len + 2) / 3);
  const size_t bufCap = prefixLen + b64Cap + suffixLen + 1;

  char *buf = (char *)heap_caps_malloc(bufCap, MALLOC_CAP_SPIRAM);
  if (!buf) {
    esp_camera_fb_return(fb);
    return;
  }

  memcpy(buf, PREFIX, prefixLen);
  size_t written = base64EncodeInto(fb->buf, fb->len, buf + prefixLen);
  memcpy(buf + prefixLen + written, SUFFIX, suffixLen);
  size_t totalLen = prefixLen + written + suffixLen;
  buf[totalLen] = '\0';

  esp_camera_fb_return(fb);

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  if (http.begin(client, BACKEND_EVENT_URL)) {
    http.addHeader("Content-Type", "application/json");
    http.POST((uint8_t *)buf, totalLen);
    http.end();
  }

  heap_caps_free(buf);
}

// 리드스위치 원시 판정 (디바운스 이전). DOOR_ACTIVE_LOW_MEANS_OPEN 로 극성을 뒤집을 수 있다.
bool readDoorIsOpenRaw() {
  bool pinHigh = (digitalRead(DOOR_GPIO_NUM) == HIGH);
  return DOOR_ACTIVE_LOW_MEANS_OPEN ? !pinHigh : pinHigh;
}

// 문 개폐 상태를 백엔드로 전송한다.
void postDoorStateSilently(bool isOpen) {
  if (WiFi.status() != WL_CONNECTED) return;

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  if (http.begin(client, BACKEND_DOOR_URL)) {
    http.addHeader("Content-Type", "application/json");
    char body[96];
    snprintf(body, sizeof(body), "{\"device_id\":\"%s\",\"is_open\":%s}",
             DEVICE_ID, isOpen ? "true" : "false");
    http.POST((uint8_t *)body, strlen(body));
    http.end();
  }
}

// 짧은 주기로 핀을 읽되, 연속 DOOR_STABLE_COUNT회 같은 값이 나와야 실제 상태
// 변화로 인정한다 (스위치 바운스/순간 잡음 방지). 상태가 실제로 바뀔 때만 전송.
void checkDoorState() {
  if (millis() - lastDoorCheckMs < DOOR_CHECK_INTERVAL_MS) return;
  lastDoorCheckMs = millis();

  int reading = readDoorIsOpenRaw() ? 1 : 0;
  if (reading == doorCandidateReading) {
    if (doorStableCount < DOOR_STABLE_COUNT) doorStableCount++;
  } else {
    doorCandidateReading = reading;
    doorStableCount = 1;
  }

  if (doorStableCount == DOOR_STABLE_COUNT && doorCandidateReading != doorLastSentState) {
    doorLastSentState = doorCandidateReading;
    postDoorStateSilently(doorCandidateReading == 1);
  }
}

// GET /jpg - 저해상도 미리보기 프레임 1장. PC가 짧은 주기로 반복 호출한다.
void handleJpg() {
  if (!cameraReady) {
    httpServer.send(503, "text/plain", "camera not ready");
    return;
  }
  setCamMode(CAM_MODE_PREVIEW);
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpServer.send(503, "text/plain", "capture failed");
    return;
  }
  httpServer.setContentLength(fb->len);
  httpServer.send(200, "image/jpeg", "");
  httpServer.client().write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// GET /capture - 고화질(HD) 프레임 1장.
void handleCapture() {
  if (!cameraReady) {
    httpServer.send(503, "text/plain", "camera not ready");
    return;
  }
  setCamMode(CAM_MODE_CAPTURE);
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpServer.send(503, "text/plain", "capture failed");
    return;
  }
  httpServer.setContentLength(fb->len);
  httpServer.send(200, "image/jpeg", "");
  httpServer.client().write(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void setup() {
  Serial.begin(921600);
  delay(3000);

  pinMode(DOOR_GPIO_NUM, INPUT_PULLUP);

  esp_log_level_set("*", ESP_LOG_NONE);

  cameraReady = initCamera();
  Serial.print(cameraReady ? "READY\n" : "ERR:CAMERA_INIT_FAILED\n");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t wifiDeadline = millis() + 15000;
  while (WiFi.status() != WL_CONNECTED && millis() < wifiDeadline) {
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WIFI_IP:");
    Serial.println(WiFi.localIP());
    if (MDNS.begin(DEVICE_ID)) {
      Serial.print("MDNS:http://");
      Serial.print(DEVICE_ID);
      Serial.println(".local");
    }
  } else {
    Serial.println("WIFI_CONNECT_TIMEOUT");
  }

  httpServer.on("/jpg", handleJpg);
  httpServer.on("/capture", handleCapture);
  httpServer.begin();
}

void loop() {
  httpServer.handleClient();

  if (millis() - lastEventPostMs >= EVENT_POST_INTERVAL_MS) {
    lastEventPostMs = millis();
    postHardwareEventSilently();
  }

  checkDoorState();
}
