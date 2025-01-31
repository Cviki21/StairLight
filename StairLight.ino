#include <Arduino.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_task_wdt.h>

// ====================================================================
// ENUMI I STRUKTURE
// ====================================================================
enum class TrackState {
  OFF = 0,
  WIPE_IN,
  EFFECT,
  WIPE_OUT
};

struct Track {
  TrackState state = TrackState::OFF;
  bool reverse = false;
  unsigned long lastUpdate = 0;
  uint16_t step = 0;
  unsigned long effectStartTime = 0;
};

struct StepSegment {
  TrackState state;
  bool reverse;
  unsigned long lastUpdate;
  uint16_t step;
  unsigned long effectStartTime;

  int indexStart;
  int indexCount;
};

// ====================================================================
// GLOBALNE PROMJENJIVE
// ====================================================================
Preferences preferences;
WebServer server(80);

#define IR1_PIN 17
#define IR2_PIN 16
#define IR3_PIN 18
#define IR4_PIN 19
#define PUSH_BUTTON_PIN 15

#define LED_PIN_UP   5
#define LED_PIN_DOWN 4

bool gEnableLed1 = true;
bool gEnableLed2 = true;

int gMaxCurrent = 2000;
int gNumLedsUp = 300;
int gNumLedsDown = 300;
CRGB* ledsUp = nullptr;
CRGB* ledsDown = nullptr;

int gBrojStepenica = 3;
int gBrojLedicaPoStepenici = 30;
int gTotalLedsStepenice = 0;
StepSegment* stepsArray = nullptr;

String gInstallationType = "linija";
String gEfektKreniOd = "sredina";
bool gRotacijaStepenica = false;
String gStepeniceMode = "allAtOnce";

uint8_t gEffect = 0;
bool gWipeAll = false;
uint16_t gWipeSpeedMs = 15;
uint16_t gOnTimeSec = 60;

uint8_t gColorR = 255, gColorG = 255, gColorB = 255;

String gDayStartStr = "08:00";
String gDayEndStr   = "20:00";
int gDayBrightnessPercent = 100;
int gNightBrightnessPercent = 30;

bool gManualOnOff = false;
bool gLastButtonState = HIGH;
unsigned long gLastButtonDebounce = 0;
const unsigned long BTN_DEBOUNCE_MS = 250;

Track trackUp;
Track trackDown;

unsigned long gIrLast[4] = {0, 0, 0, 0};
const unsigned long IR_DEBOUNCE = 300;

// Za efekte
static uint8_t sHueUp = 0, sHueDown = 0;
static uint16_t sMetUp = 0, sMetDown = 0;
static int sFadeValUp = 0, sFadeDirUp = 1;
static int sFadeValDown = 0, sFadeDirDown = 1;

// Sekvenca - stepenice
static bool  seqActive     = false;
static int   seqState      = 0;
static int   seqCurrentStep= 0;
static bool  seqForward    = true;
static unsigned long seqWaitStart = 0;

// ====================================================================
// WIFI MANAGER
// ====================================================================
void setupWiFi() {
  WiFiManager wifiManager;
  wifiManager.autoConnect("StairsAP");
}

// ====================================================================
// PREFERENCES
// ====================================================================
void loadPreferences() {
  preferences.begin("stairs", true);
  gNumLedsUp   = preferences.getInt("ledsUp", 300);
  gNumLedsDown = preferences.getInt("ledsDown", 300);
  gMaxCurrent  = preferences.getInt("maxCur", 2000);

  gEffect      = preferences.getUChar("effect", 0);
  gWipeAll     = preferences.getBool("wipeAll", false);
  gWipeSpeedMs = preferences.getUInt("speed", 15);
  gOnTimeSec   = preferences.getUInt("onTime", 60);

  uint32_t col = preferences.getUInt("color", 0xFFFFFF);
  gColorR = (col >> 16) & 0xFF;
  gColorG = (col >> 8) & 0xFF;
  gColorB = col & 0xFF;

  gInstallationType     = preferences.getString("instType", "linija");
  gBrojStepenica        = preferences.getInt("stepCount", 3);
  gBrojLedicaPoStepenici= preferences.getInt("stepLeds", 30);
  gEfektKreniOd         = preferences.getString("stepKreni", "sredina");
  gRotacijaStepenica    = preferences.getBool("stepRot", false);
  gStepeniceMode        = preferences.getString("stepMode", "allAtOnce");

  gDayStartStr           = preferences.getString("dayStart", "08:00");
  gDayEndStr             = preferences.getString("dayEnd", "20:00");
  gDayBrightnessPercent  = preferences.getInt("dayBr", 100);
  gNightBrightnessPercent= preferences.getInt("nightBr", 30);

  gEnableLed1 = preferences.getBool("enableLed1", true);
  gEnableLed2 = preferences.getBool("enableLed2", true);

  preferences.end();
}

