/*
  Solar_PV — ESP8266 Admin MVP
  Target: LOLIN/WEMOS D1 R2 & mini, ESP8266 Arduino core 3.1.x
  TFT: ST7789 240x320, Adafruit_GFX + Adafruit_ST7789
  First stage only: TFT, Wi-Fi setup, browser admin, LittleFS settings.
  No ESP32, sensors, RTC, OLED, or IR support in this stage.
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>

// Existing TFT wiring. SPI SCLK/MOSI use the ESP8266 hardware SPI pins D5/D7.
static const uint8_t TFT_CS_PIN   = D8;
static const uint8_t TFT_DC_PIN   = D0;
static const uint8_t TFT_RST_PIN  = D6;

static const char* AP_SSID = "SolarPV-Setup";
static const char* AP_PASS = "SolarPV8266";
static const char* CONFIG_PATH = "/solarpv.cfg";

Adafruit_ST7789 tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;
ESP8266WebServer server(80);

struct AppConfig {
  String ssid;
  String password;
  String title;
  bool showPower;
  bool showVoltage;
  bool showCurrent;
  bool showBattery;
  uint8_t fontScale;
  int powerX, powerY;
  int voltageX, voltageY;
  int currentX, currentY;
  int batteryX, batteryY;
  uint8_t tftPage;
};

AppConfig cfg;
bool fsReady = false;
bool apActive = false;
bool webStarted = false;
unsigned long disconnectedSince = 0;
unsigned long restartAt = 0;
unsigned long lastStatusDraw = 0;
String lastStatusText = "";

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Solar_PV — админка</title>
<style>
:root{font-family:Arial,sans-serif;color:#eaf0f6;background:#111923}
*{box-sizing:border-box}body{margin:0}header{padding:18px 20px;background:#1c2938;border-bottom:1px solid #34465b}
h1{font-size:22px;margin:0 0 5px}header p{margin:0;color:#b8c6d5;font-size:14px}
main{max-width:1000px;margin:auto;padding:16px;display:grid;grid-template-columns:minmax(280px,1fr) minmax(260px,320px);gap:16px}
section{background:#1b2735;border:1px solid #34465b;border-radius:12px;padding:16px;margin-bottom:14px}
h2{font-size:18px;margin:0 0 14px}h3{font-size:15px;margin:14px 0 8px;color:#b9c9da}
label{display:block;font-size:14px;margin:10px 0 5px;color:#c8d5e2}
input[type=text],input[type=password],input[type=number],select{width:100%;padding:10px;border:1px solid #465c73;border-radius:7px;background:#101923;color:#fff;font-size:15px}
.checks{display:grid;grid-template-columns:1fr 1fr;gap:10px}.check{display:flex;align-items:center;gap:8px;background:#243447;padding:10px;border-radius:7px;font-size:14px}
.check input{width:18px;height:18px}.positions{display:grid;grid-template-columns:1fr 1fr;gap:10px}
button{border:0;border-radius:8px;padding:11px 14px;font-size:15px;font-weight:bold;cursor:pointer}
.primary{background:#49b78b;color:#081b14}.secondary{background:#344a61;color:#fff}
.actions{display:flex;gap:9px;flex-wrap:wrap;margin-top:14px}
.hint{font-size:12px;line-height:1.5;color:#9fb0c2}.status{padding:10px;border-radius:8px;background:#243447;color:#bfead8;font-size:13px;line-height:1.5;overflow-wrap:anywhere}
.preview-wrap{position:sticky;top:12px;align-self:start}
.screen{width:240px;height:320px;position:relative;overflow:hidden;margin:0 auto;background:#17212b;border:2px solid #64778a;border-radius:5px;color:#fff}
.screen-title{position:absolute;left:8px;top:8px;font-size:16px;font-weight:bold;white-space:nowrap;max-width:224px;overflow:hidden}
.screen-status{position:absolute;left:8px;top:32px;color:#73d6a0;font-size:10px}
.demo{position:absolute;background:#263746;border:1px solid #52616d;border-radius:5px;padding:5px;overflow:hidden}
.demo .name{font-size:10px;color:#aebdca;white-space:nowrap}
.demo .value{font-size:17px;color:#ffd166;font-weight:bold;white-space:nowrap}
#pPower{width:224px;height:58px}#pVoltage,#pCurrent{width:106px;height:58px}#pBattery{width:224px;height:58px}
.screen-footer{position:absolute;bottom:6px;left:8px;right:8px;border-top:1px solid #52616d;padding-top:4px;font-size:9px;color:#aebdca;display:flex;justify-content:space-between}
.hidden{display:none!important}
@media(max-width:760px){main{grid-template-columns:1fr}.preview-wrap{position:static;grid-row:1}.screen{margin:0 auto}}
</style>
</head>
<body>
<header><h1>Solar_PV — админка</h1><p>Настройка тестового экрана ESP8266 · TFT ST7789 240 × 320</p></header>
<main>
<div>
<section>
<h2>Оформление экрана</h2>
<label for="title">Заголовок TFT (до 18 символов, русский и английский)</label>
<input id="title" type="text" maxlength="18" value="SOLAR PV">
<label for="fontScale">Размер текста</label>
<select id="fontScale"><option value="1">Обычный</option><option value="2">Крупный</option></select>
<h3>Отображаемые блоки</h3>
<div class="checks">
<label class="check"><input id="showPower" type="checkbox"> Мощность PV</label>
<label class="check"><input id="showVoltage" type="checkbox"> Напряжение</label>
<label class="check"><input id="showCurrent" type="checkbox"> Ток</label>
<label class="check"><input id="showBattery" type="checkbox"> Аккумулятор</label>
</div>
<h3>Расположение блоков (X / Y)</h3>
<p class="hint">Координаты задаются в пикселях. Полноширинные блоки: X от 0 до 16. Половинные: X от 0 до 128. Y от 40 до 240.</p>
<div class="positions">
<div><label for="powerX">Мощность — X</label><input id="powerX" type="number" min="0" max="16"></div>
<div><label for="powerY">Мощность — Y</label><input id="powerY" type="number" min="40" max="230"></div>
<div><label for="voltageX">Напряжение — X</label><input id="voltageX" type="number" min="0" max="128"></div>
<div><label for="voltageY">Напряжение — Y</label><input id="voltageY" type="number" min="40" max="240"></div>
<div><label for="currentX">Ток — X</label><input id="currentX" type="number" min="0" max="128"></div>
<div><label for="currentY">Ток — Y</label><input id="currentY" type="number" min="40" max="240"></div>
<div><label for="batteryX">Аккумулятор — X</label><input id="batteryX" type="number" min="0" max="16"></div>
<div><label for="batteryY">Аккумулятор — Y</label><input id="batteryY" type="number" min="40" max="240"></div>
</div>
<div class="actions"><button class="primary" id="saveDisplay">Сохранить и применить</button><button class="secondary" id="defaults">Сбросить макет</button></div>
<h3>Страница дисплея</h3>
<p class="hint">Выберите страницу для физического дисплея. Автоматического переключения нет.</p>
<div class="actions"><button class="secondary" id="page1">Страница 1 · Показатели</button><button class="secondary" id="page2">Страница 2 · Wi-Fi</button></div>
<p class="status" id="pageStatus">Активна страница 1</p>
<p class="hint">Значения на экране демонстрационные. На первом этапе датчики и ESP32 не подключаются.</p>
</section>
<section>
<h2>Подключение к Wi-Fi</h2>
<div class="status" id="netStatus">Получение состояния устройства…</div>
<label for="ssid">Имя сети Wi-Fi (SSID)</label><input id="ssid" type="text" maxlength="32" autocomplete="off">
<label for="password">Пароль Wi-Fi</label><input id="password" type="password" maxlength="63" autocomplete="new-password">
<p class="hint">Пароль не отображается обратно в браузере. Если сеть уже настроена, оставьте поле пароля пустым, чтобы сохранить текущий пароль. После сохранения устройство перезапустится.</p>
<div class="actions"><button class="primary" id="saveWifi">Сохранить Wi-Fi и перезапустить</button></div>
</section>
</div>
<div class="preview-wrap">
<section>
<h2>Предпросмотр</h2>
<div class="screen" id="preview">
<div class="screen-title" id="prevTitle">SOLAR PV</div>
<div class="screen-status">DEMO DATA · Wi-Fi</div>
<div class="demo" id="pPower"><div class="name">Мощность PV</div><div class="value">DEMO W</div></div>
<div class="demo" id="pVoltage"><div class="name">Напряжение</div><div class="value">DEMO V</div></div>
<div class="demo" id="pCurrent"><div class="name">Ток</div><div class="value">DEMO A</div></div>
<div class="demo" id="pBattery"><div class="name">Аккумулятор</div><div class="value">DEMO %</div></div>
<div class="screen-footer"><span id="prevNetwork">Wi-Fi: —</span><span>Стр. 1</span></div>
</div>
<p class="hint">Предпросмотр приблизительный. Итоговый вид отображается на физическом TFT.</p>
</section>
</div>
</main>
<script>
const ids=['powerX','powerY','voltageX','voltageY','currentX','currentY','batteryX','batteryY'];
const defaults={title:'SOLAR PV',fontScale:1,showPower:true,showVoltage:true,showCurrent:true,showBattery:true,powerX:8,powerY:55,voltageX:8,voltageY:125,currentX:125,currentY:125,batteryX:8,batteryY:205};
function val(id){return document.getElementById(id).value}
function checked(id){return document.getElementById(id).checked}
function setVal(id,v){document.getElementById(id).value=v}
function updatePreview(){
 document.getElementById('prevTitle').textContent=val('title')||'SOLAR PV';
 document.getElementById('prevTitle').style.fontSize=(val('fontScale')==='2'?'19px':'16px');
 const map=[['power','pPower'],['voltage','pVoltage'],['current','pCurrent'],['battery','pBattery']];
 map.forEach(([key,el])=>{
  const node=document.getElementById(el);
  node.classList.toggle('hidden',!checked('show'+key[0].toUpperCase()+key.slice(1)));
  node.style.left=val(key+'X')+'px';node.style.top=val(key+'Y')+'px';
  node.querySelector('.value').style.fontSize=(val('fontScale')==='2'?'21px':'17px');
 });
}
function loadConfig(c){
 setVal('title',c.title);setVal('fontScale',c.fontScale);
 ['Power','Voltage','Current','Battery'].forEach(k=>document.getElementById('show'+k).checked=!!c['show'+k]);
 ids.forEach(id=>setVal(id,c[id]));
 setVal('ssid',c.ssid||'');setVal('password','');
 document.getElementById('pageStatus').textContent='Активна страница '+(c.tftPage===2?'2 · Wi-Fi':'1 · Показатели');
 document.getElementById('netStatus').textContent=c.networkStatus;
 document.getElementById('prevNetwork').textContent=c.networkLabel;
 updatePreview();
}
function collectDisplay(){
 const d={title:val('title'),fontScale:val('fontScale')};
 ['Power','Voltage','Current','Battery'].forEach(k=>d['show'+k]=checked('show'+k));
 ids.forEach(id=>d[id]=val(id));
 return d;
}
document.querySelectorAll('input,select').forEach(el=>el.addEventListener('input',updatePreview));
document.getElementById('saveDisplay').addEventListener('click',async()=>{
 const data=collectDisplay();const body=new URLSearchParams();
 Object.keys(data).forEach(k=>{if(typeof data[k]==='boolean'){if(data[k])body.set(k,'1')}else body.set(k,data[k])});
 try{const r=await fetch('/api/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const j=await r.json();alert(j.message);if(j.ok)loadConfig(await (await fetch('/api/config')).json());
 }catch(e){alert('Не удалось связаться с ESP8266. Проверьте подключение.')}
});
document.getElementById('defaults').addEventListener('click',()=>{
 loadConfig(Object.assign({},defaults,{ssid:val('ssid'),networkStatus:document.getElementById('netStatus').textContent,networkLabel:document.getElementById('prevNetwork').textContent}));
});
async function choosePage(page){
 try{
  const r=await fetch('/api/page',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({page:String(page)})});
  const j=await r.json();
  document.getElementById('pageStatus').textContent=j.message;
 }catch(e){document.getElementById('pageStatus').textContent='Не удалось связаться с ESP8266';}
}
document.getElementById('page1').addEventListener('click',()=>choosePage(1));
document.getElementById('page2').addEventListener('click',()=>choosePage(2));
document.getElementById('saveWifi').addEventListener('click',async()=>{
 const body=new URLSearchParams({ssid:val('ssid'),password:val('password')});
 try{const r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const j=await r.json();alert(j.message);if(j.ok)document.getElementById('password').value='';
 }catch(e){alert('Ответ не получен. Устройство может перезапускаться; подождите и откройте страницу снова.')}
});
fetch('/api/config').then(r=>r.json()).then(loadConfig).catch(()=>{document.getElementById('netStatus').textContent='Нет связи с устройством';});
</script>
</body>
</html>
)rawliteral";

String sanitizeTitle(String s) {
  s.trim();
  String out;
  out.reserve(54);
  // Preserve complete UTF-8 sequences for Russian and Latin titles.
  for (size_t i = 0; i < s.length() && out.length() < 54; i++) {
    uint8_t c = (uint8_t)s[i];
    if (c < 0x80) {
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_' ||
          c == '.' || c == ':' || c == '/') out += (char)c;
      continue;
    }
    uint8_t count = (c & 0xE0) == 0xC0 ? 2 :
                    ((c & 0xF0) == 0xE0 ? 3 :
                    ((c & 0xF8) == 0xF0 ? 4 : 0));
    if (count == 0 || i + count > s.length() || out.length() + count > 54) continue;
    bool valid = true;
    for (uint8_t j = 1; j < count; j++) {
      if (((uint8_t)s[i + j] & 0xC0) != 0x80) valid = false;
    }
    if (valid) {
      out += s.substring(i, i + count);
      i += count - 1;
    }
  }
  if (out.length() == 0) out = "SOLAR PV";
  return out;
}


int clampInt(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { out += '\\'; out += c; }
    else if (c == '\n' || c == '\r') { out += ' '; }
    else if ((uint8_t)c >= 0x20) { out += c; }
  }
  return out;
}

void setDefaults() {
  cfg.ssid = "";
  cfg.password = "";
  cfg.title = "SOLAR PV";
  cfg.showPower = true;
  cfg.showVoltage = true;
  cfg.showCurrent = true;
  cfg.showBattery = true;
  cfg.fontScale = 1;
  cfg.powerX = 8; cfg.powerY = 55;
  cfg.voltageX = 8; cfg.voltageY = 125;
  cfg.currentX = 125; cfg.currentY = 125;
  cfg.batteryX = 8; cfg.batteryY = 205;
  cfg.tftPage = 1;
}

void loadConfig() {
  setDefaults();
  if (!fsReady) return;
  File f = LittleFS.open(CONFIG_PATH, "r");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq < 1) continue;
    String key = line.substring(0, eq);
    String value = line.substring(eq + 1);
    if (key == "ssid") cfg.ssid = value;
    else if (key == "password") cfg.password = value;
    else if (key == "title") cfg.title = sanitizeTitle(value);
    else if (key == "showPower") cfg.showPower = value == "1";
    else if (key == "showVoltage") cfg.showVoltage = value == "1";
    else if (key == "showCurrent") cfg.showCurrent = value == "1";
    else if (key == "showBattery") cfg.showBattery = value == "1";
    else if (key == "fontScale") cfg.fontScale = value == "2" ? 2 : 1;
    else if (key == "powerX") cfg.powerX = clampInt(value.toInt(), 0, 16);
    else if (key == "powerY") cfg.powerY = clampInt(value.toInt(), 40, 230);
    else if (key == "voltageX") cfg.voltageX = clampInt(value.toInt(), 0, 128);
    else if (key == "voltageY") cfg.voltageY = clampInt(value.toInt(), 40, 240);
    else if (key == "currentX") cfg.currentX = clampInt(value.toInt(), 0, 128);
    else if (key == "currentY") cfg.currentY = clampInt(value.toInt(), 40, 240);
    else if (key == "batteryX") cfg.batteryX = clampInt(value.toInt(), 0, 16);
    else if (key == "batteryY") cfg.batteryY = clampInt(value.toInt(), 40, 240);
    else if (key == "tftPage") cfg.tftPage = value == "2" ? 2 : 1;
  }
  f.close();
}

bool saveConfig() {
  if (!fsReady) return false;
  File f = LittleFS.open(CONFIG_PATH, "w");
  if (!f) return false;
  f.println("ssid=" + cfg.ssid);
  f.println("password=" + cfg.password);
  f.println("title=" + sanitizeTitle(cfg.title));
  f.println(String("showPower=") + (cfg.showPower ? "1" : "0"));
  f.println(String("showVoltage=") + (cfg.showVoltage ? "1" : "0"));
  f.println(String("showCurrent=") + (cfg.showCurrent ? "1" : "0"));
  f.println(String("showBattery=") + (cfg.showBattery ? "1" : "0"));
  f.println(String("fontScale=") + String(cfg.fontScale));
  f.println("powerX=" + String(cfg.powerX));
  f.println("powerY=" + String(cfg.powerY));
  f.println("voltageX=" + String(cfg.voltageX));
  f.println("voltageY=" + String(cfg.voltageY));
  f.println("currentX=" + String(cfg.currentX));
  f.println("currentY=" + String(cfg.currentY));
  f.println("batteryX=" + String(cfg.batteryX));
  f.println("batteryY=" + String(cfg.batteryY));
  f.println("tftPage=" + String(cfg.tftPage));
  f.close();
  return true;
}

void drawCyrillicText(const String& text, int x, int baselineY, uint16_t color, bool large) {
  u8g2Fonts.setForegroundColor(color);
  u8g2Fonts.setFont(large ? u8g2_font_unifont_t_cyrillic : u8g2_font_6x13_t_cyrillic);
  u8g2Fonts.setCursor(x, baselineY);
  u8g2Fonts.print(text);
}

void drawCard(const char* label, const char* value, int x, int y, int w, int h) {
  tft.fillRoundRect(x, y, w, h, 5, 0x29C6);
  tft.drawRoundRect(x, y, w, h, 5, 0x5B6D);
  drawCyrillicText(String(label), x + 6, y + 19, 0xBDF7, false);
  drawCyrillicText(String(value), x + 6, y + 43, 0xFEA0, cfg.fontScale == 2);
}


String getTFTStatusText() {
  if (WiFi.status() == WL_CONNECTED) {
    return "Wi-Fi " + String(WiFi.RSSI()) + " dBm " + WiFi.localIP().toString();
  } else if (apActive) {
    return "Настройка Wi-Fi: 192.168.4.1";
  }
  return "Подключение к Wi-Fi...";
}

void drawTFTStatus(bool force = false) {
  String status = getTFTStatusText();
  if (!force && status == lastStatusText) return;

  // Clear only the footer status area; do not flash the whole display.
  tft.fillRect(0, 296, 240, 24, ST77XX_BLACK);
  drawCyrillicText(status, 8, 313, 0xBDF7, false);
  lastStatusText = status;
}

void renderTFT() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  if (cfg.tftPage == 2) {
    drawCyrillicText("СОСТОЯНИЕ WI-FI", 8, 25, ST77XX_WHITE, true);
    drawCyrillicText("Страница 2", 8, 45, 0x07E0, false);
    if (WiFi.status() == WL_CONNECTED) {
      drawCyrillicText("Сеть: " + cfg.ssid, 8, 78, 0xBDF7, false);
      drawCyrillicText("Сигнал: " + String(WiFi.RSSI()) + " dBm", 8, 103, 0xBDF7, false);
      drawCyrillicText("IP: " + WiFi.localIP().toString(), 8, 128, 0xBDF7, false);
    } else {
      drawCyrillicText("Роутер не подключён", 8, 78, 0xF800, false);
      drawCyrillicText("Сеть: " + cfg.ssid, 8, 103, 0xBDF7, false);
    }
    if (apActive) {
      drawCyrillicText("Точка настройки:", 8, 165, 0xBDF7, false);
      drawCyrillicText("SolarPV-Setup", 8, 188, 0xBDF7, false);
      drawCyrillicText("192.168.4.1", 8, 211, 0xBDF7, false);
    }
    tft.drawFastHLine(8, 294, 224, 0x5B6D);
    lastStatusText = "";
    drawTFTStatus(true);
    return;
  }

  drawCyrillicText(cfg.title, 8, 25, ST77XX_WHITE, true);
  drawCyrillicText("ДЕМО-ДАННЫЕ", 8, 43, 0x07E0, false);

  if (cfg.showPower) drawCard("Мощность PV", "DEMO W", cfg.powerX, cfg.powerY, 224, 58);
  if (cfg.showVoltage) drawCard("Напряжение", "DEMO V", cfg.voltageX, cfg.voltageY, 106, 58);
  if (cfg.showCurrent) drawCard("Ток", "DEMO A", cfg.currentX, cfg.currentY, 106, 58);
  if (cfg.showBattery) drawCard("Аккумулятор", "DEMO %", cfg.batteryX, cfg.batteryY, 224, 58);

  tft.drawFastHLine(8, 294, 224, 0x5B6D);
  lastStatusText = "";
  drawTFTStatus(true);
}


String makeConfigJson() {
  String j = "{";
  j += "\"title\":\"" + jsonEscape(cfg.title) + "\",";
  j += "\"fontScale\":" + String(cfg.fontScale) + ",";
  j += "\"showPower\":" + String(cfg.showPower ? "true" : "false") + ",";
  j += "\"showVoltage\":" + String(cfg.showVoltage ? "true" : "false") + ",";
  j += "\"showCurrent\":" + String(cfg.showCurrent ? "true" : "false") + ",";
  j += "\"showBattery\":" + String(cfg.showBattery ? "true" : "false") + ",";
  j += "\"powerX\":" + String(cfg.powerX) + ",\"powerY\":" + String(cfg.powerY) + ",";
  j += "\"voltageX\":" + String(cfg.voltageX) + ",\"voltageY\":" + String(cfg.voltageY) + ",";
  j += "\"currentX\":" + String(cfg.currentX) + ",\"currentY\":" + String(cfg.currentY) + ",";
  j += "\"batteryX\":" + String(cfg.batteryX) + ",\"batteryY\":" + String(cfg.batteryY) + ",";
  j += "\"ssid\":\"" + jsonEscape(cfg.ssid) + "\",";
  String net;
  String label;
  if (WiFi.status() == WL_CONNECTED) {
    net = "Подключено к роутеру: " + WiFi.localIP().toString() +
          " · уровень сигнала: " + String(WiFi.RSSI()) + " dBm";
    label = "Wi-Fi: " + String(WiFi.RSSI()) + " dBm";
  } else if (apActive) {
    net = "Точка настройки активна: SolarPV-Setup · адрес 192.168.4.1";
    label = "Режим настройки";
  } else {
    net = "Wi-Fi не подключён";
    label = "Wi-Fi: нет связи";
  }
  j += "\"networkStatus\":\"" + jsonEscape(net) + "\",";
  j += "\"networkLabel\":\"" + jsonEscape(label) + "\",";
  j += "\"tftPage\":" + String(cfg.tftPage) + "}";
  return j;
}

void startAccessPoint() {
  if (apActive) return;
  if (cfg.ssid.length() > 0) WiFi.mode(WIFI_AP_STA);
  else WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  apActive = WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println();
  Serial.println(F("Setup access point requested."));
  Serial.print(F("SSID: ")); Serial.println(AP_SSID);
  Serial.println(F("Open http://192.168.4.1"));
  renderTFT();
}

void stopAccessPointIfConnected() {
  // Keep SolarPV-Setup available even when the router is connected.
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Router IP: "));
    Serial.println(WiFi.localIP());
  }
}


void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleApiConfig() {
  server.send(200, "application/json; charset=utf-8", makeConfigJson());
}

void handleSaveDisplay() {
  if (server.hasArg("title")) cfg.title = sanitizeTitle(server.arg("title"));
  cfg.fontScale = server.arg("fontScale") == "2" ? 2 : 1;
  cfg.showPower = server.hasArg("showPower");
  cfg.showVoltage = server.hasArg("showVoltage");
  cfg.showCurrent = server.hasArg("showCurrent");
  cfg.showBattery = server.hasArg("showBattery");
  cfg.powerX = clampInt(server.arg("powerX").toInt(), 0, 16);
  cfg.powerY = clampInt(server.arg("powerY").toInt(), 40, 230);
  cfg.voltageX = clampInt(server.arg("voltageX").toInt(), 0, 128);
  cfg.voltageY = clampInt(server.arg("voltageY").toInt(), 40, 230);
  cfg.currentX = clampInt(server.arg("currentX").toInt(), 0, 128);
  cfg.currentY = clampInt(server.arg("currentY").toInt(), 40, 230);
  cfg.batteryX = clampInt(server.arg("batteryX").toInt(), 0, 16);
  cfg.batteryY = clampInt(server.arg("batteryY").toInt(), 40, 230);

  bool ok = saveConfig();
  renderTFT();
  String message = ok ? "Настройки экрана сохранены и применены." :
                        "Настройки применены, но не сохранены: файловая система недоступна.";
  server.send(ok ? 200 : 500, "application/json; charset=utf-8",
              String("{\"ok\":") + (ok ? "true" : "false") + ",\"message\":\"" + message + "\"}");
}

void handleSetPage() {
  uint8_t page = server.arg("page") == "2" ? 2 : 1;
  cfg.tftPage = page;
  if (!saveConfig()) {
    server.send(500, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Не удалось сохранить страницу в LittleFS.\"}");
    return;
  }
  renderTFT();
  server.send(200, "application/json; charset=utf-8",
              String("{\"ok\":true,\"message\":\"Выбрана страница ") + String(page) + "\"}");
}

void handleSaveWiFi() {
  String newSsid = server.arg("ssid");
  String newPassword = server.arg("password");
  newSsid.trim();
  if (newSsid.length() == 0 || newSsid.length() > 32) {
    server.send(400, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Укажите имя Wi-Fi сети (SSID).\"}");
    return;
  }
  if (newPassword.length() > 0) cfg.password = newPassword;
  if (cfg.password.length() > 63) cfg.password = cfg.password.substring(0, 63);
  cfg.ssid = newSsid;
  if (!saveConfig()) {
    server.send(500, "application/json; charset=utf-8",
                "{\"ok\":false,\"message\":\"Не удалось сохранить настройки. Проверьте LittleFS.\"}");
    return;
  }
  server.send(200, "application/json; charset=utf-8",
              "{\"ok\":true,\"message\":\"Настройки Wi-Fi сохранены. ESP8266 перезапустится; если роутер не подключится, точка SolarPV-Setup останется доступна.\"}");
  restartAt = millis() + 1200;
}

void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void setupWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleApiConfig);
  server.on("/api/save", HTTP_POST, handleSaveDisplay);
  server.on("/api/page", HTTP_POST, handleSetPage);
  server.on("/api/wifi", HTTP_POST, handleSaveWiFi);
  server.onNotFound(handleNotFound);
  server.begin();
  webStarted = true;
  Serial.println(F("Web admin started on port 80."));
}

void connectWiFiAtBoot() {
  if (cfg.ssid.length() == 0) {
    startAccessPoint();
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
  Serial.print(F("Connecting to configured Wi-Fi"));
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000UL) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    disconnectedSince = 0;
    Serial.print(F("Connected. IP: "));
    Serial.println(WiFi.localIP());
    startAccessPoint();  // Keep both router and setup AP available.
  } else {
    Serial.println(F("Wi-Fi connection failed; enabling setup AP."));
    startAccessPoint();
    disconnectedSince = millis();
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println(F("Solar_PV ESP8266 Admin MVP"));

  fsReady = LittleFS.begin();
  if (!fsReady) {
    Serial.println(F("LittleFS mount failed; trying first-time format."));
    if (LittleFS.format()) fsReady = LittleFS.begin();
  }
  if (!fsReady) Serial.println(F("ERROR: LittleFS unavailable; settings cannot be saved."));

  loadConfig();

  tft.init(240, 320);
  tft.setRotation(0);
  u8g2Fonts.begin(tft);
  u8g2Fonts.setFontMode(1);
  u8g2Fonts.setFontDirection(0);
  tft.fillScreen(ST77XX_BLACK);
  drawCyrillicText("СОЛАР PV", 8, 28, ST77XX_WHITE, true);
  drawCyrillicText("Запуск админки...", 8, 48, 0xBDF7, false);

  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  connectWiFiAtBoot();
  setupWebServer();
  renderTFT();
}

void loop() {
  if (webStarted) server.handleClient();

  if (restartAt != 0 && (long)(millis() - restartAt) >= 0) {
    delay(100);
    ESP.restart();
  }

  if (cfg.ssid.length() > 0) {
    if (WiFi.status() == WL_CONNECTED) {
      disconnectedSince = 0;
      stopAccessPointIfConnected();
    } else {
      if (disconnectedSince == 0) disconnectedSince = millis();
      if (!apActive && millis() - disconnectedSince >= 30000UL) startAccessPoint();
    }
  }

  // Check Wi-Fi status every 15 seconds, repaint only the footer when it changes.
  if (millis() - lastStatusDraw >= 15000UL) {
    lastStatusDraw = millis();
    drawTFTStatus();
  }
}
