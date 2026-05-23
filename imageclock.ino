/*
 * ESP32 — WiFi Slideshow + Device-Time Clock with DS1307 RTC
 * ─────────────────────────────────────────────────────────────
 * DEFAULT: Slideshow cycles JPEG images from SD every 4 s
 *
 * TIME SYNC:
 *   - DS1307 RTC (I2C: SDA=GPIO21, SCL=GPIO22) keeps time persistently
 *   - On boot: reads time from RTC module (survives power cycles)
 *   - When AP starts: loads RTC time (no waiting for web sync)
 *   - When user opens 192.168.4.1: device time auto-syncs to RTC
 *
 * TOUCH (TTP223, GPIO 32):
 *   single tap  → show clock + calendar for 10 s, then resume
 *
 * WiFi AP: always on from boot — "ESP32-Slideshow" / "12345678"
 *   → connect phone, open 192.168.4.1
 *   → page auto-sends device time to /settime (RTC updates)
 *   → upload / manage images
 *
 * Wiring:
 *   TFT_CS→5   TFT_DC→2   TFT_RST→4
 *   SD_CS→15   MOSI→23    MISO→19    SCK→18
 *   TTP223 OUT→GPIO 32
 *   DS1307: SDA→GPIO 21, SCL→GPIO 22 (I2C)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <JPEGDEC.h>
#include <time.h>
#include <Wire.h>
#include "RTClib.h"
#include "freertos/semphr.h"

// ── Pins ──────────────────────────────────────────────────────
#define TFT_CS    5
#define TFT_DC    2
#define TFT_RST   4
#define SD_CS     15
#define TOUCH_PIN 32

// ── Config ────────────────────────────────────────────────────
#define DISP_W        240
#define DISP_H        320
#define MAX_IMAGES    80
#define MAX_FNAME     32
#define SLIDE_DELAY   4000
#define WIFI_SSID     "ESP32-Slideshow"
#define WIFI_PASS     "12345678"
#define CLOCK_SHOW_MS 10000

// ── Forward declarations ──────────────────────────────────────
int  daysInMonth(int m, int y);
int  firstDay(int m, int y);
void drawGrid(struct tm& ti);
void drawTime(struct tm& ti);
void drawClockScreen();
void returnToSlideshow();
void showMsg(const char* line1, const char* line2, uint16_t color);
void onStationConnect(WiFiEvent_t event, WiFiEventInfo_t info);

// ── Clock colors ──────────────────────────────────────────────
#define C_BG     ST77XX_WHITE
#define C_TEXT   ST77XX_BLACK
#define C_TIME   0x001F
#define C_BANNER 0x03E0
#define C_TODAY  ST77XX_RED
#define C_SUN    ST77XX_RED

// ── Mode ──────────────────────────────────────────────────────
enum AppMode { MODE_SLIDESHOW, MODE_CLOCK };
volatile AppMode appMode     = MODE_SLIDESHOW;
unsigned long    clockShowAt = 0;

// ── Objects ───────────────────────────────────────────────────
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
WebServer       server(80);
JPEGDEC         jpeg;
RTC_DS1307      rtc;

SemaphoreHandle_t tftMutex = NULL;

// ── Slideshow state ───────────────────────────────────────────
bool          sdOK        = false;
char          imageList[MAX_IMAGES][MAX_FNAME];
int           imageCount  = 0;
int           curImg      = 0;
bool          slideshowOn = true;
unsigned long lastSlideMs = 0;
volatile bool uploading   = false;

// ── Clock state ───────────────────────────────────────────────
bool          is12h      = true;
bool          timeValid  = false;
struct tm     clk;
unsigned long lastTickMs = 0;
int           lastSec    = -1;
int           lastDay    = -1;

// ── Touch state ───────────────────────────────────────────────
unsigned long touchStart = 0;

// ── JPEG fit globals ──────────────────────────────────────────
float g_scale = 1.0f;
int   g_imgX  = 0, g_imgY = 0;


// ══════════════════════════════════════════════════════════════
// WiFi AP event — fires the moment a device connects
// Shows a prompt on the TFT so the user knows to open the browser
// ══════════════════════════════════════════════════════════════
void onStationConnect(WiFiEvent_t event, WiFiEventInfo_t info) {
    Serial.println("Device connected to AP");
    showMsg("Device connected!", "Open 192.168.4.1", ST77XX_CYAN);
}

// ══════════════════════════════════════════════════════════════
// HTML page — sends device time to /settime on every page load.
// This is the most reliable method: JS always runs in the real
// browser, unlike captive portal sandboxed webviews.
// Captive portal probes get a 302 redirect → this page.
// ══════════════════════════════════════════════════════════════
const char PAGE[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Slideshow</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#0a0a1a;color:#fff;font-family:sans-serif;padding:16px}
h1{color:#7c4dff;margin-bottom:16px;font-size:1.4em}
.card{background:#151528;border-radius:12px;padding:16px;margin-bottom:16px}
.card h2{color:#7c4dff;font-size:.8em;letter-spacing:.1em;margin-bottom:12px}
canvas{border:2px solid #1e1e38;display:block;margin:0 auto 12px}
input[type=file]{display:none}
.btn{display:block;width:100%;padding:12px;border:none;border-radius:8px;
  font-size:1em;cursor:pointer;margin-bottom:8px;font-weight:bold}
.btn-p{background:#7c4dff;color:#fff}
.btn-c{background:#00e5ff;color:#0a0a1a}
.btn-g{background:#00c853;color:#0a0a1a}
.btn:disabled{opacity:.45;cursor:not-allowed}
#prog{height:8px;background:#1e1e38;border-radius:4px;margin-bottom:8px;display:none}
#bar{height:8px;background:#00e5ff;border-radius:4px;width:0;transition:width .2s}
#status{color:#9e9ebc;font-size:.85em;text-align:center;margin-bottom:8px;min-height:1.2em}
#queue-info{color:#7c4dff;font-size:.8em;text-align:center;margin-bottom:6px;min-height:1em}
.thumb-row{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:10px;min-height:0}
.thumb-wrap{position:relative;width:60px;height:80px}
.thumb-wrap canvas{width:60px;height:80px;border-radius:4px;display:block}
.thumb-badge{position:absolute;top:2px;right:2px;background:#0a0a1a;border-radius:50%;
  width:18px;height:18px;font-size:11px;display:flex;align-items:center;justify-content:center}
.file-item{display:flex;align-items:center;padding:10px;
  background:#1e1e38;border-radius:8px;margin-bottom:6px}
.file-name{flex:1;font-size:.9em;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.file-size{color:#9e9ebc;font-size:.8em;margin:0 8px}
.del{background:#cf3232;color:#fff;border:none;border-radius:6px;
  padding:6px 10px;cursor:pointer;font-size:.8em}
.btn-r{background:#cf3232;color:#fff}
#tsbar{background:#0d2a1a;border-radius:8px;padding:7px 14px;margin-bottom:12px;
  font-size:.8em;color:#00e5a0;text-align:center;}
</style></head><body>
<h1>[CAM] ESP32 Slideshow</h1>
<div id="tsbar">Syncing clock from your device...</div>
<div class="card">
<h2>UPLOAD IMAGES</h2>
<div id="thumbs" class="thumb-row"></div>
<label class="btn btn-p" for="fi">[+] Pick Images (one or more)</label>
<input type="file" id="fi" accept="image/*" multiple>
<div id="queue-info"></div>
<div id="prog"><div id="bar"></div></div>
<div id="status">Select images to begin</div>
<button class="btn btn-c" id="send" disabled>[UP] Convert &amp; Upload All (240x320 JPEG)</button>
</div>
<div class="card">
<h2>SD CARD FILES</h2>
<button class="btn btn-g" onclick="loadList()">[R] Refresh List</button>
<div id="files"></div>
</div>
<script>
// ── Sync device time to ESP32 on every page load ──────────────
(function(){
  var now=new Date();
  var p=function(n){return('0'+n).slice(-2);};
  var ts=now.getFullYear()+'/'+p(now.getMonth()+1)+'/'+p(now.getDate())
         +' '+p(now.getHours())+':'+p(now.getMinutes())+':'+p(now.getSeconds());
  var bar=document.getElementById('tsbar');
  fetch('/settime?t='+encodeURIComponent(ts))
    .then(function(r){
      if(r.ok){ bar.textContent='Clock synced: '+ts; bar.style.color='#00e5a0'; }
      else { bar.textContent='Sync failed'; bar.style.color='#cf3232'; }
    })
    .catch(function(){ bar.textContent='Sync error'; bar.style.color='#cf3232'; });
})();

// ── Elements ──────────────────────────────────────────────────
const fi=document.getElementById('fi');
const st=document.getElementById('status');
const qi=document.getElementById('queue-info');
const bar=document.getElementById('bar');
const prog=document.getElementById('prog');
const sendBtn=document.getElementById('send');
const thumbsEl=document.getElementById('thumbs');

// ── Queue state ───────────────────────────────────────────────
let queue=[];
let busy=false;

// ── Render a 240x320 thumbnail into a canvas, return blob ─────
function processImage(file){
  return new Promise(function(resolve){
    const img=new Image(), url=URL.createObjectURL(file);
    img.onload=function(){
      const cv=document.createElement('canvas');
      cv.width=240; cv.height=320;
      const ctx=cv.getContext('2d');
      const sc=Math.min(240/img.width,320/img.height);
      const dw=Math.round(img.width*sc), dh=Math.round(img.height*sc);
      ctx.fillStyle='#000'; ctx.fillRect(0,0,240,320);
      ctx.drawImage(img,(240-dw)/2,(320-dh)/2,dw,dh);
      URL.revokeObjectURL(url);
      cv.toBlob(function(blob){ resolve({name:file.name.replace(/\.[^.]+$/,'')+'.jpg',blob:blob,thumb:cv}); },'image/jpeg',0.85);
    };
    img.src=url;
  });
}

// ── File picker ───────────────────────────────────────────────
fi.onchange=async function(){
  const files=Array.from(this.files); if(!files.length) return;
  st.textContent='Processing '+files.length+' image(s)...';
  sendBtn.disabled=true; queue=[];
  thumbsEl.innerHTML='';

  for(let i=0;i<files.length;i++){
    const res=await processImage(files[i]);
    queue.push({name:res.name,blob:res.blob});
    const wrap=document.createElement('div'); wrap.className='thumb-wrap';
    const tc=res.thumb.cloneNode(true); wrap.appendChild(tc);
    const badge=document.createElement('div'); badge.className='thumb-badge';
    badge.id='b'+i; badge.textContent=(i+1); wrap.appendChild(badge);
    thumbsEl.appendChild(wrap);
    st.textContent='Ready '+queue.length+'/'+files.length+'...';
  }

  const total=(queue.reduce((s,x)=>s+x.blob.size,0)/1024|0);
  st.textContent='Ready: '+queue.length+' image(s), ~'+total+' KB total';
  qi.textContent='';
  sendBtn.disabled=false;
};

// ── Upload queue one by one ───────────────────────────────────
sendBtn.onclick=async function(){
  if(!queue.length||busy) return;
  busy=true; sendBtn.disabled=true;
  prog.style.display='block';

  for(let i=0;i<queue.length;i++){
    const item=queue[i];
    qi.textContent='File '+(i+1)+' of '+queue.length+': '+item.name;
    const badge=document.getElementById('b'+i);

    await new Promise(function(resolve){
      const fd=new FormData(); fd.append('file',item.blob,item.name);
      const xhr=new XMLHttpRequest();
      xhr.upload.onprogress=function(e){
        if(e.lengthComputable){
          const p=e.loaded*100/e.total|0;
          bar.style.width=p+'%';
          st.textContent='Uploading '+item.name+' ... '+p+'%';
        }
      };
      xhr.onload=function(){
        bar.style.width='0';
        if(xhr.status==200){
          st.textContent='[OK] '+item.name;
          if(badge) badge.textContent='[OK]';
        } else {
          st.textContent='[ERR] '+item.name;
          if(badge) badge.textContent='!';
        }
        resolve();
      };
      xhr.onerror=function(){ st.textContent='[ERR] '+item.name; resolve(); };
      xhr.open('POST','/upload');
      xhr.send(fd);
    });
  }

  prog.style.display='none';
  qi.textContent='';
  st.textContent='All done! '+queue.length+' image(s) uploaded.';
  busy=false; queue=[];
  fi.value='';
  loadList();
};

// ── File list ─────────────────────────────────────────────────
function loadList(){
  fetch('/list').then(function(r){return r.json();}).then(function(files){
    const div=document.getElementById('files');
    if(!files.length){div.innerHTML='<p style="color:#9e9ebc;padding:8px">No files on SD card</p>';return;}
    div.innerHTML=files.map(function(f){
      return '<div class="file-item"><span class="file-name">'+f.name+'</span>'
            +'<span class="file-size">'+(f.size/1024).toFixed(1)+'KB</span>'
            +'<button class="del" onclick="del(\''+f.name+'\')">Delete</button></div>';
    }).join('');
  }).catch(function(){document.getElementById('files').innerHTML='<p style="color:#cf3232;padding:8px">Failed to load list</p>';});
}
function del(name){
  if(!confirm('Delete '+name+'?')) return;
  fetch('/delete?f='+encodeURIComponent(name)).then(function(r){return r.text();}).then(loadList);
}
loadList();
</script></body></html>)HTML";


// ══════════════════════════════════════════════════════════════
// SD helpers
// ══════════════════════════════════════════════════════════════
bool hasExt(const char* name, const char* ext) {
    int nl = strlen(name), el = strlen(ext);
    if (nl < el) return false;
    const char* s = name + nl - el;
    for (int i = 0; i < el; i++)
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)ext[i])) return false;
    return true;
}
bool isImage(const char* name) {
    return hasExt(name, ".jpg") || hasExt(name, ".jpeg");
}
void addToList(const char* path) {
    if (imageCount >= MAX_IMAGES) return;
    for (int i = 0; i < imageCount; i++)
        if (strcmp(imageList[i], path) == 0) return;
    strncpy(imageList[imageCount], path, MAX_FNAME - 1);
    imageList[imageCount][MAX_FNAME - 1] = '\0';
    imageCount++;
}
void scanSD() {
    imageCount = 0;
    File root = SD.open("/");
    if (!root) return;
    File f = root.openNextFile();
    while (f && imageCount < MAX_IMAGES) {
        if (!f.isDirectory() && isImage(f.name())) {
            snprintf(imageList[imageCount], MAX_FNAME, "/%s", f.name());
            imageCount++;
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();
}


// ══════════════════════════════════════════════════════════════
// JPEG / slideshow display
// ══════════════════════════════════════════════════════════════
void computeFit(int w, int h) {
    float sx = (float)DISP_W / w, sy = (float)DISP_H / h;
    g_scale = (sx < sy) ? sx : sy;
    if (g_scale > 1.0f) g_scale = 1.0f;
    g_imgX = (DISP_W - (int)(w * g_scale)) / 2;
    g_imgY = (DISP_H - (int)(h * g_scale)) / 2;
}
uint8_t* allocBuf(size_t sz) {
    if (psramFound()) { uint8_t* p = (uint8_t*)ps_malloc(sz); if (p) return p; }
    return (uint8_t*)malloc(sz);
}
int jpegDraw(JPEGDRAW* d) {
    tft.startWrite();
    for (int py = 0; py < d->iHeight; py++) {
        int dY = g_imgY + d->y + py;
        if (dY < 0 || dY >= DISP_H) continue;
        int startX = -1, count = 0;
        for (int px = 0; px < d->iWidth; px++) {
            int dX = g_imgX + d->x + px;
            if (dX < 0 || dX >= DISP_W) continue;
            if (startX < 0) startX = dX;
            count++;
        }
        if (count > 0) {
            int srcOff = py * d->iWidth + (startX - (g_imgX + d->x));
            tft.setAddrWindow(startX, dY, count, 1);
            tft.writePixels(&d->pPixels[srcOff], count);
        }
        yield();
    }
    tft.endWrite();
    return 1;
}
bool showJPEG(const char* path) {
    File f = SD.open(path, FILE_READ);
    if (!f) return false;
    size_t sz = f.size();
    uint8_t* buf = allocBuf(sz);
    if (!buf) { f.close(); return false; }
    f.read(buf, sz); f.close();
    if (!jpeg.openRAM(buf, (int)sz, jpegDraw)) { free(buf); return false; }
    computeFit(jpeg.getWidth(), jpeg.getHeight());
    tft.fillScreen(ST77XX_BLACK);
    jpeg.setPixelType(RGB565_LITTLE_ENDIAN);
    jpeg.decode(0, 0, 0);
    jpeg.close(); free(buf);
    return true;
}
void showOverlay(const char* txt) {
    tft.fillRect(0, DISP_H - 20, DISP_W, 20, ST77XX_BLACK);
    tft.setTextSize(1); tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(4, DISP_H - 14); tft.print(txt);
}


// ══════════════════════════════════════════════════════════════
// TFT status banner
// ══════════════════════════════════════════════════════════════
void showMsg(const char* line1, const char* line2 = "", uint16_t color = ST77XX_CYAN) {
    if (xSemaphoreTake(tftMutex, pdMS_TO_TICKS(200)) != pdTRUE) return;
    tft.fillRect(0, 0, DISP_W, 40, ST77XX_BLACK);
    tft.setTextColor(color); tft.setTextSize(2);
    tft.setCursor(10, 5); tft.print(line1);
    tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(10, 25); tft.print(line2);
    xSemaphoreGive(tftMutex);
}


// ══════════════════════════════════════════════════════════════
// Clock display
// ══════════════════════════════════════════════════════════════
void drawClockStatus(const char* msg) {
    tft.fillRect(0, 140, 240, 20, C_BG);
    tft.setTextColor(C_TEXT); tft.setTextSize(2);
    tft.setCursor(10, 140); tft.print(msg);
}

void drawTime(struct tm& ti) {
    char buf[16];
    if (is12h) strftime(buf, sizeof(buf), "%I:%M:%S %p", &ti);
    else        strftime(buf, sizeof(buf), "%H:%M:%S   ", &ti);
    tft.setTextSize(2);
    tft.setTextColor(C_TIME, C_BG);
    tft.setCursor((240 - 132) / 2, 35);
    tft.print(buf);
}

void drawGrid(struct tm& ti) {
    tft.fillRect(0,  0, 240,  30, C_BG);
    tft.fillRect(0, 60, 240, 260, C_BG);

    char buf[50];
    int16_t x1, y1; uint16_t w, h;

    strftime(buf, sizeof(buf), "%A", &ti);
    tft.setTextColor(C_TEXT); tft.setTextSize(2);
    tft.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((240 - w) / 2, 10); tft.print(buf);

    tft.fillRoundRect(10, 65, 220, 35, 8, C_BANNER);
    strftime(buf, sizeof(buf), "%B %Y", &ti);
    tft.setTextColor(ST77XX_WHITE);
    tft.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((240 - w) / 2, 75); tft.print(buf);

    tft.setTextSize(1);
    const char* days[] = {"Su","Mo","Tu","We","Th","Fr","Sa"};
    for (int i = 0; i < 7; i++) {
        tft.setCursor(15 + i * 32, 115);
        tft.setTextColor(i == 0 ? C_SUN : C_TEXT);
        tft.print(days[i]);
    }

    tft.setTextSize(2);
    int mon   = ti.tm_mon + 1;
    int year  = ti.tm_year + 1900;
    int today = ti.tm_mday;
    int dim   = daysInMonth(mon, year);
    int start = firstDay(mon, year);
    int yPos  = 135;

    for (int day = 1; day <= dim; day++) {
        int col  = (start + day - 1) % 7;
        int xPos = 12 + col * 32;
        if (day > 1 && col == 0) yPos += 30;
        if (day == today) {
            tft.fillRoundRect(xPos - 4, yPos - 4, 26, 24, 4, C_TODAY);
            tft.setTextColor(ST77XX_WHITE);
        } else if (col == 0) {
            tft.setTextColor(C_SUN);
        } else {
            tft.setTextColor(C_TEXT);
        }
        tft.setCursor(xPos, yPos);
        if (day < 10) tft.print(" ");
        tft.print(day);
    }
}

int daysInMonth(int m, int y) {
    if (m == 2) { bool leap = (y%4==0 && y%100!=0)||(y%400==0); return leap ? 29 : 28; }
    if (m==4||m==6||m==9||m==11) return 30;
    return 31;
}
int firstDay(int m, int y) {
    struct tm ti; memset(&ti, 0, sizeof(ti));
    ti.tm_year = y - 1900; ti.tm_mon = m - 1; ti.tm_mday = 1;
    mktime(&ti); return ti.tm_wday;
}

void drawClockScreen() {
    tft.invertDisplay(true);
    tft.fillScreen(C_BG);
    lastDay = -1; lastSec = -1;
    if (timeValid) {
        drawGrid(clk);
        drawTime(clk);
    } else {
        drawClockStatus("No time yet");
    }
}

void returnToSlideshow() {
    tft.invertDisplay(false);
    tft.fillScreen(ST77XX_BLACK);
    lastSlideMs = 0;
    appMode = MODE_SLIDESHOW;
}


// ══════════════════════════════════════════════════════════════
// Soft clock tick
// ══════════════════════════════════════════════════════════════
void tickClock() {
    if (!timeValid) return;
    if (millis() - lastTickMs >= 1000) {
        lastTickMs += 1000;   // advance by exactly 1000 ms to avoid drift accumulation
        time_t ep = mktime(&clk); ep++;
        struct tm* tp = localtime(&ep);
        clk = *tp;
    }
}


// ══════════════════════════════════════════════════════════════
// WiFi AP — always on, /settime endpoint sets clock from device
// ══════════════════════════════════════════════════════════════
File uploadFile;

void handleIndex()   { server.send_P(200, "text/html", PAGE); }

// /settime?t=YYYY/MM/DD HH:MM:SS  — called automatically by web page JS
void handleSetTime() {
    if (!server.hasArg("t")) { server.send(400, "text/plain", "Missing t"); return; }
    String ts = server.arg("t");   // e.g. "2026/05/14 22:35:10"
    struct tm nt; memset(&nt, 0, sizeof(nt));
    int yy=0, mo=0, dd=0, hh=0, mm=0, ss=0;
    int n = sscanf(ts.c_str(), "%d/%d/%d %d:%d:%d", &yy, &mo, &dd, &hh, &mm, &ss);
    if (n < 6 || yy < 2020) { server.send(400, "text/plain", "Bad format"); return; }
    nt.tm_year = yy - 1900; nt.tm_mon = mo - 1; nt.tm_mday = dd;
    nt.tm_hour = hh; nt.tm_min = mm; nt.tm_sec = ss; nt.tm_isdst = -1;
    mktime(&nt);
    clk        = nt;
    timeValid  = true;
    lastTickMs = millis();
    lastSec    = -1;
    Serial.printf("Time set from device: %02d:%02d:%02d %02d/%02d/%04d\n",
                  hh, mm, ss, dd, mo, yy);
    
    // Also update the DS1307 RTC with this time
    if (rtc.isrunning()) {
        rtc.adjust(DateTime(yy, mo, dd, hh, mm, ss));
        Serial.println("DS1307 updated with new time");
    }
    
    server.send(200, "text/plain", "OK");
}

void handleList() {
    String json = "[";
    File root = SD.open("/"); File f = root.openNextFile(); bool first = true;
    while (f) {
        if (!f.isDirectory()) {
            if (!first) json += ",";
            json += "{\"name\":\"" + String(f.name()) + "\",\"size\":" + String(f.size()) + "}";
            first = false;
        }
        f.close(); f = root.openNextFile();
    }
    root.close(); json += "]";
    server.send(200, "application/json", json);
}
void handleUpload()  { server.send(200, "text/plain", "OK"); scanSD(); }
void handleUploadFile() {
    HTTPUpload& up = server.upload();
    if (up.status == UPLOAD_FILE_START) {
        uploading = true;
        String p = "/" + up.filename;
        if (SD.exists(p.c_str())) SD.remove(p.c_str());
        uploadFile = SD.open(p.c_str(), FILE_WRITE);
        showMsg("Uploading...", up.filename.c_str(), ST77XX_CYAN);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) uploadFile.write(up.buf, up.currentSize);
    } else if (up.status == UPLOAD_FILE_END) {
        if (uploadFile) { uploadFile.close(); addToList(("/" + up.filename).c_str()); }
        uploading = false;
        showMsg("Upload OK!", up.filename.c_str(), ST77XX_GREEN);
    }
}
void handleDelete() {
    String path = "/" + server.arg("f");
    if (SD.exists(path.c_str())) { SD.remove(path.c_str()); scanSD(); server.send(200, "text/plain", "Deleted"); }
    else server.send(404, "text/plain", "Not found");
}

// Captive portal — all OS probes get a 302 redirect to the main page.
// The main page's JS then does the time sync reliably in the real browser.
void handleCaptive() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
}

void startAP() {
    WiFi.onEvent(onStationConnect, ARDUINO_EVENT_WIFI_AP_STACONNECTED);
    WiFi.softAP(WIFI_SSID, WIFI_PASS);
    server.on("/",                    handleIndex);
    server.on("/settime",             handleSetTime);
    server.on("/list",                handleList);
    server.on("/upload",  HTTP_POST,  handleUpload, handleUploadFile);
    server.on("/delete",              handleDelete);
    // Captive portal probe URLs (Android, iOS, Windows, Linux)
    server.on("/generate_204",        handleCaptive);   // Android
    server.on("/gen_204",             handleCaptive);   // Android alt
    server.on("/hotspot-detect.html", handleCaptive);   // Apple
    server.on("/ncsi.txt",            handleCaptive);   // Windows
    server.on("/connecttest.txt",     handleCaptive);   // Windows 10
    server.onNotFound(               handleCaptive);   // catch-all
    server.begin();
    Serial.println("AP started: " WIFI_SSID " → 192.168.4.1");
}



// ══════════════════════════════════════════════════════════════
// Touch detection — single tap only → show clock
// ══════════════════════════════════════════════════════════════
void checkTouch() {
    bool touched = (digitalRead(TOUCH_PIN) == HIGH);

    if (touched) {
        if (touchStart == 0) touchStart = millis();
    } else {
        if (touchStart > 0) {
            unsigned long held = millis() - touchStart;
            if (held < 2000) {   // treat anything under 2 s as a tap
                // Short tap → show clock
                if (xSemaphoreTake(tftMutex, pdMS_TO_TICKS(300)) == pdTRUE) {
                    appMode     = MODE_CLOCK;
                    clockShowAt = millis();
                    drawClockScreen();
                    xSemaphoreGive(tftMutex);
                }
            }
        }
        touchStart = 0;
    }
}


// ══════════════════════════════════════════════════════════════
// Slideshow task  (Core 1, priority 0)
// ══════════════════════════════════════════════════════════════
void slideshowTask(void* arg) {
    for (;;) {
        if (sdOK && appMode == MODE_SLIDESHOW &&
            slideshowOn && imageCount > 0 && !uploading &&
            millis() - lastSlideMs >= SLIDE_DELAY) {

            lastSlideMs = millis();

            if (xSemaphoreTake(tftMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
                bool ok = showJPEG(imageList[curImg]);
                if (ok) {
                    showOverlay(imageList[curImg] + 1);
                }
                xSemaphoreGive(tftMutex);
            }
            curImg = (curImg + 1) % imageCount;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}


// ══════════════════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    pinMode(TOUCH_PIN, INPUT);

    // ── Read RTC first — before any other init so boot time isn't lost ──
    Wire.begin(21, 22);
    if (rtc.begin()) {
        if (!rtc.isrunning()) {
            Serial.println("RTC stopped! Setting to compile time...");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
        DateTime now = rtc.now();
        struct tm rt; memset(&rt, 0, sizeof(rt));
        rt.tm_year = now.year() - 1900;
        rt.tm_mon  = now.month() - 1;
        rt.tm_mday = now.day();
        rt.tm_hour = now.hour();
        rt.tm_min  = now.minute();
        rt.tm_sec  = now.second();
        clk        = rt;
        timeValid  = true;
        lastTickMs = millis();
        Serial.printf("RTC read at boot: %02d:%02d:%02d %02d/%02d/%04d\n",
                      now.hour(), now.minute(), now.second(),
                      now.day(), now.month(), now.year());
    } else {
        Serial.println("RTC not found!");
    }

    tft.init(240, 320);
    delay(150);                  // let ST7789 controller settle before sending commands
    tft.setRotation(0);
    tft.invertDisplay(false);
    tft.invertDisplay(false);    // second call as insurance — controller ignores first if not ready
    tft.fillScreen(ST77XX_BLACK);

    tft.setTextColor(ST77XX_CYAN); tft.setTextSize(2);
    tft.setCursor(20, 80);  tft.print("WiFi Slideshow");
    tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(20, 110); tft.print("+ Device Clock");
    tft.setCursor(20, 130); tft.print("Initializing...");

    // SD
    if (!SD.begin(SD_CS)) {
        tft.setTextColor(ST77XX_RED); tft.setCursor(20, 150);
        tft.print("No SD — Clock only");
        sdOK = false;
    } else {
        sdOK = true;
        scanSD();
        tft.setTextColor(ST77XX_GREEN);
        tft.setCursor(20, 150); tft.printf("SD OK — %d images", imageCount);
    }

    if (psramFound()) {
        tft.setCursor(20, 168); tft.setTextColor(ST77XX_GREEN); tft.print("PSRAM: OK");
    }

    // Start WiFi AP immediately — always on from boot
    startAP();
    tft.setCursor(20, 186); tft.setTextColor(ST77XX_GREEN);
    tft.print("WiFi: " WIFI_SSID);
    tft.setCursor(20, 200); tft.setTextColor(ST77XX_WHITE);
    tft.print("192.168.4.1");
    tft.setCursor(20, 214); tft.print("Tap wire = Clock");

    if (!sdOK) {
        appMode     = MODE_CLOCK;
        clockShowAt = millis() - CLOCK_SHOW_MS;
    }

    lastSlideMs = millis();
    tftMutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(slideshowTask, "slideshow", 8192, NULL, 0, NULL, 1);
}


// ══════════════════════════════════════════════════════════════
// LOOP  (Core 0)
// ══════════════════════════════════════════════════════════════
void loop() {
    checkTouch();
    tickClock();
    server.handleClient();

    if (appMode == MODE_CLOCK) {
        if (xSemaphoreTake(tftMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (timeValid && clk.tm_mday != lastDay) {
                drawGrid(clk); lastDay = clk.tm_mday;
            }
            if (timeValid && clk.tm_sec != lastSec) {
                drawTime(clk); lastSec = clk.tm_sec;
            }
            xSemaphoreGive(tftMutex);
        }

        if (sdOK && millis() - clockShowAt >= CLOCK_SHOW_MS) {
            if (xSemaphoreTake(tftMutex, pdMS_TO_TICKS(300)) == pdTRUE) {
                returnToSlideshow();
                xSemaphoreGive(tftMutex);
            }
        }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}