void savePreferences() {
  preferences.begin("stairs", false);
  preferences.putInt("ledsUp", gNumLedsUp);
  preferences.putInt("ledsDown", gNumLedsDown);
  preferences.putInt("maxCur", gMaxCurrent);
  preferences.putUChar("effect", gEffect);
  preferences.putBool("wipeAll", gWipeAll);
  preferences.putUInt("speed", gWipeSpeedMs);
  preferences.putUInt("onTime", gOnTimeSec);

  uint32_t col = ((uint32_t)gColorR << 16) | ((uint32_t)gColorG << 8) | gColorB;
  preferences.putUInt("color", col);

  preferences.putString("instType", gInstallationType);
  preferences.putInt("stepCount", gBrojStepenica);
  preferences.putInt("stepLeds", gBrojLedicaPoStepenici);
  preferences.putString("stepKreni", gEfektKreniOd);
  preferences.putBool("stepRot", gRotacijaStepenica);
  preferences.putString("stepMode", gStepeniceMode);

  preferences.putString("dayStart", gDayStartStr);
  preferences.putString("dayEnd", gDayEndStr);
  preferences.putInt("dayBr", gDayBrightnessPercent);
  preferences.putInt("nightBr", gNightBrightnessPercent);

  preferences.putBool("enableLed1", gEnableLed1);
  preferences.putBool("enableLed2", gEnableLed2);

  preferences.end();
}

// ====================================================================
// FILE / HTTP
// ====================================================================
bool handleFileRead(String path) {
  if (path.endsWith("/")) path += "index.html";
  if (SPIFFS.exists(path)) {
    File f = SPIFFS.open(path, "r");
    String ct = "text/html";
    if (path.endsWith(".css")) ct = "text/css";
    if (path.endsWith(".js"))  ct = "application/javascript";
    server.streamFile(f, ct);
    f.close();
    return true;
  }
  return false;
}

void handleNotFound() {
  if (!handleFileRead(server.uri())) {
    server.send(404, "text/plain", "Not found");
  }
}

// ====================================================================
// TIME, DAY/NIGHT
// ====================================================================
void setupTime() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

uint8_t parseHour(const String& hhmm) {
  int idx = hhmm.indexOf(':');
  if (idx < 0) return 8;
  return (uint8_t)hhmm.substring(0, idx).toInt();
}

uint8_t parseMin(const String& hhmm) {
  int idx = hhmm.indexOf(':');
  if (idx < 0) return 0;
  return (uint8_t)hhmm.substring(idx + 1).toInt();
}

uint8_t getDayNightBrightness() {
  struct tm tinfo;
  if (!getLocalTime(&tinfo)) {
    return map(gDayBrightnessPercent, 0, 100, 0, 255);
  }
  int cur = tinfo.tm_hour * 60 + tinfo.tm_min;
  int ds = parseHour(gDayStartStr) * 60 + parseMin(gDayStartStr);
  int de = parseHour(gDayEndStr)   * 60 + parseMin(gDayEndStr);

  if (ds < de) {
    if (cur >= ds && cur < de) {
      return map(gDayBrightnessPercent, 0, 100, 0, 255);
    } else {
      return map(gNightBrightnessPercent, 0, 100, 0, 255);
    }
  } else {
    if (cur >= ds || cur < de) {
      return map(gDayBrightnessPercent, 0, 100, 0, 255);
    } else {
      return map(gNightBrightnessPercent, 0, 100, 0, 255);
    }
  }
}

// ====================================================================
// PUSH BUTTON
// ====================================================================
void handlePushButton() {
  bool reading = digitalRead(PUSH_BUTTON_PIN);
  if (reading != gLastButtonState && (millis() - gLastButtonDebounce > BTN_DEBOUNCE_MS)) {
    gLastButtonDebounce = millis();
    gLastButtonState = reading;
    if (reading == LOW) {
      gManualOnOff = !gManualOnOff;
      if (!gManualOnOff) {
        trackUp.state = TrackState::OFF;
        trackDown.state = TrackState::OFF;
        if (stepsArray) {
          for (int i = 0; i < gBrojStepenica; i++) {
            stepsArray[i].state = TrackState::OFF;
          }
        }
        if (ledsUp) {
          for (int i = 0; i < gNumLedsUp; i++) ledsUp[i] = CRGB::Black;
        }
        if (ledsDown) {
          for (int i = 0; i < gNumLedsDown; i++) ledsDown[i] = CRGB::Black;
        }
        if (gInstallationType == "stepenica" && ledsUp) {
          for (int i = 0; i < gTotalLedsStepenice; i++) ledsUp[i] = CRGB::Black;
        }
        FastLED.show();
      }
    }
  }
}

