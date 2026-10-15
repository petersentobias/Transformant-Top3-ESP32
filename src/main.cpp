#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <gfxfont.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
// Same GC9A01A SPI display wiring as capsule-radar-128/src/boards/board_lcd_128.h
static Arduino_DataBus *lcdBus = new Arduino_ESP32SPI(8, 9, 10, 11, 12);
static Arduino_GFX *lcd = new Arduino_GC9A01(lcdBus, 14, 0, true);

// Compact reference-inspired UI for the 240x240 round GC9A01 display.
// Firmware uses bitmap fonts, so visual typography will differ from HTML Canvas.
static constexpr uint16_t BG=0x0022, WHITE565=0xF7FF, MINT=0x6F77;
static constexpr uint16_t GOLD=0xFE80, SILVER=0xCE79, BRONZE=0xD3C9;
static String names[3]={"Väntar på data","-","-"};
static String amounts[3]={"-","-","-"};
static String periodLabel="Väntar på BC",updatedLabel="Ej hämtat";
static String apiKey,caPem,apiError="Ej hämtat";
static bool hasLiveData=false,staleData=false;
static unsigned long lastFetch=0;
static bool fetchAttempted=false;
static const char* apiUrl="https://transformant-top3-api.petersen-tobias.workers.dev/api/top3";
static String sek(int64_t v){String s=String(v),out;int n=0;for(int i=s.length()-1;i>=0;i--){if(n&&n%3==0)out=" "+out;out=String(s[i])+out;n++;}return out+" kr";}

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
 centered(periodLabel.c_str(),81,1,rgb(160,187,197),navy);
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
   smoothText(names[i].substring(0,18).c_str(),x+34,y+12,&FreeSans9pt7b,WHITE565);
   lcd->setFont(nullptr);
   label(amounts[i].c_str(),x+35,y+18,1,MINT,panel);
 }
 lcd->setFont(nullptr);
 lcd->drawFastHLine(65,198,110,rgb(38,71,82));
 label(updatedLabel.c_str(),52,205,1,rgb(162,188,197),navy);
 wifiBars(139,213);
 label(staleData?"Gammal":"WiFi",170,205,1,staleData?GOLD:MINT,navy);
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
void fetchTop3(){
 if(WiFi.status()!=WL_CONNECTED){apiError="Wi-Fi saknas";staleData=hasLiveData;return;}
 if(apiKey.length()<16){apiError="API-nyckel saknas";staleData=hasLiveData;return;}
 if(caPem.indexOf("-----BEGIN CERTIFICATE-----")<0){apiError="CA-certifikat saknas";staleData=hasLiveData;return;}
 WiFiClientSecure client;client.setCACert(caPem.c_str());client.setTimeout(12000);
 HTTPClient http;
 if(!http.begin(client,apiUrl)){apiError="HTTPS start misslyckades";staleData=hasLiveData;return;}
 http.setTimeout(12000);http.addHeader("X-Device-Key",apiKey);
 int status=http.GET();
 if(status!=200){apiError="HTTP "+String(status);http.end();staleData=hasLiveData;return;}
 JsonDocument d;DeserializationError e=deserializeJson(d,http.getStream());http.end();
 if(e||!d["top3"].is<JsonArray>()){apiError="Ogiltigt JSON";staleData=hasLiveData;return;}
 JsonArray a=d["top3"].as<JsonArray>();
 for(int i=0;i<3;i++){
   if(i<(int)a.size()){
     names[i]=a[i]["name"].as<String>();
     amounts[i]=sek(a[i]["amount_sek"].as<int64_t>());
   }else{names[i]="-";amounts[i]="-";}
 }
 String start=d["period_start"] | "";String end=d["period_end"] | "";
 if(start.length()==10&&end.length()==10)periodLabel=start.substring(8,10)+"-"+end.substring(8,10)+"/"+end.substring(5,7);
 time_t now=time(nullptr);struct tm t;
 if(now>1700000000&&localtime_r(&now,&t)){char buf[20];strftime(buf,sizeof(buf),"Uppd %H:%M",&t);updatedLabel=buf;}
 else updatedLabel="Uppdaterad";
 staleData=d["stale"] | false;apiError=staleData?"Cache från server":"OK";hasLiveData=true;drawDashboard();
}


void sendJson(int status, const String& body) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(status, "application/json; charset=utf-8", body);
}

