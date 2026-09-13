#include "App.h"
#include <esp_arduino_version.h>

namespace {
uint32_t idleColor=0x0000FF, readColor=0x00FF00, writeColor=0xFF0000;
bool pwmReady=false;
constexpr int pins[] = {PIN_RGB_RED,PIN_RGB_GREEN,PIN_RGB_BLUE};

bool parseColor(const String &text, uint32_t &value) {
  if(text.length()!=7 || text[0]!='#')return false;
  for(size_t i=1;i<7;++i)if(!isxdigit(static_cast<unsigned char>(text[i])))return false;
  value=strtoul(text.c_str()+1,nullptr,16);
  return true;
}

String colorText(uint32_t color) {
  char text[8];
  snprintf(text,sizeof(text),"#%06lx",static_cast<unsigned long>(color));
  return String(text);
}

void applyColor(uint32_t color) {
  if(!pwmReady)return;
  if(!rgbEnabled.load())color=0;
  for(uint8_t channel=0;channel<3;++channel) {
    // Gemeinsame Anode: invertierte PWM, 255 bedeutet vollständig aus.
    const uint8_t duty=255-((color>>(16-8*channel))&0xFF);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(pins[channel],duty);
#else
    ledcWrite(channel,duty);
#endif
  }
}
} // namespace

void initStatusLed() {
  bool ready=true;
  for(uint8_t channel=0;channel<3;++channel) {
    pinMode(pins[channel],OUTPUT);
    digitalWrite(pins[channel],HIGH);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    const bool attached=ledcAttach(pins[channel],5000,8);
    ready=attached && ready;
    if(attached)ledcWrite(pins[channel],255);
#else
    ready=(ledcSetup(channel,5000,8)>0) && ready;
    ledcAttachPin(pins[channel],channel);
    ledcWrite(channel,255);
#endif
  }
  pwmReady=ready;
  if(!ready)debugPrintln("[LED] FEHLER: PWM-Initialisierung");
  rgbOff();
}

void rgbOff() {
  rgbReadActive=false;
  applyColor(idleColor);
}

void rgbReadOk() {
  applyColor(readColor);
  rgbReadStartedAt=millis();
  rgbReadActive=true;
}

void rgbWriteStart() {
  rgbReadActive=false;
  applyColor(writeColor);
}

void loadLedSettings() {
  // Vorhandenen Ein/Aus-Wert aus Version 5.6 übernehmen.
  bool enabled=preferences.getBool("rgbled",true);
  JsonDocument doc;
  const String saved=preferences.getString("ledcfg","");
  if(saved.length() && !deserializeJson(doc,saved)) {
    uint32_t idle,read,write;
    if(doc["enabled"].is<bool>() && parseColor(doc["idle"]|"",idle) &&
       parseColor(doc["read"]|"",read) && parseColor(doc["write"]|"",write)) {
      enabled=doc["enabled"].as<bool>();
      idleColor=idle; readColor=read; writeColor=write;
    }
  }
  rgbEnabled.store(enabled);
  rgbOff();
}

String ledSettingsJson() {
  JsonDocument doc;
  doc["ok"]=true;
  doc["enabled"]=rgbEnabled.load();
  doc["idle"]=colorText(idleColor);
  doc["read"]=colorText(readColor);
  doc["write"]=colorText(writeColor);
  String json;
  serializeJson(doc,json);
  return json;
}

void handleLedSettings(AsyncWebServerRequest *request, JsonVariant &body) {
  if(!body.is<JsonObject>() || !body["enabled"].is<bool>()) {
    request->send(400,"application/json","{\"error\":\"enabled must be boolean\"}");
    return;
  }
  SensorLock lock(pdMS_TO_TICKS(1000));
  if(!lock) {
    request->send(503,"application/json","{\"error\":\"sensor busy\"}");
    return;
  }
  uint32_t idle=idleColor,read=readColor,write=writeColor;
  const char *keys[]={"idle","read","write"};
  uint32_t *colors[]={&idle,&read,&write};
  for(uint8_t i=0;i<3;++i) {
    if(!body[keys[i]].isUnbound() &&
       (!body[keys[i]].is<const char*>() || !parseColor(body[keys[i]].as<String>(),*colors[i]))) {
      request->send(400,"application/json","{\"error\":\"colors must be #RRGGBB\"}");
      return;
    }
  }
  JsonDocument saved;
  saved["enabled"]=body["enabled"].as<bool>();
  saved["idle"]=colorText(idle);
  saved["read"]=colorText(read);
  saved["write"]=colorText(write);
  String json;
  serializeJson(saved,json);
  // Ein einziger NVS-Wert verhindert teilweise gespeicherte Farbgruppen.
  if(preferences.putString("ledcfg",json)!=json.length()) {
    request->send(500,"application/json","{\"error\":\"save failed\"}");
    return;
  }
  idleColor=idle; readColor=read; writeColor=write;
  rgbEnabled.store(saved["enabled"].as<bool>());
  rgbOff();
  debugPrintf("[LED] Gespeichert: %s | Ruhe %s / Lesen %s / Schreiben %s\n",
              rgbEnabled.load()?"aktiv":"aus",colorText(idle).c_str(),
              colorText(read).c_str(),colorText(write).c_str());
  request->send(200,"application/json",ledSettingsJson());
}