// ====================================================================
// EFEKTI
// ====================================================================
void doSolid(CRGB* arr, int count, uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < count; i++) {
    arr[i] = CRGB(r, g, b);
  }
}

void applyGlobalEffect(CRGB* arr, int count, bool isUp) {
  if (gEffect == 0) {
    doSolid(arr, count, gColorR, gColorG, gColorB);
    return;
  }
  switch (gEffect) {
    case 1: {
      for (int i = 0; i < count; i++) {
        arr[i].nscale8(200);
      }
      arr[random(count)] = CHSV(random8(), 200, 255);
    } break;
    case 2: {
      for (int i = 0; i < count; i++) {
        if (isUp) arr[i] = CHSV(sHueUp + i*2, 255, 255);
        else      arr[i] = CHSV(sHueDown + i*2, 255, 255);
      }
      if (isUp) sHueUp++;
      else      sHueDown++;
    } break;
    case 3: {
      for (int i = 0; i < count; i++) {
        arr[i].fadeToBlackBy(40);
      }
      if (isUp) {
        arr[sMetUp] = CHSV(30, 255, 255);
        sMetUp++;
        if (sMetUp >= count) sMetUp = 0;
      } else {
        arr[sMetDown] = CHSV(180, 255, 255);
        sMetDown++;
        if (sMetDown >= count) sMetDown = 0;
      }
    } break;
    case 4: {
      if (isUp) {
        sFadeValUp += (5 * sFadeDirUp);
        if (sFadeValUp > 255) { sFadeValUp = 255; sFadeDirUp = -1; }
        if (sFadeValUp < 0)   { sFadeValUp = 0;   sFadeDirUp = 1; }
        CRGB c = CHSV(160, 200, 255);
        c.nscale8_video((uint8_t)sFadeValUp);
        for (int i = 0; i < count; i++) arr[i] = c;
      } else {
        sFadeValDown += (5 * sFadeDirDown);
        if (sFadeValDown > 255) { sFadeValDown = 255; sFadeDirDown=-1; }
        if (sFadeValDown < 0)   { sFadeValDown = 0;   sFadeDirDown=1; }
        CRGB c = CHSV(60, 200, 255);
        c.nscale8_video((uint8_t)sFadeValDown);
        for (int i = 0; i < count; i++) arr[i] = c;
      }
    } break;
    case 5: {
      for (int i = 0; i < count; i++) {
        uint8_t rr = random(150,256);
        uint8_t gg = random(0,100);
        arr[i] = CRGB(rr, gg, 0);
      }
    } break;
    case 6: {
      for (int i = 0; i < count; i++) {
        arr[i] = CRGB::Black;
      }
      int flashes = random(2,4);
      for (int f=0; f<flashes; f++){
        int start = random(count);
        int length = random(10,30);
        for (int i=0;i<length && (start+i)<count;i++){
          arr[start+i] = CRGB::White;
        }
        FastLED.show();
        delay(random(30,80));
        for (int i=0;i<length && (start+i)<count;i++){
          arr[start+i] = CRGB::Black;
        }
        FastLED.show();
        delay(random(60,150));
      }
    } break;
    default: {
      doSolid(arr, count, gColorR, gColorG, gColorB);
    } break;
  }
}

// ====================================================================
// LINIJA
// ====================================================================
void updateWipeIn_line(Track& trk, CRGB* arr, int count, bool isUp) {
  unsigned long now = millis();
  if (now - trk.lastUpdate < gWipeSpeedMs) return;
  trk.lastUpdate += gWipeSpeedMs;

  CRGB wipeColor;
  if (gEffect == 0) {
    wipeColor = CRGB(gColorR, gColorG, gColorB);
  } else {
    wipeColor = CHSV(isUp ? sHueUp : sHueDown, 255, 255);
    if (isUp) sHueUp++;
    else      sHueDown++;
  }
  int idx = trk.reverse ? (count - 1 - trk.step) : trk.step;
  if (idx >= 0 && idx < count) {
    arr[idx] = wipeColor;
    trk.step++;
  } else {
    trk.state = TrackState::EFFECT;
    trk.effectStartTime = now;
  }
}

void updateEffect_line(Track& trk) {
  unsigned long now = millis();
  if ((now - trk.effectStartTime) >= (gOnTimeSec * 1000UL)) {
    if (gWipeAll) {
      trk.state = TrackState::WIPE_OUT;
      trk.lastUpdate = now;
      trk.step = 0;
    } else {
      trk.state = TrackState::OFF;
    }
  }
}