void setupRoutes() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html; charset=utf-8", R"HTML(<!doctype html><html lang="sv"><meta name="viewport" content="width=device-width,initial-scale=1"><meta charset="utf-8"><title>TFM Top</title><style>body{font:16px system-ui;background:#10251b;color:#eef;margin:0;padding:24px;max-width:600px}section{background:#1c392b;border-radius:14px;padding:18px;margin:16px 0}button,input{padding:12px;border-radius:8px;border:0;margin:5px}button{background:#61e49b;color:#10251b}input{width:85%}pre{white-space:pre-wrap}</style><h1>Transformant Top 3</h1><p>ESP32 – första utvecklingsversionen</p><section><h2>Wi-Fi</h2><button onclick="scan()">Sök nätverk</button><div id="networks"></div><input id="ssid" placeholder="Nätverksnamn"><input id="password" type="password" placeholder="Wi-Fi-lösenord"><button onclick="save()">Spara och anslut</button><p id="result"></p></section><section><h2>Business Central API</h2><p>Nyckel och CA-certifikat lagras lokalt på enheten.</p><input id="key" type="password" autocomplete="off" placeholder="Ny DEVICE_API_KEY"><textarea id="ca" rows="5" style="width:95%" placeholder="PEM CA-certifikat (-----BEGIN CERTIFICATE-----)"></textarea><button onclick="saveApi()">Spara API-inställningar</button><p id="apiresult"></p></section><section><h2>Status</h2><pre id="status"></pre></section><script>async function scan(){let r=await fetch('/api/wifi/scan');let n=await r.json();document.querySelector('#networks').replaceChildren(...n.map(x=>{let b=document.createElement('button');b.textContent=x.ssid+' ('+x.rssi+' dBm)';b.onclick=()=>document.querySelector('#ssid').value=x.ssid;return b}))}async function save(){let r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:document.querySelector('#ssid').value,password:document.querySelector('#password').value})});document.querySelector('#result').textContent=await r.text()}async function saveApi(){let key=document.querySelector('#key').value,ca=document.querySelector('#ca').value;let r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({key,ca})});document.querySelector('#apiresult').textContent=await r.text();document.querySelector('#key').value=''}async function status(){try{document.querySelector('#status').textContent=JSON.stringify(await (await fetch('/api/status')).json(),null,2)}catch(e){}}status();</script></html>)HTML");
  });
  server.on("/api/status", HTTP_GET, []() {
    JsonDocument d;
    d["hostname"] = "tfmtop";
    d["wifi_connected"] = WiFi.status() == WL_CONNECTED;
    d["setup_ap"] = apMode;
    d["ip"] = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    d["firmware"] = "0.2.0-api";
    d["api_configured"] = apiKey.length()>=16;
    d["ca_configured"] = caPem.length()>100;
    d["api_status"] = apiError;
    d["has_live_data"] = hasLiveData;
    d["stale"] = staleData;
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
  server.on("/api/config",HTTP_POST,[](){
    JsonDocument d;if(deserializeJson(d,server.arg("plain"))){sendJson(400,"{\"error\":\"Invalid JSON\"}");return;}
    String key=d["key"] | "";String ca=d["ca"] | "";
    if(key.length()<16||key.length()>256||ca.length()<500||ca.indexOf("-----BEGIN CERTIFICATE-----")<0||ca.length()>8192){sendJson(400,"{\"error\":\"Invalid key or CA PEM\"}");return;}
    prefs.putString("apiKey",key);prefs.putString("caPem",ca);apiKey=key;caPem=ca;
    fetchAttempted=false;sendJson(200,"{\"status\":\"saved\"}");
  });
  server.begin();
}

void setup() {
  Serial.begin(115200);
  drawBootScreen();
  prefs.begin("tfmtop",false);
  ssid=prefs.getString("ssid","");
  password=prefs.getString("password","");
  apiKey=prefs.getString("apiKey","");caPem=prefs.getString("caPem","");
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3","pool.ntp.org","time.google.com");
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
  drawDashboard();
}

void loop() {
  server.handleClient();
  if(WiFi.status()==WL_CONNECTED&&(!fetchAttempted||millis()-lastFetch>=300000UL)){
    fetchAttempted=true;lastFetch=millis();fetchTop3();
  }
  if(!apMode && WiFi.status()!=WL_CONNECTED && millis()-lastReconnect>15000) {
    lastReconnect=millis();
    WiFi.reconnect();
  }
}
