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
static uint32_t refreshSeconds=300;
static String periodStart="",periodEnd="",updatedAt="";
static int64_t topAmounts[3]={0,0,0};
static float topHours[3]={0,0,0};
static int sourceRows=0;
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
 static const char* months[]={"JANUARI","FEBRUARI","MARS","APRIL","MAJ","JUNI","JULI","AUGUSTI","SEPTEMBER","OKTOBER","NOVEMBER","DECEMBER"};
 String monthTitle="TOPP 3";
 if(periodEnd.length()==10){int m=periodEnd.substring(5,7).toInt();if(m>=1&&m<=12)monthTitle=String(months[m-1])+" "+periodEnd.substring(0,4);}
 smoothCenter(monthTitle.c_str(),69,&FreeSansBold9pt7b,WHITE565);
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
     topAmounts[i]=a[i]["amount_sek"].as<int64_t>();topHours[i]=a[i]["hours"].as<float>();amounts[i]=sek(topAmounts[i]);
   }else{names[i]="-";amounts[i]="-";topAmounts[i]=0;topHours[i]=0;}
 }
 String start=d["period_start"] | "";String end=d["period_end"] | "";
 periodStart=start;periodEnd=end;updatedAt=d["updated_at"] | "";sourceRows=d["source_rows"] | 0;
 if(start.length()==10&&end.length()==10)periodLabel=String(start.substring(8,10).toInt())+"/"+String(start.substring(5,7).toInt())+" - "+String(end.substring(8,10).toInt())+"/"+String(end.substring(5,7).toInt());
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
    server.send(200, "text/html; charset=utf-8", R"HTML(<!doctype html><html lang="sv"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Transformant Top 3</title>
<style>:root{color-scheme:dark;font-family:system-ui,Segoe UI,sans-serif}*{box-sizing:border-box}body{margin:0;background:#071b1c;color:#e8f7f4}main{max-width:1040px;margin:auto;padding:26px 18px 60px}header{display:flex;align-items:center;justify-content:space-between;gap:16px;flex-wrap:wrap}h1{margin:0;font-size:clamp(26px,4vw,38px)}h2{margin:0 0 16px;font-size:19px}p{color:#a6c3c0}.tag{border:1px solid #37645d;padding:7px 12px;border-radius:30px;color:#9ce9d0}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:18px;margin-top:22px}.card{background:#102e30;border:1px solid #234648;border-radius:18px;padding:22px}.wide{grid-column:1/-1}.muted{color:#9abbb7}.rows{display:grid;gap:12px}.person{display:grid;grid-template-columns:40px 1fr auto;align-items:center;gap:12px;background:#173a3c;padding:14px;border-radius:12px}.medal{display:grid;place-items:center;width:36px;height:36px;border-radius:50%;background:#e5bc66;color:#172629;font-weight:800}.person:nth-child(2) .medal{background:#becdd3}.person:nth-child(3) .medal{background:#c98c67}.person strong{display:block}.person small{color:#abc9c5}.amount{font-weight:750;color:#80e9c5;white-space:nowrap}label{display:block;margin:14px 0 6px;color:#b6d4d0}input,select,textarea{background:#071d20;color:#e8f7f4;border:1px solid #3b6464;border-radius:10px;padding:12px;width:100%;font:inherit}button{background:#75e5b3;color:#072521;border:0;border-radius:10px;padding:12px 17px;font:inherit;font-weight:700;cursor:pointer;margin-top:12px}button.secondary{background:#254d4c;color:#e8f7f4}button:disabled{opacity:.55}.stats{display:grid;grid-template-columns:repeat(2,1fr);gap:12px}.stat{padding:13px;background:#173a3c;border-radius:12px}.stat b{display:block;margin-top:5px;overflow-wrap:anywhere}.notice{min-height:20px;font-size:14px}.ok{color:#80e9b3}.bad{color:#ffae9c}.network{display:flex;justify-content:space-between;gap:10px;width:100%;text-align:left;background:#173a3c;margin-top:7px}.footer{font-size:12px;color:#87aaa6}details{margin-top:15px}summary{cursor:pointer;color:#b8dcd5}code{overflow-wrap:anywhere}@media(max-width:720px){.grid{grid-template-columns:1fr}.wide{grid-column:auto}.person{grid-template-columns:34px 1fr}.amount{grid-column:2}.stats{grid-template-columns:1fr 1fr}}</style></head>
<body><main><header><div><h1>TRANSFORMANT <span style="color:#83e8c2">TOP 3</span></h1><p>Business Central · Månadens debiteringsvärde</p></div><span class="tag" id="connection">Ansluter…</span></header>
<div class="grid"><section class="card wide"><div style="display:flex;justify-content:space-between;align-items:center;gap:12px;flex-wrap:wrap"><div><h2>Topplista</h2><span class="muted" id="period">Väntar på data</span></div><button id="refresh" onclick="refreshNow()">↻ Uppdatera nu</button></div><div class="rows" id="top" style="margin-top:20px"><p>Hämtar uppgifter…</p></div><p class="footer" id="dataInfo"></p></section>
<section class="card"><h2>Wi-Fi och nätverk</h2><div class="stats"><div class="stat"><span class="muted">Anslutet nätverk</span><b id="ssidNow">–</b></div><div class="stat"><span class="muted">Signal</span><b id="signal">–</b></div><div class="stat"><span class="muted">IP-adress</span><b id="ip">–</b></div><div class="stat"><span class="muted">Enhet</span><b>tfmtop.local</b></div></div><h3>Byt Wi-Fi</h3><button class="secondary" onclick="scan()">Sök tillgängliga nätverk</button><div id="networks"></div><label for="ssid">Nätverksnamn (SSID)</label><input id="ssid" maxlength="32" autocomplete="off" placeholder="Välj nätverk ovan eller skriv manuellt"><label for="password">Lösenord för det nya nätverket</label><input id="password" type="password" autocomplete="new-password"><button onclick="saveWifi()">Spara och byt nätverk</button><p class="notice" id="wifiMsg"></p><p class="footer">Enheten startar om vid nätverksbyte. Anslut till det nya nätverket för att öppna sidan igen.</p></section>
<section class="card"><h2>Uppdatering och API</h2><div class="stats"><div class="stat"><span class="muted">API-status</span><b id="apiStatus">–</b></div><div class="stat"><span class="muted">Senast uppdaterad</span><b id="updated">–</b></div><div class="stat"><span class="muted">API-nyckel</span><b id="apiConfigured">–</b></div><div class="stat"><span class="muted">HTTPS-certifikat</span><b id="caConfigured">–</b></div></div><label for="interval">Automatisk uppdatering</label><select id="interval"><option value="60">Varje minut</option><option value="120">Varannan minut</option><option value="300">Var 5:e minut</option><option value="600">Var 10:e minut</option><option value="900">Var 15:e minut</option><option value="1800">Var 30:e minut</option><option value="3600">Varje timme</option></select><button onclick="saveInterval()">Spara intervall</button><p class="notice" id="intervalMsg"></p><details><summary>Byt API-nyckel eller CA-certifikat</summary><p>Fyll i båda fälten för att ersätta befintliga uppgifter. Hemliga värden visas aldrig.</p><label for="key">DEVICE_API_KEY</label><input id="key" type="password" autocomplete="new-password"><label for="ca">CA-certifikat (PEM)</label><textarea id="ca" rows="5" placeholder="-----BEGIN CERTIFICATE-----"></textarea><button onclick="saveApi()">Spara API-inställningar</button><p class="notice" id="apiMsg"></p></details></section></div><p class="footer">Enhetens administrationssida är tillgänglig på det lokala nätverket. Begränsa åtkomsten till betrodda användare.</p></main>
<script>
const el=id=>document.getElementById(id);
const money=n=>new Intl.NumberFormat('sv-SE',{maximumFractionDigits:0}).format(n||0)+' kr';
const dateShort=s=>/^\d{4}-\d\d-\d\d$/.test(s||'')?Number(s.slice(8))+'/'+Number(s.slice(5,7)):'–';
function message(id,t,ok){el(id).textContent=t;el(id).className='notice '+(ok?'ok':'bad')}
async function req(path,options){let r=await fetch(path,options);let d=await r.json();if(!r.ok)throw Error(d.error||'HTTP '+r.status);return d}
function post(body){return {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}}
function render(d){
 el('connection').textContent=d.wifi_connected?'● Wi-Fi anslutet':'○ Offline';
 el('ssidNow').textContent=d.wifi_ssid||'Ej ansluten';el('ip').textContent=d.wifi_ip||d.ip||'–';
 let db=d.wifi_rssi;el('signal').textContent=d.wifi_connected?(db>=-55?'▂▄▆█':db>=-67?'▂▄▆':db>=-78?'▂▄':'▂')+' '+db+' dBm':'Ingen signal';
 el('period').textContent=dateShort(d.period_start)+' - '+dateShort(d.period_end);
 el('apiStatus').textContent=(d.stale?'Äldre data · ':'')+(d.api_status||'–');
 el('apiConfigured').textContent=d.api_configured?'Konfigurerad':'Saknas';
 el('caConfigured').textContent=d.ca_configured?'Installerat':'Saknas';
 el('updated').textContent=d.updated_at?new Date(d.updated_at).toLocaleString('sv-SE',{timeZone:'Europe/Stockholm',dateStyle:'short',timeStyle:'short'}):'Inte ännu';
 el('dataInfo').textContent=(d.source_rows||0)+' rader från Business Central · '+(d.has_live_data?'Data inläst':'Väntar på första hämtning')+(d.stale?' · Kan vara inaktuell':'');
 if(document.activeElement!==el('interval'))el('interval').value=String(d.refresh_seconds||300);
 el('top').replaceChildren(...(d.top3||[]).filter(x=>x.name&&x.name!=='-').map(x=>{let div=document.createElement('div');div.className='person';let m=document.createElement('span');m.className='medal';m.textContent=x.rank;let info=document.createElement('div');let n=document.createElement('strong');n.textContent=x.name;let h=document.createElement('small');h.textContent=Number(x.hours||0).toLocaleString('sv-SE')+' timmar';info.append(n,h);let amount=document.createElement('span');amount.className='amount';amount.textContent=money(x.amount_sek);div.append(m,info,amount);return div}));
 if(!el('top').children.length)el('top').textContent='Inga topplistedata ännu';
}
async function load(){try{render(await req('/api/status'))}catch(e){el('connection').textContent='Kan inte nå enheten'}}
async function refreshNow(){el('refresh').disabled=true;try{await req('/api/refresh',{method:'POST'});await load()}catch(e){alert('Uppdateringen misslyckades: '+e.message)}finally{el('refresh').disabled=false}}
async function scan(){el('networks').textContent='Söker…';try{let n=await req('/api/wifi/scan');el('networks').replaceChildren(...n.map(x=>{let b=document.createElement('button');b.className='network';b.textContent=x.ssid+' · '+x.rssi+' dBm'+(x.secure?' 🔒':'');b.onclick=()=>{el('ssid').value=x.ssid;el('password').focus()};return b}));if(!n.length)el('networks').textContent='Inga nätverk hittades'}catch(e){el('networks').textContent=e.message}}
async function saveWifi(){let ssid=el('ssid').value.trim();if(!ssid){message('wifiMsg','Välj ett nätverk först',false);return}if(!confirm('Byt till '+ssid+'? Enheten startar om.'))return;try{await req('/api/wifi',post({ssid,password:el('password').value}));message('wifiMsg','Sparat. Enheten startar om…',true);el('password').value=''}catch(e){message('wifiMsg',e.message,false)}}
async function saveInterval(){try{await req('/api/settings',post({refresh_seconds:Number(el('interval').value)}));message('intervalMsg','Intervallet är sparat',true);await load()}catch(e){message('intervalMsg',e.message,false)}}
async function saveApi(){let key=el('key').value,ca=el('ca').value;if(!key||!ca){message('apiMsg','Fyll i både nyckel och certifikat',false);return}try{await req('/api/config',post({key,ca}));el('key').value='';el('ca').value='';message('apiMsg','API-inställningarna är sparade',true);await load()}catch(e){message('apiMsg',e.message,false)}}
load();setInterval(load,15000);
</script></body></html>)HTML");
  });
  server.on("/api/status", HTTP_GET, []() {
    JsonDocument d;
    d["hostname"] = "tfmtop";
    d["wifi_connected"] = WiFi.status() == WL_CONNECTED;
    d["setup_ap"] = apMode;
    d["ip"] = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    d["firmware"] = "0.3.0-dashboard";
    d["wifi_ssid"] = WiFi.status()==WL_CONNECTED?WiFi.SSID():"";
    d["wifi_rssi"] = WiFi.status()==WL_CONNECTED?WiFi.RSSI():-100;
    d["wifi_ip"] = WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():"";
    d["refresh_seconds"] = refreshSeconds;
    d["last_attempt_seconds_ago"] = fetchAttempted?(millis()-lastFetch)/1000:0;
    d["period_start"] = periodStart;
    d["period_end"] = periodEnd;
    d["updated_at"] = updatedAt;
    d["source_rows"] = sourceRows;
    JsonArray top=d["top3"].to<JsonArray>();
    for(int i=0;i<3;i++){JsonObject item=top.add<JsonObject>();item["rank"]=i+1;item["name"]=names[i];item["amount_sek"]=topAmounts[i];item["hours"]=topHours[i];}
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
  server.on("/api/refresh",HTTP_POST,[](){
    if(WiFi.status()!=WL_CONNECTED){sendJson(503,"{\"error\":\"Wi-Fi offline\"}");return;}
    fetchAttempted=true;lastFetch=millis();fetchTop3();
    JsonDocument d;d["status"]=apiError;d["stale"]=staleData;String out;serializeJson(d,out);sendJson(hasLiveData?200:502,out);
  });
  server.on("/api/settings",HTTP_POST,[](){
    JsonDocument d;if(deserializeJson(d,server.arg("plain"))){sendJson(400,"{\"error\":\"Invalid JSON\"}");return;}
    int seconds=d["refresh_seconds"] | 300;
    if(seconds<60||seconds>3600){sendJson(400,"{\"error\":\"Interval must be 60-3600 seconds\"}");return;}
    refreshSeconds=seconds;prefs.putUInt("refresh",refreshSeconds);
    sendJson(200,"{\"status\":\"saved\"}");
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
  apiKey=prefs.getString("apiKey","");caPem=prefs.getString("caPem","");refreshSeconds=prefs.getUInt("refresh",300);if(refreshSeconds<60||refreshSeconds>3600)refreshSeconds=300;
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
  if(WiFi.status()==WL_CONNECTED&&(!fetchAttempted||millis()-lastFetch>=refreshSeconds*1000UL)){
    fetchAttempted=true;lastFetch=millis();fetchTop3();
  }
  if(!apMode && WiFi.status()!=WL_CONNECTED && millis()-lastReconnect>15000) {
    lastReconnect=millis();
    WiFi.reconnect();
  }
}
