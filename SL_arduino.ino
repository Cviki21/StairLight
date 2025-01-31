/***************************************************************
 * STEPS_FINAL_WIPEALL.ino
 * 
 * 1) Traka je OFF dok IR ne pokrene efekt.
 * 2) Ako je wipeAll=true => WipeIn + WipeOut, inače direktno
 *    OFF->EFFECT->onTime->OFF.
 * 3) effect !=0 => original boje, effect=0 => user boja (Solid).
 * 4) mA korekcija (gCorrectedCurrent).
 * 5) pushButton => manualOnOff => ignoriramo IR.
 ***************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <FastLED.h>
#include <ArduinoJson.h>  
#include <time.h>

// -----------------------------
// KONFIGURACIJE I GLOBALNE VAR
// -----------------------------

// IR pinovi
#define IR1_PIN 17
#define IR2_PIN 16
#define IR3_PIN 18
#define IR4_PIN 19

// Push button
#define PUSH_BUTTON_PIN 15

// WiFi fallback
String gWifiSsid = "HR House";
String gWifiPassword = "Njemacka1507";
bool   wifiConnected = false;
const char* AP_SSID  = "StairsAP";
const char* AP_PASS  = "12345678";

// LED pinovi
#define LED_PIN_UP   5
#define LED_PIN_DOWN 4
int    gNumLedsUp   = 300;
int    gNumLedsDown = 300;
CRGB*  ledsUp       = nullptr;
CRGB*  ledsDown     = nullptr;

int gMaxCurrent     = 2000;
int gCorrectedCurrent=2000;

// Efekti
// 0=Solid, 1=Confetti, 2=Rainbow, 3=Meteor, 4=FadeInOut, 5=Fire, 6=Lightning
uint8_t gEffect = 0;

// Wipe All (In+Out) umjesto gWipeIn/gWipeOut
bool     gWipeAll     = false;
uint16_t gWipeSpeedMs = 15;
uint16_t gOnTimeSec   = 60;

// Boja za Solid
uint8_t gColorR=255, gColorG=255, gColorB=255;

// Instalacija
String gInstallationType = "linija";
int    gBrojStepenica    = 12;
int    gBrojLedicaPoStepenici=30;
String gEfektKreniOd     = "sredina";
bool   gRotacijaStepenica= false;
// Moguće: String gStepeniceMode = "allAtOnce"; // ili "sequence"

// Day/Night
String gDayStartStr = "08:00";
String gDayEndStr   = "20:00";
int    gDayBrightnessPercent   = 100;
int    gNightBrightnessPercent = 30;

// Manual On/Off
bool gManualOnOff      = false;
bool gLastButtonState  = HIGH;
unsigned long gLastButtonDebounce= 0;
const unsigned long BTN_DEBOUNCE_MS= 250;

// IR debounce
unsigned long gIrLast[4] = {0,0,0,0};
const unsigned long IR_DEBOUNCE= 300;

// Preferences + WebServer
Preferences preferences;
WebServer server(80);

// TrackState
enum class TrackState {
  OFF=0,
  WIPE_IN,
  EFFECT,
  WIPE_OUT
};

struct Track {
  TrackState state= TrackState::OFF;
  bool reverse= false;
  unsigned long lastUpdate=0;
  uint16_t step= 0;
  unsigned long effectStartTime=0;
};

Track trackUp;
Track trackDown;

// -----------------------------------------------
//            FUNKCIJE: Preferences
// -----------------------------------------------
void loadPreferences(){
  preferences.begin("stairs", true);
  gWifiSsid  = preferences.getString("wifiSsid",  gWifiSsid);
  gWifiPassword= preferences.getString("wifiPass",gWifiPassword);

  gNumLedsUp   = preferences.getInt("ledsUp",   300);
  gNumLedsDown = preferences.getInt("ledsDown", 300);
  gMaxCurrent  = preferences.getInt("maxCur",   2000);

  gEffect= preferences.getUChar("effect", 0);
  gWipeAll= preferences.getBool("wipeAll", false);
  gWipeSpeedMs= preferences.getUInt("speed", 15);
  gOnTimeSec= preferences.getUInt("onTime", 60);

  uint32_t col= preferences.getUInt("color", 0xFFFFFF);
  gColorR= (col>>16)&0xFF;
  gColorG= (col>>8)&0xFF;
  gColorB=  col & 0xFF;

  gInstallationType= preferences.getString("instType","linija");
  gBrojStepenica= preferences.getInt("stepCount",12);
  gBrojLedicaPoStepenici= preferences.getInt("stepLeds",30);
  gEfektKreniOd= preferences.getString("stepKreni","sredina");
  gRotacijaStepenica= preferences.getBool("stepRot",false);
  // Moguce: gStepeniceMode= preferences.getString("stepMode","allAtOnce");

  gDayStartStr= preferences.getString("dayStart","08:00");
  gDayEndStr=   preferences.getString("dayEnd","20:00");
  gDayBrightnessPercent  = preferences.getInt("dayBr",100);
  gNightBrightnessPercent= preferences.getInt("nightBr",30);

  preferences.end();
}

void savePreferences(){
  preferences.begin("stairs", false);
  preferences.putString("wifiSsid", gWifiSsid);
  preferences.putString("wifiPass", gWifiPassword);

  preferences.putInt("ledsUp",   gNumLedsUp);
  preferences.putInt("ledsDown", gNumLedsDown);
  preferences.putInt("maxCur",   gMaxCurrent);

  preferences.putUChar("effect", gEffect);
  preferences.putBool("wipeAll", gWipeAll);
  preferences.putUInt("speed",   gWipeSpeedMs);
  preferences.putUInt("onTime",  gOnTimeSec);

  uint32_t col= ((uint32_t)gColorR<<16) | ((uint32_t)gColorG<<8) | gColorB;
  preferences.putUInt("color", col);

  preferences.putString("instType", gInstallationType);
  preferences.putInt("stepCount",   gBrojStepenica);
  preferences.putInt("stepLeds",    gBrojLedicaPoStepenici);
  preferences.putString("stepKreni",  gEfektKreniOd);
  preferences.putBool("stepRot",    gRotacijaStepenica);
  // preferences.putString("stepMode",  gStepeniceMode);

  preferences.putString("dayStart", gDayStartStr);
  preferences.putString("dayEnd",   gDayEndStr);
  preferences.putInt("dayBr",   gDayBrightnessPercent);
  preferences.putInt("nightBr",gNightBrightnessPercent);
  preferences.end();
}

// -----------------------------------------------
//            FUNKCIJE: WiFi, SPIFFS, ...
// -----------------------------------------------
void setupWiFi(){
  WiFi.mode(WIFI_STA);
  WiFi.begin(gWifiSsid.c_str(), gWifiPassword.c_str());
  unsigned long start= millis();
  while(WiFi.status()!=WL_CONNECTED && (millis()-start)<10000){
    delay(200);
  }
  if(WiFi.status()==WL_CONNECTED){
    wifiConnected=true;
    Serial.println("[WiFi] Connected => IP="+ WiFi.localIP().toString());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.println("[WiFi] AP Mode => IP="+ WiFi.softAPIP().toString());
  }
}

bool handleFileRead(String path){
  if(path.endsWith("/")) path+= "index.html";
  if(SPIFFS.exists(path)){
    File f= SPIFFS.open(path, "r");
    String ct= "text/html";
    if(path.endsWith(".css")) ct="text/css";
    if(path.endsWith(".js")) ct="application/javascript";
    server.streamFile(f,ct);
    f.close();
    return true;
  }
  return false;
}
void handleNotFound(){
  if(!handleFileRead(server.uri())){
    server.send(404,"text/plain","Not found");
  }
}

// -----------------------------------------------
//            FUNKCIJE: Time, Day/Night
// -----------------------------------------------
void setupTime(){
  configTime(3600,3600,"pool.ntp.org","time.nist.gov");
}
uint8_t parseHour(const String &hhmm){
  int idx= hhmm.indexOf(':');
  if(idx<0) return 8;
  return (uint8_t) hhmm.substring(0,idx).toInt();
}
uint8_t parseMin(const String &hhmm){
  int idx= hhmm.indexOf(':');
  if(idx<0) return 0;
  return (uint8_t) hhmm.substring(idx+1).toInt();
}
uint8_t getDayNightBrightness(){
  struct tm tinfo;
  if(!getLocalTime(&tinfo)){
    return map(gDayBrightnessPercent, 0,100, 0,255);
  }
  int cur= tinfo.tm_hour*60 + tinfo.tm_min;
  int ds= parseHour(gDayStartStr)*60 + parseMin(gDayStartStr);
  int de= parseHour(gDayEndStr)*60   + parseMin(gDayEndStr);

  if(ds<de){
    // normal
    if(cur>=ds && cur<de){
      return map(gDayBrightnessPercent, 0,100, 0,255);
    } else {
      return map(gNightBrightnessPercent,0,100,0,255);
    }
  } else {
    // prelazak iza ponoci
    if(cur>=ds || cur<de){
      return map(gDayBrightnessPercent,0,100,0,255);
    } else {
      return map(gNightBrightnessPercent,0,100,0,255);
    }
  }
}

// -----------------------------------------------
//            FUNKCIJE: IR
// -----------------------------------------------
void setupIRPins(){
  pinMode(IR1_PIN,INPUT_PULLUP);
  pinMode(IR2_PIN,INPUT_PULLUP);
  pinMode(IR3_PIN,INPUT_PULLUP);
  pinMode(IR4_PIN,INPUT_PULLUP);
}
void handleIrSensors(){
  if(gManualOnOff) return;
  unsigned long now= millis();
  int r1= digitalRead(IR1_PIN);
  int r2= digitalRead(IR2_PIN);
  int r3= digitalRead(IR3_PIN);
  int r4= digitalRead(IR4_PIN);

  if(r1==LOW && (now-gIrLast[0]>IR_DEBOUNCE)){
    gIrLast[0]= now;
    Serial.println("[IR] IR1 => Up normal");
    if(trackUp.state==TrackState::OFF){
      if(gWipeAll){
        trackUp.state= TrackState::WIPE_IN;
        trackUp.reverse= false;
        trackUp.lastUpdate= now;
        trackUp.step=0;
        trackUp.effectStartTime=0;
      } else {
        // direct => EFFECT
        trackUp.state= TrackState::EFFECT;
        trackUp.effectStartTime= now;
      }
    }
  }
  if(r2==LOW && (now-gIrLast[1]>IR_DEBOUNCE)){
    gIrLast[1]= now;
    Serial.println("[IR] IR2 => Up reverse");
    if(trackUp.state==TrackState::OFF){
      if(gWipeAll){
        trackUp.state= TrackState::WIPE_IN;
        trackUp.reverse= true;
        trackUp.lastUpdate= now;
        trackUp.step=0;
        trackUp.effectStartTime=0;
      } else {
        trackUp.state= TrackState::EFFECT;
        trackUp.effectStartTime= now;
      }
    }
  }
  if(r3==LOW && (now-gIrLast[2]>IR_DEBOUNCE)){
    gIrLast[2]= now;
    Serial.println("[IR] IR3 => Down normal");
    if(trackDown.state==TrackState::OFF){
      if(gWipeAll){
        trackDown.state= TrackState::WIPE_IN;
        trackDown.reverse= false;
        trackDown.lastUpdate= now;
        trackDown.step=0;
        trackDown.effectStartTime=0;
      } else {
        trackDown.state= TrackState::EFFECT;
        trackDown.effectStartTime= now;
      }
    }
  }
  if(r4==LOW && (now-gIrLast[3]>IR_DEBOUNCE)){
    gIrLast[3]= now;
    Serial.println("[IR] IR4 => Down reverse");
    if(trackDown.state==TrackState::OFF){
      if(gWipeAll){
        trackDown.state= TrackState::WIPE_IN;
        trackDown.reverse= true;
        trackDown.lastUpdate= now;
        trackDown.step=0;
        trackDown.effectStartTime=0;
      } else {
        trackDown.state= TrackState::EFFECT;
        trackDown.effectStartTime= now;
      }
    }
  }
}

// -----------------------------------------------
//            FUNKCIJE: push button
// -----------------------------------------------
void handlePushButton(){
  bool reading= digitalRead(PUSH_BUTTON_PIN);
  if(reading != gLastButtonState && (millis()-gLastButtonDebounce> BTN_DEBOUNCE_MS)){
    gLastButtonDebounce= millis();
    gLastButtonState= reading;
    if(reading==LOW){
      gManualOnOff= !gManualOnOff;
      Serial.print("[BUTTON] ManualOnOff => ");
      Serial.println(gManualOnOff? "ON" : "OFF");
      if(!gManualOnOff){
        trackUp.state= TrackState::OFF;
        trackDown.state= TrackState::OFF;
        for(int i=0;i<gNumLedsUp;i++){
          ledsUp[i]= CRGB::Black;
        }
        for(int i=0;i<gNumLedsDown;i++){
          ledsDown[i]= CRGB::Black;
        }
        FastLED.show();
      }
    }
  }
}

// -----------------------------------------------
//            FUNKCIJE: Wipe i Effect
// -----------------------------------------------
void updateWipeIn(Track &trk, CRGB* arr, int count){
  unsigned long now= millis();
  if(now- trk.lastUpdate< gWipeSpeedMs) return;
  trk.lastUpdate+= gWipeSpeedMs;

  int idx= trk.reverse? (count-1- trk.step) : trk.step;
  if(idx>=0 && idx< count){
    // Ako effect=0 => user boja, inače samo stavi user boju
    // (ili White) => ovdje je user boja
    arr[idx]= CRGB(gColorR,gColorG,gColorB);
    trk.step++;
  } else {
    // prelaz => EFFECT
    trk.state= TrackState::EFFECT;
    trk.effectStartTime= now;
    Serial.println("[WipeIn->EFFECT]");
  }
}

void updateEffectState(Track &trk){
  unsigned long now= millis();
  if((now- trk.effectStartTime)> (gOnTimeSec*1000UL)){
    if(gWipeAll){
      trk.state= TrackState::WIPE_OUT;
      trk.lastUpdate= now;
      trk.step=0;
      Serial.println("[EFFECT->WIPE_OUT]");
    } else {
      trk.state= TrackState::OFF;
      Serial.println("[EFFECT->OFF]");
    }
  }
}

void updateWipeOut(Track &trk, CRGB* arr, int count){
  unsigned long now= millis();
  if(now- trk.lastUpdate< gWipeSpeedMs) return;
  trk.lastUpdate+= gWipeSpeedMs;

  // obrnut smjer => ako trk.reverse=false => sad idx= (count-1- step)
  int idx= trk.reverse? trk.step : (count-1- trk.step);
  // ili suprotno, po želji
  // (ispod je samo primjer, moze se invertati)
  // int idx= (!trk.reverse)? (count-1- trk.step) : trk.step;

  if(idx>=0 && idx< count){
    arr[idx]= CRGB::Black;
    trk.step++;
  } else {
    trk.state= TrackState::OFF;
    Serial.println("[WipeOut->OFF]");
  }
}

void updateTrack(Track &trk, CRGB* arr, int count){
  switch(trk.state){
    case TrackState::OFF:
      // U OFF => gasi LED
      for(int i=0;i<count;i++){
        arr[i]= CRGB::Black;
      }
      break;
    case TrackState::WIPE_IN:
      updateWipeIn(trk, arr, count);
      break;
    case TrackState::EFFECT:
      // ne partial bojam => cekam onTime => updateEffectState
      updateEffectState(trk);
      break;
    case TrackState::WIPE_OUT:
      updateWipeOut(trk, arr, count);
      break;
  }
}

// Efekti
static uint8_t sHueUp=0, sHueDown=0;
static uint16_t sMetUp=0, sMetDown=0;
static int sFadeValUp=0, sFadeDirUp=1;
static int sFadeValDown=0, sFadeDirDown=1;

void doSolid(CRGB* arr, int count){
  for(int i=0;i<count;i++){
    arr[i]= CRGB(gColorR,gColorG,gColorB);
  }
}

void applyGlobalEffect(CRGB* arr, int count, bool isUp){
  switch(gEffect){
    case 1: // confetti
      for(int i=0;i<count;i++){
        arr[i].nscale8(200);
      }
      arr[random(count)]= CHSV(random8(),200,255);
      break;
    case 2: // rainbow
      for(int i=0;i<count;i++){
        if(isUp){
          arr[i]= CHSV(sHueUp + i*2,255,255);
        } else {
          arr[i]= CHSV(sHueDown+ i*2,255,255);
        }
      }
      if(isUp) sHueUp++; else sHueDown++;
      break;
    case 3: // Meteor
      for(int i=0;i<count;i++){
        arr[i].fadeToBlackBy(40);
      }
      if(isUp){
        arr[sMetUp]= CHSV(30,255,255);
        sMetUp++;
        if(sMetUp>=count) sMetUp=0;
      } else {
        arr[sMetDown]= CHSV(180,255,255);
        sMetDown++;
        if(sMetDown>=count) sMetDown=0;
      }
      break;
    case 4: // FadeInOut
    {
      if(isUp){
        sFadeValUp+=(5*sFadeDirUp);
        if(sFadeValUp>255){sFadeValUp=255;sFadeDirUp=-1;}
        if(sFadeValUp<0){sFadeValUp=0;sFadeDirUp=1;}
        CRGB c= CHSV(160,200,255);
        c.nscale8_video((uint8_t)sFadeValUp);
        for(int i=0;i<count;i++){
          arr[i]= c;
        }
      } else {
        sFadeValDown+=(5*sFadeDirDown);
        if(sFadeValDown>255){sFadeValDown=255;sFadeDirDown=-1;}
        if(sFadeValDown<0){sFadeValDown=0;sFadeDirDown=1;}
        CRGB c= CHSV(60,200,255);
        c.nscale8_video((uint8_t)sFadeValDown);
        for(int i=0;i<count;i++){
          arr[i]= c;
        }
      }
    }
    break;
    case 5: // fire
      for(int i=0;i<count;i++){
        uint8_t rr= random(150,256);
        uint8_t gg= random(0,100);
        arr[i]= CRGB(rr,gg,0);
      }
      break;
    case 6: // lightning
      for(int i=0;i<count;i++){
        arr[i]= CRGB::Black;
      }
      {
        int flashes= random(2,4);
        for(int f=0; f<flashes; f++){
          int start= random(count);
          int length= random(10,30);
          for(int i=0;i<length && (start+i)<count;i++){
            arr[start+i]= CRGB::White;
          }
          FastLED.show();
          delay(random(30,80));
          for(int i=0;i<length && (start+i)<count;i++){
            arr[start+i]= CRGB::Black;
          }
          FastLED.show();
          delay(random(60,150));
        }
      }
      break;
    default:
      // fallback => solid
      doSolid(arr,count);
      break;
  }
}

void applyTrackState(Track &trk, CRGB* arr, int count, bool isUp){
  updateTrack(trk, arr, count);

  // Ako smo u EFFECT => prikaži gEffect
  if(trk.state== TrackState::EFFECT){
    if(gEffect==0){
      // Solid => user boja
      doSolid(arr,count);
    } else {
      // ostali effect
      applyGlobalEffect(arr,count,isUp);
    }
    // onTime => prelazak => WipeOut/Off => rjesava updateEffectState
    unsigned long now= millis();
    if((now - trk.effectStartTime)> (gOnTimeSec*1000UL)){
      if(gWipeAll){
        trk.state= TrackState::WIPE_OUT;
        trk.lastUpdate= now;
        trk.step=0;
        // invert smjer
        trk.reverse= !trk.reverse;
        Serial.println("[EFFECT->WIPE_OUT]");
      } else {
        trk.state= TrackState::OFF;
        Serial.println("[EFFECT->OFF]");
      }
    }
  }
}

void applyTrack(Track &trk, CRGB* arr, int count, bool isUp){
  applyTrackState(trk, arr, count, isUp);
}

// -----------------------------------------------
//            WEB API
// -----------------------------------------------
void handleGetConfig(){
  DynamicJsonDocument doc(1024);
  doc["ledsUp"]   = gNumLedsUp;
  doc["ledsDown"] = gNumLedsDown;
  doc["maxCur"]   = gMaxCurrent;
  doc["effect"]   = gEffect;
  doc["colorR"]   = gColorR;
  doc["colorG"]   = gColorG;
  doc["colorB"]   = gColorB;
  doc["speed"]    = gWipeSpeedMs;
  doc["onTime"]   = gOnTimeSec;
  doc["dayStart"] = gDayStartStr;
  doc["dayEnd"]   = gDayEndStr;
  doc["dayBr"]    = gDayBrightnessPercent;
  doc["nightBr"]  = gNightBrightnessPercent;

  doc["installationType"]      = gInstallationType;
  doc["brojStepenica"]         = gBrojStepenica;
  doc["brojLedicaPoStepenici"] = gBrojLedicaPoStepenici;
  doc["efektKreniOd"]          = gEfektKreniOd;
  doc["rotacijaStepenica"]     = gRotacijaStepenica;
  // doc["stepeniceMode"]         = gStepeniceMode;

  // Umjesto wipeIn/wipeOut => wipeAll
  doc["wipeAll"] = gWipeAll;

  String out;
  serializeJson(doc, out);
  server.send(200,"application/json", out);
}

void handleSaveConfig(){
  if(!server.hasArg("plain")){
    server.send(400,"application/json","{\"status\":\"error\",\"msg\":\"No JSON\"}");
    return;
  }
  String body= server.arg("plain");

  DynamicJsonDocument doc(1024);
  DeserializationError err= deserializeJson(doc, body);
  if(err){
    server.send(400,"application/json","{\"status\":\"error\",\"msg\":\"Bad JSON\"}");
    return;
  }

  gNumLedsUp=  doc["ledsUp"] | gNumLedsUp;
  gNumLedsDown= doc["ledsDown"]| gNumLedsDown;
  gMaxCurrent=  doc["maxCur"] | gMaxCurrent;
  gEffect=      doc["effect"] | gEffect;
  gColorR=      doc["colorR"] | gColorR;
  gColorG=      doc["colorG"] | gColorG;
  gColorB=      doc["colorB"] | gColorB;
  gWipeSpeedMs= doc["speed"]  | gWipeSpeedMs;
  gOnTimeSec=   doc["onTime"] | gOnTimeSec;

  gDayStartStr= doc["dayStart"]| gDayStartStr;
  gDayEndStr=   doc["dayEnd"]  | gDayEndStr;
  gDayBrightnessPercent=   doc["dayBr"]   | gDayBrightnessPercent;
  gNightBrightnessPercent= doc["nightBr"] | gNightBrightnessPercent;

  gInstallationType= doc["installationType"] | gInstallationType;
  gBrojStepenica= doc["brojStepenica"] | gBrojStepenica;
  gBrojLedicaPoStepenici= doc["brojLedicaPoStepenici"] | gBrojLedicaPoStepenici;
  gEfektKreniOd= doc["efektKreniOd"] | gEfektKreniOd;
  gRotacijaStepenica= doc["rotacijaStepenica"] | gRotacijaStepenica;
  // gStepeniceMode= doc["stepeniceMode"] | gStepeniceMode;

  gWipeAll= doc["wipeAll"] | gWipeAll;

  savePreferences();
  server.send(200,"application/json","{\"status\":\"ok\"}");
}

void setupRoutes(){
  server.on("/",HTTP_GET, [](){
    if(!handleFileRead("/index.html")){
      server.send(404,"text/plain","Not found");
    }
  });
  server.on("/api/getConfig",HTTP_GET, handleGetConfig);
  server.on("/api/saveConfig",HTTP_POST,handleSaveConfig);
  server.onNotFound(handleNotFound);
}

// -----------------------------------------------
//            SETUP & LOOP
// -----------------------------------------------
void setup(){
  Serial.begin(115200);
  delay(200);
  Serial.println("-- STEPS_FINAL_WIPEALL START --");

  pinMode(PUSH_BUTTON_PIN, INPUT_PULLUP);

  loadPreferences();
  setupWiFi();
  if(!SPIFFS.begin(true)){
    Serial.println("[SPIFFS] mount failed");
  }
  setupRoutes();
  server.begin();
  Serial.println("[HTTP] server started.");

  setupIRPins();

  ledsUp=   new CRGB[gNumLedsUp];
  ledsDown= new CRGB[gNumLedsDown];
  FastLED.addLeds<WS2812B,LED_PIN_UP,GRB>(ledsUp,   gNumLedsUp);
  FastLED.addLeds<WS2812B,LED_PIN_DOWN,GRB>(ledsDown,gNumLedsDown);
  FastLED.clear(true);

  setupTime();
  Serial.println("-- setup() done --");
}

void loop(){
  server.handleClient();
  handlePushButton();

  if(!gManualOnOff){
    // normal rad
    handleIrSensors();
    // up
    applyTrack(trackUp,   ledsUp,   gNumLedsUp,   true);
    // down
    applyTrack(trackDown, ledsDown, gNumLedsDown, false);
  } else {
    // manual ON => ignoriramo IR
    trackUp.state= TrackState::OFF;
    trackDown.state= TrackState::OFF;

    if(gEffect==0){
      // solid user boja
      for(int i=0;i<gNumLedsUp;i++){
        ledsUp[i]= CRGB(gColorR,gColorG,gColorB);
      }
      for(int i=0;i<gNumLedsDown;i++){
        ledsDown[i]= CRGB(gColorR,gColorG,gColorB);
      }
    } else {
      // apply effect
      applyGlobalEffect(ledsUp, gNumLedsUp, true);
      applyGlobalEffect(ledsDown, gNumLedsDown, false);
    }
  }

  // dayNight
  uint8_t br= getDayNightBrightness();

  // mA korekcija
  if(gMaxCurrent>2000) gCorrectedCurrent=2000;
  else gCorrectedCurrent= gMaxCurrent;

  FastLED.setMaxPowerInVoltsAndMilliamps(5, gCorrectedCurrent);
  FastLED.setBrightness(br);
  FastLED.show();
}