void updateWipeOut_line(Track& trk, CRGB* arr, int count) {
  unsigned long now = millis();
  if (now - trk.lastUpdate < gWipeSpeedMs) return;
  trk.lastUpdate += gWipeSpeedMs;

  int idx = trk.reverse ? trk.step : (count - 1 - trk.step);
  if (idx >= 0 && idx < count) {
    arr[idx] = CRGB::Black;
    trk.step++;
  } else {
    trk.state = TrackState::OFF;
  }
}

void updateTrack_line(Track& trk, CRGB* arr, int count, bool isUp) {
  switch(trk.state) {
    case TrackState::OFF: 
      break;
    case TrackState::WIPE_IN:
      updateWipeIn_line(trk, arr, count, isUp);
      break;
    case TrackState::EFFECT:
      applyGlobalEffect(arr, count, isUp);
      updateEffect_line(trk);
      break;
    case TrackState::WIPE_OUT:
      updateWipeOut_line(trk, arr, count);
      break;
  }
}

// ====================================================================
// STEPENICA
// ====================================================================
void initStepSegments() {
  gTotalLedsStepenice = gBrojStepenica * gBrojLedicaPoStepenici;
  if (stepsArray) {
    delete[] stepsArray;
    stepsArray = nullptr;
  }
  stepsArray = new StepSegment[gBrojStepenica];

  for (int i=0; i<gBrojStepenica; i++){
    stepsArray[i].state = TrackState::OFF;
    bool rev = false;
    if (gEfektKreniOd=="ruba" && gRotacijaStepenica && (i%2==1)) rev = true;
    stepsArray[i].reverse = rev;
    stepsArray[i].lastUpdate = 0;
    stepsArray[i].step = 0;
    stepsArray[i].effectStartTime = 0;
    stepsArray[i].indexStart = i*gBrojLedicaPoStepenici;
    stepsArray[i].indexCount = gBrojLedicaPoStepenici;
  }
  if (ledsUp) {
    delete[] ledsUp;
    ledsUp=nullptr;
  }
  ledsUp = new CRGB[gTotalLedsStepenice];
  FastLED.addLeds<WS2812B, LED_PIN_UP, GRB>(ledsUp, gTotalLedsStepenice);
  FastLED.clear(true);
}

void updateWipeIn_step(StepSegment& seg, CRGB* arr) {
  unsigned long now = millis();
  if (now - seg.lastUpdate < gWipeSpeedMs) return;
  seg.lastUpdate += gWipeSpeedMs;

  CRGB wipeColor = CRGB(gColorR, gColorG, gColorB);
  if (gEffect != 0) {
    wipeColor = CHSV(sHueUp, 255, 255);
    sHueUp++;
  }
  if (gEfektKreniOd=="sredina") {
    int mid = seg.indexCount/2;
    int leftIndex  = mid - seg.step -1;
    int rightIndex = mid + seg.step;
    if (leftIndex >= 0) arr[seg.indexStart + leftIndex] = wipeColor;
    if (rightIndex< seg.indexCount) arr[seg.indexStart + rightIndex] = wipeColor;
    seg.step++;
    if (leftIndex<0 && rightIndex>=seg.indexCount) {
      seg.state = TrackState::EFFECT;
      seg.effectStartTime = now;
    }
  } else {
    int idx = seg.reverse ? (seg.indexCount-1 - seg.step) : seg.step;
    if (idx>=0 && idx<seg.indexCount) {
      arr[seg.indexStart + idx] = wipeColor;
      seg.step++;
    } else {
      seg.state = TrackState::EFFECT;
      seg.effectStartTime = now;
    }
  }
}

void updateEffect_step(StepSegment& seg) {
  unsigned long now = millis();
  if ((now - seg.effectStartTime) >= (gOnTimeSec*1000UL)) {
    if (gWipeAll) {
      seg.state = TrackState::WIPE_OUT;
      seg.lastUpdate = now;
      seg.step = 0;
    } else {
      seg.state = TrackState::OFF;
    }
  }
}

