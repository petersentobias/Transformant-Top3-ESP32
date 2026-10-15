#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
// Same GC9A01A SPI display wiring as capsule-radar-128/src/boards/board_lcd_128.h
static Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(8, 9, 10, 11, 12);
static Arduino_GFX *lcd = new Arduino_GC9A01(lcdBus, 14, 0, true);

// Round 240x240 screen: content is placed within the circle's safe area.
// All measurements are explicit; no long title or amount can cross the rim.
static constexpr uint16_t BG=0x0000, PANEL=0x0863, PANEL_EDGE=0x1188;
static constexpr uint16_t BLUE=0x05BF, WHITE565=0xFFFF, SOFT=0xBDF7;
static constexpr uint16_t GOLD=0xFEA0, SILVER=0xC638, BRONZE=0xB365;
static const char* demoNames[3]={"ANNA","ERIK","MARIA"};
static const char* demoAmounts[3]={"184 500","171 200","158 900"};

void centeredLabel(const char* value,int centerX,int top,uint8_t size,uint16_t ink,uint16_t background) {
  // Built-in GFX 5x7 font advances by 6 pixels per character.
  int width=(int)strlen(value)*6*size;
  lcd->setTextSize(size);
  lcd->setTextColor(ink,background);
  lcd->setCursor(centerX-width/2,top);
  lcd->print(value);
}

void drawDashboard() {
  lcd->fillScreen(BG);
  // Thin framing ring, not a thick neon border competing with the figures.
  lcd->drawCircle(120,120,117,PANEL_EDGE);
  lcd->drawCircle(120,120,115,0x0092);

  // Brand header: deliberately compact, entirely inside the circle.
  centeredLabel("TRANSFORMANT",120,24,1,BLUE,BG);
  centeredLabel("TOP 3",120,41,3,WHITE565,BG);
  lcd->drawFastHLine(73,72,94,BLUE);
  centeredLabel("OKTOBER 2026",120,78,1,SOFT,BG);

  const uint16_t medals[3]={GOLD,SILVER,BRONZE};
  for(int i=0;i<3;i++){
    const int y=99+i*37;
    // Widths are reduced toward the lower arc to prevent edge clipping.
    const int x=(i==2)?39:30;
    const int w=(i==2)?162:180;
    lcd->fillRoundRect(x,y,w,32,9,PANEL);
    lcd->drawRoundRect(x,y,w,32,9,PANEL_EDGE);
    lcd->fillCircle(x+17,y+16,11,medals[i]);
    centeredLabel(i==0?"1":(i==1?"2":"3"),x+17,y+12,1,BG,medals[i]);
    lcd->setTextSize(1);
    lcd->setTextColor(WHITE565,PANEL);
    lcd->setCursor(x+34,y+5);
    lcd->print(demoNames[i]);
    lcd->setTextColor(BLUE,PANEL);
    lcd->setCursor(x+34,y+18);
    lcd->print(demoAmounts[i]);
    lcd->setTextColor(SOFT,PANEL);
    lcd->setCursor(x+w-28,y+18);
    lcd->print("kr");
  }
  centeredLabel("DEMODATA",120,216,1,SOFT,BG);
  Serial.println("[lcd] centered round dashboard rendered");
}

void drawBootScreen() {
  // Identical LCD initialization and PWM backlight method to Capsule Radar 1.28.
  pinMode(2,OUTPUT);
  ledcAttach(2,5000,8);
  ledcWrite(2,255);
  Serial.println("[lcd] Arduino_GFX GC9A01 begin");
  if(!lcd->begin(40000000)){
    Serial.println("[lcd] initialization failed");
    return;
  }
  lcd->fillScreen(0x001F);
  delay(250);
  drawDashboard();
}

WebServer server(80);
Preferences prefs;
String ssid;
String password;
bool apMode = false;
unsigned long lastReconnect = 0;

void sendJson(int status, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, "application/json; charset=utf-8", body);
}

