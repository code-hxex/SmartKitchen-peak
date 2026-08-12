// 18. Web data collection — Teachable Machine style dataset collector.
// Board becomes a hotspot with a live camera page. Type a label, click
// capture (or use auto-burst): each shot downloads to the browser as
// "<label>.<n>.jpg", ready for Edge Impulse upload.
//
// Connect to WiFi "XIAO_CAM" (pw 12345678), open http://192.168.4.1
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"

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

static const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>XIAO Data Collector</title>
<style>
body{font-family:sans-serif;background:#111;color:#eee;text-align:center;margin:0;padding:16px}
#cam{width:300px;height:300px;object-fit:cover;border-radius:8px;border:3px solid #444}
input,button{font-size:17px;padding:9px 14px;margin:5px;border-radius:8px;border:none}
input{width:130px;text-align:center}
button{background:#4c8bf5;color:#fff;cursor:pointer}
button.burst{background:#f5734c}
button.dl{background:#3fbf6f}
#count{font-size:14px;color:#9f9;margin:6px}
#gallery{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;max-width:680px;margin:12px auto}
.shot{position:relative}
.shot img{width:76px;height:76px;object-fit:cover;border-radius:6px;border:2px solid #555}
.shot .lb{position:absolute;left:0;bottom:0;right:0;background:rgba(0,0,0,.6);font-size:11px;
  border-radius:0 0 6px 6px;overflow:hidden;white-space:nowrap}
.shot .x{position:absolute;top:-7px;right:-7px;width:22px;height:22px;line-height:20px;
  background:#e04c4c;color:#fff;border-radius:50%;cursor:pointer;font-weight:bold;font-size:14px}
</style></head><body>
<h2>XIAO 데이터 수집기</h2>
<img id="cam" src="/jpg">
<div>
<input id="label" placeholder="라벨 (예: mug)" value="mug">
<button onclick="cap()">1장 캡처</button>
<button class="burst" onclick="burst()">20장 버스트</button>
</div>
<div>
<button class="dl" onclick="zipAll()">전체 ZIP 다운로드</button>
<button style="background:#666" onclick="clearAll()">전체 비우기</button>
</div>
<div id="count">0장 보관 중 - 캡처하면 아래 갤러리에 임시 보관됩니다. X로 지우고, ZIP으로 한꺼번에 받으세요.</div>
<div id="gallery"></div>
<script>
let shots=[];  // {label, u8, url}
setInterval(()=>{cam.src='/jpg?t='+Date.now()},700);

async function cap(){
  const label=document.getElementById('label').value.trim()||'unlabeled';
  const r=await fetch('/jpg?t='+Date.now());
  const u8=new Uint8Array(await r.arrayBuffer());
  const url=URL.createObjectURL(new Blob([u8],{type:'image/jpeg'}));
  shots.push({label,u8,url});
  render();
}
async function burst(){ for(let i=0;i<20;i++){ await cap(); await new Promise(r=>setTimeout(r,350)); } }

function del(i){ URL.revokeObjectURL(shots[i].url); shots.splice(i,1); render(); }
function clearAll(){ if(!confirm('전부 삭제할까요?'))return; shots.forEach(s=>URL.revokeObjectURL(s.url)); shots=[]; render(); }
function render(){
  document.getElementById('gallery').innerHTML=shots.map((s,i)=>
    `<div class="shot"><img src="${s.url}"><div class="lb">${s.label}</div><div class="x" onclick="del(${i})">x</div></div>`).join('');
  document.getElementById('count').textContent=shots.length+'장 보관 중';
}

// ---- minimal ZIP (store, no compression) ----
const CRC_T=(()=>{let t=[];for(let n=0;n<256;n++){let c=n;for(let k=0;k<8;k++)c=c&1?0xEDB88320^(c>>>1):c>>>1;t[n]=c>>>0}return t})();
function crc32(u8){let c=0xFFFFFFFF;for(let i=0;i<u8.length;i++)c=CRC_T[(c^u8[i])&0xFF]^(c>>>8);return (c^0xFFFFFFFF)>>>0}
function zipAll(){
  if(!shots.length){alert('보관된 사진이 없습니다');return}
  const enc=new TextEncoder(), parts=[], cdir=[]; let off=0, counts={};
  for(const s of shots){
    counts[s.label]=(counts[s.label]||0)+1;
    const name=enc.encode(s.label+'.'+counts[s.label]+'.jpg');
    const crc=crc32(s.u8), sz=s.u8.length;
    const lh=new DataView(new ArrayBuffer(30));
    lh.setUint32(0,0x04034b50,true);lh.setUint16(4,20,true);lh.setUint32(14,crc,true);
    lh.setUint32(18,sz,true);lh.setUint32(22,sz,true);lh.setUint16(26,name.length,true);
    parts.push(new Uint8Array(lh.buffer),name,s.u8);
    const cd=new DataView(new ArrayBuffer(46));
    cd.setUint32(0,0x02014b50,true);cd.setUint16(4,20,true);cd.setUint16(6,20,true);
    cd.setUint32(16,crc,true);cd.setUint32(20,sz,true);cd.setUint32(24,sz,true);
    cd.setUint16(28,name.length,true);cd.setUint32(42,off,true);
    cdir.push(new Uint8Array(cd.buffer),name);
    off+=30+name.length+sz;
  }
  let cdLen=0; cdir.forEach(p=>cdLen+=p.length);
  const end=new DataView(new ArrayBuffer(22));
  end.setUint32(0,0x06054b50,true);end.setUint16(8,shots.length,true);end.setUint16(10,shots.length,true);
  end.setUint32(12,cdLen,true);end.setUint32(16,off,true);
  const blob=new Blob([...parts,...cdir,new Uint8Array(end.buffer)],{type:'application/zip'});
  const a=document.createElement('a');
  a.href=URL.createObjectURL(blob); a.download='xiao_dataset.zip'; a.click();
  URL.revokeObjectURL(a.href);
}
</script></body></html>
)HTML";

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleJpg() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { server.send(503, "text/plain", "capture failed"); return; }
  uint8_t *jpg = NULL; size_t jpgLen = 0;
  bool ok = frame2jpg(fb, 85, &jpg, &jpgLen);
  esp_camera_fb_return(fb);
  if (!ok) { server.send(500, "text/plain", "jpeg failed"); return; }
  server.setContentLength(jpgLen);
  server.send(200, "image/jpeg", "");
  server.client().write(jpg, jpgLen);
  free(jpg);
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
  // AWB/AE warmup
  for (int i = 0; i < 10; i++) { camera_fb_t *w = esp_camera_fb_get(); if (w) esp_camera_fb_return(w); delay(60); }

  // standalone hotspot (AP) - clients join this SSID, page is always http://192.168.4.1
  WiFi.mode(WIFI_AP);
  WiFi.softAP("__AP_SSID__", "__AP_PASS__", __AP_CHANNEL__);
  Serial.print("AP started. Open http://");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/jpg", handleJpg);
  server.begin();
  Serial.println("Collector ready");
}

void loop() { server.handleClient(); }