void updateWipeOut_step(StepSegment& seg, CRGB* arr) {
  unsigned long now = millis();
  if (now - seg.lastUpdate < gWipeSpeedMs) return;
  seg.lastUpdate += gWipeSpeedMs;

  if (gEfektKreniOd=="sredina") {
    int mid = seg.indexCount/2;
    int leftIndex  = mid - seg.step -1;
    int rightIndex = mid + seg.step;
    if (leftIndex >= 0) arr[seg.indexStart + leftIndex] = CRGB::Black;
    if (rightIndex< seg.indexCount) arr[seg.indexStart + rightIndex] = CRGB::Black;
    seg.step++;
    if (leftIndex<0 && rightIndex>=seg.indexCount) {
      seg.state = TrackState::OFF;
    }
  } else {
    int idx = seg.reverse ? seg.step : (seg.indexCount-1 - seg.step);
    if (idx>=0 && idx<seg.indexCount) {
      arr[seg.indexStart + idx] = CRGB::Black;
      seg.step++;
    } else {
      seg.state = TrackState::OFF;
    }
  }
}

void applyEffectSegment(StepSegment& seg, CRGB* arr) {
  int start = seg.indexStart;
  int count = seg.indexCount;
  if (gEffect == 0) {
    for(int i=0; i<count; i++){
      arr[start + i] = CRGB(gColorR, gColorG, gColorB);
    }
  } else {
    static CRGB temp[2048];
    if (count>2048) return;
    for(int i=0; i<count; i++){
      temp[i] = CRGB::Black;
    }
    applyGlobalEffect(temp, count, true);
    for(int i=0; i<count; i++){
      arr[start + i] = temp[i];
    }
  }
}

void updateSegment(StepSegment& seg, CRGB* arr) {
  switch(seg.state) {
    case TrackState::OFF: 
      break;
    case TrackState::WIPE_IN:
      updateWipeIn_step(seg, arr);
      break;
    case TrackState::EFFECT:
      applyEffectSegment(seg, arr);
      updateEffect_step(seg);
      break;
    case TrackState::WIPE_OUT:
      updateWipeOut_step(seg, arr);
      break;
  }
}

void updateAllStepSegments() {
  for (int i=0; i<gBrojStepenica; i++){
    updateSegment(stepsArray[i], ledsUp);
  }
}

// ====================================================================
// SEQ - FORWARD/REVERSE
// ====================================================================
void startSequence(bool forward) {
  seqActive  = true;
  seqForward = forward;
  seqState   = 0;
  seqWaitStart = 0;

  if (forward) {
    seqCurrentStep = 0;
  } else {
    seqCurrentStep = gBrojStepenica - 1;
  }
  for(int i=0; i<gBrojStepenica; i++){
    stepsArray[i].state = TrackState::OFF;
    stepsArray[i].step  = 0;
    stepsArray[i].lastUpdate = millis();
  }
}

void updateSequence() {
  if (!seqActive) return;

  if (seqState == 0) {
    if (seqForward) {
      if (seqCurrentStep < gBrojStepenica) {
        if (stepsArray[seqCurrentStep].state==TrackState::OFF) {
          stepsArray[seqCurrentStep].state=TrackState::WIPE_IN;
          stepsArray[seqCurrentStep].step=0;
          stepsArray[seqCurrentStep].lastUpdate= millis();
        }
        updateSegment(stepsArray[seqCurrentStep], ledsUp);
        if (stepsArray[seqCurrentStep].state==TrackState::EFFECT) {
          seqCurrentStep++;
        }
      } else {
        seqState=1;
        seqWaitStart=millis();
      }
    } else {
      if (seqCurrentStep >= 0) {
        if (stepsArray[seqCurrentStep].state==TrackState::OFF) {
          stepsArray[seqCurrentStep].state=TrackState::WIPE_IN;
          stepsArray[seqCurrentStep].step=0;
          stepsArray[seqCurrentStep].lastUpdate= millis();
        }
        updateSegment(stepsArray[seqCurrentStep], ledsUp);
        if (stepsArray[seqCurrentStep].state==TrackState::EFFECT) {
          seqCurrentStep--;
        }
      } else {
        seqState=1;
        seqWaitStart=millis();
      }
    }
  }
  else if (seqState==1) {
    if ((millis() - seqWaitStart) >= (gOnTimeSec*1000UL)) {
      seqState=2;
      if (seqForward) {
        seqCurrentStep = gBrojStepenica - 1;
      } else {
        seqCurrentStep = 0;
      }
    } else {
      for(int i=0;i<gBrojStepenica;i++){
        if (stepsArray[i].state==TrackState::EFFECT) {
          updateSegment(stepsArray[i], ledsUp);
        }
      }
    }
  }
  else if (seqState==2) {
    if (seqForward) {
      if (seqCurrentStep >= 0) {
        if (stepsArray[seqCurrentStep].state==TrackState::EFFECT) {
          stepsArray[seqCurrentStep].state=TrackState::WIPE_OUT;
          stepsArray[seqCurrentStep].step=0;
          stepsArray[seqCurrentStep].lastUpdate=millis();
        }
        updateSegment(stepsArray[seqCurrentStep], ledsUp);
        if(stepsArray[seqCurrentStep].state==TrackState::OFF) {
          seqCurrentStep--;
        }
      } else {
        seqActive=false;
      }
    } else {
      if (seqCurrentStep < gBrojStepenica) {
        if (stepsArray[seqCurrentStep].state==TrackState::EFFECT) {
          stepsArray[seqCurrentStep].state=TrackState::WIPE_OUT;
          stepsArray[seqCurrentStep].step=0;
          stepsArray[seqCurrentStep].lastUpdate=millis();
        }
        updateSegment(stepsArray[seqCurrentStep], ledsUp);
        if(stepsArray[seqCurrentStep].state==TrackState::OFF) {
          seqCurrentStep++;
        }
      } else {
        seqActive=false;
      }
    }
  }
}