void setupRoutes() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html; charset=utf-8", R"HTML(<!doctype html><html lang="sv"><meta name="viewport" content="width=device-width,initial-scale=1"><meta charset="utf-8"><title>TFM Top</title><style>body{font:16px system-ui;background:#10251b;color:#eef;margin:0;padding:24px;max-width:600px}section{background:#1c392b;border-radius:14px;padding:18px;margin:16px 0}button,input{padding:12px;border-radius:8px;border:0;margin:5px}button{background:#61e49b;color:#10251b}input{width:85%}pre{white-space:pre-wrap}</style><h1>Transformant Top 3</h1><p>ESP32 – första utvecklingsversionen</p><section><h2>Wi-Fi</h2><button onclick="scan()">Sök nätverk</button><div id="networks"></div><input id="ssid" placeholder="Nätverksnamn"><input id="password" type="password" placeholder="Wi-Fi-lösenord"><button onclick="save()">Spara och anslut</button><p id="result"></p></section><section><h2>Status</h2><pre id="status"></pre></section><script>async function scan(){let r=await fetch('/api/wifi/scan');let n=await r.json();document.querySelector('#networks').replaceChildren(...n.map(x=>{let b=document.createElement('button');b.textContent=x.ssid+' ('+x.rssi+' dBm)';b.onclick=()=>document.querySelector('#ssid').value=x.ssid;return b}))}async function save(){let r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:document.querySelector('#ssid').value,password:document.querySelector('#password').value})});document.querySelector('#result').textContent=await r.text()}async function status(){try{document.querySelector('#status').textContent=JSON.stringify(await (await fetch('/api/status')).json(),null,2)}catch(e){}}status();</script></html>)HTML");
  });
  server.on("/api/status", HTTP_GET, []() {
    JsonDocument d;
    d["hostname"] = "tfmtop";
    d["wifi_connected"] = WiFi.status() == WL_CONNECTED;
    d["setup_ap"] = apMode;
    d["ip"] = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    d["firmware"] = "0.1.0-dev";
    String out; serializeJson(d,out); sendJson(200,out);
  });
  server.on("/api/wifi/scan", HTTP_GET, []() {
    JsonDocument d;
    JsonArray a=d.to<JsonArray>();
    int n=WiFi.scanNetworks();
    for(int i=0;i<n;i++) {
      JsonObject o=a.add<JsonObject>();
      o["ssid"]=WiFi.SSID(i);
      o["rssi"]=WiFi.RSSI(i);
      o["secure"]=WiFi.encryptionType(i)!=WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    String out;serializeJson(d,out);sendJson(200,out);
  });
  server.on("/api/wifi", HTTP_POST, []() {
    JsonDocument d;
    if(deserializeJson(d,server.arg("plain"))) {sendJson(400,"{\"error\":\"Invalid JSON\"}");return;}
    String newSsid=d["ssid"] | "";
    String newPassword=d["password"] | "";
    if(newSsid.length()==0 || newSsid.length()>32) {sendJson(400,"{\"error\":\"Invalid SSID\"}");return;}
    prefs.putString("ssid",newSsid);
    prefs.putString("password",newPassword);
    sendJson(200,"{\"status\":\"saved; restarting\"}");
    delay(500);
    ESP.restart();
  });
  server.begin();
}

void setup() {
  Serial.begin(115200);
  drawBootScreen();
  prefs.begin("tfmtop",false);
  ssid=prefs.getString("ssid","");
  password=prefs.getString("password","");
  WiFi.mode(WIFI_AP_STA);
  if(ssid.length()) {
    WiFi.begin(ssid.c_str(),password.c_str());
    unsigned long started=millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-started<12000) delay(200);
  }
  if(WiFi.status()!=WL_CONNECTED) {
    apMode=true;
    WiFi.softAP("TFMTop-Setup");
    Serial.print("Setup AP IP: "); Serial.println(WiFi.softAPIP());
  } else {
    Serial.print("Connected IP: "); Serial.println(WiFi.localIP());
  }
  if(MDNS.begin("tfmtop")) MDNS.addService("http","tcp",80);
  setupRoutes();
}

void loop() {
  server.handleClient();
  if(!apMode && WiFi.status()!=WL_CONNECTED && millis()-lastReconnect>15000) {
    lastReconnect=millis();
    WiFi.reconnect();
  }
}
