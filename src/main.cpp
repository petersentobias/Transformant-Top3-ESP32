#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
// Same GC9A01A SPI display wiring as capsule-radar-128/src/boards/board_lcd_128.h
static Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(8, 9, 10, 11, 12);
static Arduino_GFX *lcd = new Arduino_GC9A01(lcdBus, 14, 0, true);

// Compact reference-inspired UI for the 240x240 round GC9A01 display.
// Firmware uses bitmap fonts, so visual typography will differ from HTML Canvas.
static constexpr uint16_t BG=0x0022, WHITE565=0xF7FF, MINT=0x6F77;
static constexpr uint16_t GOLD=0xFE80, SILVER=0xCE79, BRONZE=0xD3C9;
static const char* demoNames[3]={"Anna Andersson","Erik Svensson","Maria Lind"};
static const char* demoAmounts[3]={"184 500 kr","171 200 kr","158 900 kr"};
static uint16_t rgb(uint8_t r,uint8_t g,uint8_t b){return lcd->color565(r,g,b);}
static void label(const char* t,int x,int y,int scale,uint16_t fg,uint16_t bg){
 lcd->setTextSize(scale);lcd->setTextColor(fg,bg);lcd->setCursor(x,y);lcd->print(t);
}
static void centered(const char* t,int y,int scale,uint16_t fg,uint16_t bg){
 int w=(int)strlen(t)*6*scale;label(t,120-w/2,y,scale,fg,bg);
}
static void wifiBars(int x,int y){
 int dbm=WiFi.status()==WL_CONNECTED?WiFi.RSSI():-100;
 int n=dbm>=-55?4:dbm>=-67?3:dbm>=-78?2:dbm>=-88?1:0;
 for(int i=0;i<4;i++)lcd->fillRoundRect(x+i*7,y-(i+1)*3,5,(i+1)*3,1,i<n?MINT:rgb(42,76,82));
}
// Proportional FreeSans fonts: vector-like GFX glyphs at native pixel size.
// The GFX FreeFonts are monochrome (not true antialiasing), but much less blocky
// than doubling the built-in 5x7 font. No huge frame buffer is needed.
static void smoothText(const char* str,int x,int baseline,const GFXfont* font,uint16_t color){
 lcd->setFont(font);lcd->setTextSize(1);lcd->setTextColor(color);
 lcd->setCursor(x,baseline);lcd->print(str);
}
static void smoothCenter(const char* str,int baseline,const GFXfont* font,uint16_t color){
 lcd->setFont(font);lcd->setTextSize(1);
 int16_t x1,y1;uint16_t w,h;lcd->getTextBounds(str,0,baseline,&x1,&y1,&w,&h);
 smoothText(str,120-(int)w/2-x1,baseline,font,color);
}
void drawDashboard(){
 const uint16_t navy=rgb(1,9,14),panel=rgb(4,22,29);
 lcd->fillScreen(navy);
 // Restrained accent arcs rather than full-screen bright blue rings.
 lcd->drawCircle(120,120,115,rgb(9,49,62));
 lcd->drawCircle(120,120,113,rgb(4,32,42));
 lcd->drawCircle(120,120,109,rgb(3,31,37));
 smoothCenter("TRANSFORMANT",39,&FreeSansBold9pt7b,WHITE565);
 smoothCenter("OKTOBER 2026",69,&FreeSansBold9pt7b,WHITE565);
 lcd->setFont(nullptr);
 centered("1-15 okt",81,1,rgb(160,187,197),navy);
 const uint16_t metals[3]={GOLD,SILVER,BRONZE};
 const uint16_t shades[3]={rgb(112,68,9),rgb(85,103,120),rgb(120,67,34)};
 for(int i=0;i<3;i++){
   int y=95+i*32,x=25,w=190;
   lcd->fillRoundRect(x+1,y+3,w-2,28,8,rgb(0,4,7));
   lcd->fillRoundRect(x,y,w,28,8,panel);
   lcd->drawFastHLine(x+24,y+28,w-28,shades[i]);
   lcd->drawFastHLine(x+35,y+28,w-70,metals[i]);
   lcd->fillTriangle(x+13,y+1,x+23,y+1,x+18,y+14,shades[i]);
   lcd->fillCircle(x+18,y+14,12,rgb(0,7,11));
   lcd->fillCircle(x+18,y+14,10,metals[i]);
   lcd->drawCircle(x+18,y+14,8,rgb(255,225,165));
   lcd->setFont(nullptr);
   label(i==0?"1":i==1?"2":"3",x+15,y+10,1,rgb(15,26,32),metals[i]);
   smoothText(demoNames[i],x+34,y+12,&FreeSans9pt7b,WHITE565);
   lcd->setFont(nullptr);
   label(demoAmounts[i],x+35,y+18,1,MINT,panel);
 }
 lcd->setFont(nullptr);
 lcd->drawFastHLine(65,198,110,rgb(38,71,82));
 label("Uppd 14:27",65,205,1,rgb(162,188,197),navy);
 wifiBars(139,213);
 label("WiFi",170,205,1,MINT,navy);
 Serial.println("[lcd] proportional font dashboard rendered");
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