// ====================================================================
// REINIT
// ====================================================================
void reinitializeSetup() {
  if (ledsUp)   { delete[] ledsUp;   ledsUp   =nullptr; }
  if (ledsDown) { delete[] ledsDown; ledsDown =nullptr; }
  if (stepsArray){ delete[] stepsArray; stepsArray=nullptr; }

  FastLED.clear(true);

  if (gInstallationType == "linija") {
    if (gEnableLed1) {
      ledsUp = new CRGB[gNumLedsUp];
      FastLED.addLeds<WS2812B, LED_PIN_UP, GRB>(ledsUp, gNumLedsUp);
    }
    if (gEnableLed2) {
      ledsDown = new CRGB[gNumLedsDown];
      FastLED.addLeds<WS2812B, LED_PIN_DOWN, GRB>(ledsDown, gNumLedsDown);
    }
  } else {
    initStepSegments();
  }
  FastLED.setMaxPowerInVoltsAndMilliamps(5, gMaxCurrent);
}

// ====================================================================
// IR Senzori - LINIJA
// ====================================================================
void handleIrSensors_linija() {
  unsigned long now = millis();
  int r1 = digitalRead(IR1_PIN);
  int r2 = digitalRead(IR2_PIN);
  int r3 = digitalRead(IR3_PIN);
  int r4 = digitalRead(IR4_PIN);

  if (gEnableLed1 && ledsUp) {
    if (r1==LOW && (now-gIrLast[0]>IR_DEBOUNCE)) {
      gIrLast[0]=now;
      if (trackUp.state==TrackState::OFF) {
        if (gWipeAll) {
          trackUp.state=TrackState::WIPE_IN;
          trackUp.reverse=false;
          trackUp.lastUpdate=now;
          trackUp.step=0;
          trackUp.effectStartTime=0;
        } else {
          trackUp.state=TrackState::EFFECT;
          trackUp.effectStartTime=now;
        }
      }
    }
    if (r2==LOW && (now-gIrLast[1]>IR_DEBOUNCE)) {
      gIrLast[1]=now;
      if (trackUp.state==TrackState::OFF) {
        if (gWipeAll) {
          trackUp.state=TrackState::WIPE_IN;
          trackUp.reverse=true;
          trackUp.lastUpdate=now;
          trackUp.step=0;
          trackUp.effectStartTime=0;
        } else {
          trackUp.state=TrackState::EFFECT;
          trackUp.effectStartTime=now;
        }
      }
    }
  }
  if (gEnableLed2 && ledsDown) {
    if (r3==LOW && (now-gIrLast[2]>IR_DEBOUNCE)) {
      gIrLast[2]=now;
      if (trackDown.state==TrackState::OFF) {
        if (gWipeAll) {
          trackDown.state=TrackState::WIPE_IN;
          trackDown.reverse=false;
          trackDown.lastUpdate=now;
          trackDown.step=0;
          trackDown.effectStartTime=0;
        } else {
          trackDown.state=TrackState::EFFECT;
          trackDown.effectStartTime=now;
        }
      }
    }
    if (r4==LOW && (now-gIrLast[3]>IR_DEBOUNCE)) {
      gIrLast[3]=now;
      if (trackDown.state==TrackState::OFF) {
        if (gWipeAll) {
          trackDown.state=TrackState::WIPE_IN;
          trackDown.reverse=true;
          trackDown.lastUpdate=now;
          trackDown.step=0;
          trackDown.effectStartTime=0;
        } else {
          trackDown.state=TrackState::EFFECT;
          trackDown.effectStartTime=now;
        }
      }
    }
  }
}

