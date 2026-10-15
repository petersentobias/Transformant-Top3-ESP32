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

// RGB565 Transformant-inspired palette. Test figures are illustrative only.
static constexpr uint16_t BG=0x0000, NAVY=0x0864, CYAN=0x04DF;
static constexpr uint16_t WHITE565=0xFFFF, MUTED=0xA534;
static constexpr uint16_t GOLD=0xFEA0, SILVER=0xC638, BRONZE=0xB365;
static const char* demoNames[3]={"ANNA","ERIK","MARIA"};
static const char* demoAmounts[3]={"184 500","171 200","158 900"};

void drawDashboard() {
  lcd->fillScreen(BG);
  // Layered rings and blue highlights are clipped by the physical round panel.
  lcd->drawCircle(120,120,118,NAVY);
  lcd->drawCircle(120,120,114,0x00D5);
  lcd->drawCircle(120,120,110,NAVY);
  lcd->fillRoundRect(33,16,174,38,12,NAVY);
  lcd->drawRoundRect(33,16,174,38,12,CYAN);
  lcd->setTextSize(2);
  lcd->setTextColor(WHITE565,NAVY);
  lcd->setCursor(60,26);
  lcd->print("TRANSFORMANT");
  lcd->setTextSize(1);
  lcd->setTextColor(CYAN,BG);
  lcd->setCursor(92,64);
  lcd->print("TOP 3");
  lcd->drawFastHLine(44,79,152,CYAN);
  const uint16_t medal[3]={GOLD,SILVER,BRONZE};
  for(int i=0;i<3;i++){
    int y=90+i*40;
    lcd->fillRoundRect(26,y,188,35,9,0x10A3);
    lcd->drawRoundRect(26,y,188,35,9,NAVY);
    lcd->fillCircle(44,y+17,11,medal[i]);
    lcd->setTextSize(1);
    lcd->setTextColor(BG,medal[i]);
    lcd->setCursor(41,y+14);
    lcd->print(i+1);
    lcd->setTextColor(WHITE565,0x10A3);
    lcd->setCursor(62,y+6);
    lcd->print(demoNames[i]);
    lcd->setTextColor(CYAN,0x10A3);
    lcd->setCursor(62,y+20);
    lcd->print(demoAmounts[i]);
    lcd->setTextColor(MUTED,0x10A3);
    lcd->setCursor(164,y+20);
    lcd->print("SEK");
  }
  lcd->setTextColor(MUTED,BG);
  lcd->setCursor(75,219);
  lcd->print("DEMODATA");
  Serial.println("[lcd] Transformant Top 3 dashboard rendered");
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
