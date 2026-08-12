// 19. Web inference viewer — live camera page with the Edge Impulse model's
// prediction drawn over the image: colored frame box + top label + per-class
// confidence bars. (Classification model = whole-frame box; per-object
// location boxes require a FOMO object-detection model.)
//
// Connect to WiFi "XIAO_CAM" (pw 12345678), open http://192.168.4.1
#include <__EI_PROJECT___inferencing.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"
#include "esp_heap_caps.h"

// tensor arena to PSRAM (MobileNet does not fit internal SRAM)
void *ei_malloc(size_t size) {
  void *p = heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM);
  if (!p) p = heap_caps_aligned_alloc(16, size, MALLOC_CAP_DEFAULT);
  return p;
}
void *ei_calloc(size_t nitems, size_t size) {
  void *p = ei_malloc(nitems * size);
  if (p) memset(p, 0, nitems * size);
  return p;
}
void ei_free(void *ptr) { heap_caps_free(ptr); }

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

WebServer server(80);
static float features[EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT];

static int get_feature_data(size_t offset, size_t length, float *out_ptr) {
  memcpy(out_ptr, features + offset, length * sizeof(float));
  return 0;
}

static const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>XIAO Live Inference</title>
<style>
body{font-family:sans-serif;background:#111;color:#eee;text-align:center;margin:0;padding:16px}
#wrap{position:relative;display:inline-block}
img{width:320px;height:320px;object-fit:cover;border-radius:8px;display:block}
#box{position:absolute;inset:0;border:5px solid #4c8bf5;border-radius:8px;pointer-events:none;
     transition:border-color .3s}
#tag{position:absolute;left:0;top:0;background:#4c8bf5;color:#fff;font-weight:bold;
     padding:4px 10px;border-radius:8px 0 8px 0;font-size:18px;transition:background .3s}
.bar{display:flex;align-items:center;margin:4px auto;width:320px;font-size:14px}
.bar span{width:90px;text-align:right;padding-right:8px}
.bar .track{flex:1;background:#333;border-radius:4px;height:16px;overflow:hidden}
.bar .fill{height:100%;background:#4c8bf5;width:0%;transition:width .3s}
.bar b{width:48px;text-align:left;padding-left:6px}
#lat{color:#888;font-size:13px;margin-top:8px}
</style></head><body>
<h2>🔍 XIAO 실시간 추론</h2>
<div id="wrap"><img id="cam" src="/jpg"><div id="box"></div><div id="tag">...</div></div>
<div id="bars"></div>
<div id="lat"></div>
<script>
const COLORS={person:'#4cf58b',cup_mug:'#f5c94c',pen:'#f5734c',chair:'#b04cf5',computer:'#4c8bf5'};
setInterval(()=>{cam.src='/jpg?t='+Date.now()},900);
async function tick(){
  try{
    const r=await fetch('/classify'); const d=await r.json();
    let html='';
    for(const [k,v] of Object.entries(d.scores)){
      const c=COLORS[k]||'#4c8bf5';
      html+=`<div class="bar"><span>${k}</span><div class="track"><div class="fill" style="width:${(v*100).toFixed(0)}%;background:${c}"></div></div><b>${(v*100).toFixed(0)}%</b></div>`;
    }
    document.getElementById('bars').innerHTML=html;
    const c=COLORS[d.label]||'#4c8bf5';
    document.getElementById('box').style.borderColor=c;
    const tag=document.getElementById('tag');
    tag.style.background=c;
    tag.textContent=`${d.label} ${(d.confidence*100).toFixed(0)}%`;
    document.getElementById('lat').textContent=`dsp ${d.dsp_ms}ms + nn ${d.nn_ms}ms`;
  }catch(e){}
  setTimeout(tick,300);
}
tick();
</script></body></html>
)HTML";

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleJpg() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { server.send(503, "text/plain", "no frame"); return; }
  uint8_t *jpg = NULL; size_t jpgLen = 0;
  bool ok = frame2jpg(fb, 85, &jpg, &jpgLen);
  esp_camera_fb_return(fb);
  if (!ok) { server.send(500, "text/plain", "jpeg failed"); return; }
  server.setContentLength(jpgLen);
  server.send(200, "image/jpeg", "");
  server.client().write(jpg, jpgLen);
  free(jpg);
}

void handleClassify() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { server.send(503, "application/json", "{\"error\":\"no frame\"}"); return; }

  // CW rotation correction (camera module sits rotated on the desk —
  // verified empirically: person 0.68 CW vs 0.33 none vs 0.03 CCW)
  const int W = EI_CLASSIFIER_INPUT_WIDTH, H = EI_CLASSIFIER_INPUT_HEIGHT;
  const uint16_t *src = (const uint16_t *)fb->buf;
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int sx = y * fb->width / H;
      int sy = fb->height - 1 - (x * fb->height / W);
      uint16_t px = src[sy * fb->width + sx];
      px = (px >> 8) | (px << 8);
      uint8_t r = ((px >> 11) & 0x1F) << 3;
      uint8_t g = ((px >> 5) & 0x3F) << 2;
      uint8_t b = (px & 0x1F) << 3;
      features[y * W + x] = (float)((r << 16) | (g << 8) | b);
    }
  }
  esp_camera_fb_return(fb);

  signal_t signal;
  signal.total_length = W * H;
  signal.get_data = &get_feature_data;
  ei_impulse_result_t result = {0};
  if (run_classifier(&signal, &result, false) != EI_IMPULSE_OK) {
    server.send(500, "application/json", "{\"error\":\"classifier\"}");
    return;
  }

  int best = 0;
  String json = "{\"scores\":{";
  for (int i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > result.classification[best].value) best = i;
    json += "\"" + String(ei_classifier_inferencing_categories[i]) + "\":" +
            String(result.classification[i].value, 3);
    if (i < EI_CLASSIFIER_LABEL_COUNT - 1) json += ",";
  }
  json += "},\"label\":\"" + String(ei_classifier_inferencing_categories[best]) + "\"";
  json += ",\"confidence\":" + String(result.classification[best].value, 3);
  json += ",\"dsp_ms\":" + String(result.timing.dsp);
  json += ",\"nn_ms\":" + String(result.timing.classification) + "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(2000);

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
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_240X240;
  config.pixel_format = PIXFORMAT_RGB565;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.fb_count = 2;

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed");
    while (true) delay(1000);
  }
  for (int i = 0; i < 10; i++) { camera_fb_t *w = esp_camera_fb_get(); if (w) esp_camera_fb_return(w); delay(60); }

  // standalone hotspot (AP) - clients join this SSID, page is always http://192.168.4.1
  WiFi.mode(WIFI_AP);
  WiFi.softAP("__AP_SSID__", "__AP_PASS__", __AP_CHANNEL__);
  Serial.print("AP started. Open http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/jpg", handleJpg);
  server.on("/classify", handleClassify);
  server.begin();
  Serial.println("Inference viewer ready");
}

void loop() { server.handleClient(); }