// ====================================================================
// IR Senzori - STEPENICA
// ====================================================================
void handleIrSensors_stepenica() {
  unsigned long now = millis();
  int r1 = digitalRead(IR1_PIN);
  int r2 = digitalRead(IR2_PIN);
  int r3 = digitalRead(IR3_PIN);
  int r4 = digitalRead(IR4_PIN);

  if (gEnableLed1) {
    if (r1==LOW && (now-gIrLast[0]>IR_DEBOUNCE)) {
      gIrLast[0]=now;
      if (gStepeniceMode=="sequence") {
        if (!seqActive) {
          startSequence(true);
        }
      } else {
        for (int i=0;i<gBrojStepenica;i++){
          if (stepsArray[i].state==TrackState::OFF) {
            stepsArray[i].state = gWipeAll? TrackState::WIPE_IN : TrackState::EFFECT;
            stepsArray[i].step=0;
            stepsArray[i].lastUpdate=millis();
          }
        }
      }
    }
    if (r2==LOW && (now-gIrLast[1]>IR_DEBOUNCE)) {
      gIrLast[1]=now;
      if (gStepeniceMode=="sequence") {
        if (!seqActive) {
          startSequence(false);
        }
      } else {
        for (int i=0;i<gBrojStepenica;i++){
          if (stepsArray[i].state==TrackState::OFF) {
            stepsArray[i].state = gWipeAll? TrackState::WIPE_IN : TrackState::EFFECT;
            stepsArray[i].step=0;
            stepsArray[i].lastUpdate=millis();
          }
        }
      }
    }
  }
  if (gEnableLed2) {
    if (r3==LOW && (now-gIrLast[2]>IR_DEBOUNCE)) {
      gIrLast[2]=now;
      if (gStepeniceMode=="sequence") {
        if (!seqActive) {
          startSequence(true);
        }
      } else {
        for (int i=0;i<gBrojStepenica;i++){
          if (stepsArray[i].state==TrackState::OFF) {
            stepsArray[i].state = gWipeAll? TrackState::WIPE_IN : TrackState::EFFECT;
            stepsArray[i].step=0;
            stepsArray[i].lastUpdate=millis();
          }
        }
      }
    }
    if (r4==LOW && (now-gIrLast[3]>IR_DEBOUNCE)) {
      gIrLast[3]=now;
      if (gStepeniceMode=="sequence") {
        if (!seqActive) {
          startSequence(false);
        }
      } else {
        for (int i=0;i<gBrojStepenica;i++){
          if (stepsArray[i].state==TrackState::OFF) {
            stepsArray[i].state = gWipeAll? TrackState::WIPE_IN : TrackState::EFFECT;
            stepsArray[i].step=0;
            stepsArray[i].lastUpdate=millis();
          }
        }
      }
    }
  }
}

// ====================================================================
// GLAVNA handleIrSensors
// ====================================================================
void handleIrSensors() {
  if (gInstallationType=="linija") {
    handleIrSensors_linija();
  } else {
    handleIrSensors_stepenica();
  }
}

// ====================================================================
// RUTE
// ====================================================================
void setupRoutes() {
  server.on("/", HTTP_GET, [](){
    if (!handleFileRead("/index.html")) {
      server.send(404, "text/plain","Not found");
    }
  });
  server.onNotFound(handleNotFound);

  server.on("/api/getConfig", HTTP_GET, [](){
    StaticJsonDocument<1024> doc;
    doc["ledsUp"]    = gNumLedsUp;
    doc["ledsDown"]  = gNumLedsDown;
    doc["maxCur"]    = gMaxCurrent;
    doc["effect"]    = gEffect;
    doc["wipeAll"]   = gWipeAll;
    doc["speed"]     = gWipeSpeedMs;
    doc["onTime"]    = gOnTimeSec;
    doc["colorR"]    = gColorR;
    doc["colorG"]    = gColorG;
    doc["colorB"]    = gColorB;
    doc["enableLed1"]= gEnableLed1;
    doc["enableLed2"]= gEnableLed2;
    doc["installationType"] = gInstallationType;
    doc["brojStepenica"]    = gBrojStepenica;
    doc["brojLedicaPoStepenici"] = gBrojLedicaPoStepenici;
    doc["efektKreniOd"]     = gEfektKreniOd;
    doc["rotacijaStepenica"]= gRotacijaStepenica;
    doc["stepMode"]         = gStepeniceMode;
    doc["dayStart"]         = gDayStartStr;
    doc["dayEnd"]           = gDayEndStr;
    doc["dayBr"]            = gDayBrightnessPercent;
    doc["nightBr"]          = gNightBrightnessPercent;

    String out;
    serializeJson(doc,out);
    server.send(200,"application/json", out);
  });

  server.on("/api/saveConfig", HTTP_POST, [](){
    if (!server.hasArg("plain")) {
      server.send(400,"application/json","{\"status\":\"error\"}");
      return;
    }
    String body = server.arg("plain");
    StaticJsonDocument<1024> doc;
    auto err= deserializeJson(doc, body);
    if (err) {
      server.send(400,"application/json","{\"status\":\"error\"}");
      return;
    }
    gWipeAll          = doc["wipeAll"]           | gWipeAll;
    gWipeSpeedMs      = doc["speed"]            | gWipeSpeedMs;
    gOnTimeSec        = doc["onTime"]           | gOnTimeSec;
    gEffect           = doc["effect"]           | gEffect;
    gColorR           = doc["colorR"]           | gColorR;
    gColorG           = doc["colorG"]           | gColorG;
    gColorB           = doc["colorB"]           | gColorB;
    gEnableLed1       = doc["enableLed1"]       | gEnableLed1;
    gEnableLed2       = doc["enableLed2"]       | gEnableLed2;
    gInstallationType = doc["installationType"] | gInstallationType;
    gNumLedsUp        = doc["ledsUp"]           | gNumLedsUp;
    gNumLedsDown      = doc["ledsDown"]         | gNumLedsDown;
    gBrojStepenica    = doc["brojStepenica"]    | gBrojStepenica;
    gBrojLedicaPoStepenici = doc["brojLedicaPoStepenici"] | gBrojLedicaPoStepenici;
    gEfektKreniOd     = doc["efektKreniOd"]     | gEfektKreniOd;
    gRotacijaStepenica= doc["rotacijaStepenica"]| gRotacijaStepenica;
    gStepeniceMode    = doc["stepMode"]         | gStepeniceMode;
    gMaxCurrent       = doc["maxCur"]           | gMaxCurrent;
    gDayStartStr      = doc["dayStart"]         | gDayStartStr;
    gDayEndStr        = doc["dayEnd"]           | gDayEndStr;
    gDayBrightnessPercent = doc["dayBr"]        | gDayBrightnessPercent;
    gNightBrightnessPercent= doc["nightBr"]     | gNightBrightnessPercent;

    savePreferences();
    reinitializeSetup();
    server.send(200,"application/json","{\"status\":\"ok\"}");
  });
}

// ====================================================================
// SETUP & LOOP
// ====================================================================
void setup() {
  pinMode(PUSH_BUTTON_PIN, INPUT_PULLUP);
  pinMode(IR1_PIN, INPUT_PULLUP);
  pinMode(IR2_PIN, INPUT_PULLUP);
  pinMode(IR3_PIN, INPUT_PULLUP);
  pinMode(IR4_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  delay(200);

  loadPreferences();
  setupWiFi();
  SPIFFS.begin(true);
  setupRoutes();
  setupTime();

  if (gInstallationType == "linija") {
    if (gEnableLed1) {
      ledsUp = new CRGB[gNumLedsUp];
      FastLED.addLeds<WS2812B, LED_PIN_UP, GRB>(ledsUp, gNumLedsUp);
    }
    if (gEnableLed2) {
      ledsDown = new CRGB[gNumLedsDown];
      FastLED.addLeds<WS2812B, LED_PIN_DOWN, GRB>(ledsDown, gNumLedsDown);
    }
  } else {
    initStepSegments();
  }
  FastLED.setMaxPowerInVoltsAndMilliamps(5, gMaxCurrent);
  server.begin();

  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 5000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
}

void loop() {
  server.handleClient();
  handlePushButton();

  if (!gManualOnOff) {
    handleIrSensors();
    if (gInstallationType=="stepenica" && gEnableLed1) {
      if (gStepeniceMode=="sequence") {
        updateSequence();
      } else {
        updateAllStepSegments();
      }
    } else if (gInstallationType=="linija") {
      if (gEnableLed1 && ledsUp) {
        updateTrack_line(trackUp, ledsUp, gNumLedsUp, true);
      }
      if (gEnableLed2 && ledsDown) {
        updateTrack_line(trackDown, ledsDown, gNumLedsDown, false);
      }
    }
  } else {
    if (gInstallationType=="linija") {
      if (gEnableLed1 && ledsUp)   applyGlobalEffect(ledsUp,   gNumLedsUp, true);
      if (gEnableLed2 && ledsDown) applyGlobalEffect(ledsDown, gNumLedsDown, false);
    } else {
      if (gEnableLed1 && ledsUp) {
        applyGlobalEffect(ledsUp, gTotalLedsStepenice, true);
      }
    }
  }

  FastLED.setBrightness(getDayNightBrightness());
  FastLED.show();
  esp_task_wdt_reset();
}
