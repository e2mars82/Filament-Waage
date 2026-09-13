#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <Update.h>
#include <time.h>
#include <MD5Builder.h>
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <TFT_eSPI.h>
#include <HX711.h>
#include <Adafruit_PN532.h>
#include "Config.h"
#include "lang/lang_de.h"
#include "lang/lang_en.h"
#include <stdarg.h>
// Sämtliche Diagnoseausgaben laufen über diese zwei Funktionen. Mit ENABLE_DEBUG
// in Config.h lassen sie sich abschalten, ohne die serielle Schnittstelle zu ändern.
void debugPrintf(const char *format, ...)  {
  if(!ENABLE_DEBUG)return;
  char buffer[256];
  va_list args;
  va_start(args,format);
  vsnprintf(buffer,sizeof(buffer),format,args);
  va_end(args);
  Serial.print(buffer);
}
void debugPrintln(const char *text)  {
  if(ENABLE_DEBUG)Serial.println(text);
}
struct Roll  {
  String id, material, maker, color = "#3b82f6";
  float weight = 0, previous = 0;
  float spoolWeight = 0;
  String spoolType = "Standard";
};
struct SpoolType { String name; float weight; };
constexpr char DB_FILE[] = "/filament.json";
std::vector<Roll> rolls;
std::vector<String> customMakers, customMaterials;
std::vector<SpoolType> spoolTypes;
HX711 scale;
// Display/Touch verwenden VSPI. Die SD-Karte muss deshalb HSPI verwenden;
// sonst wird der Display-SPI-Bus beim Initialisieren der SD-Karte umgeschaltet.
SPIClass sdSpi(HSPI);
// Die installierte Adafruit-Bibliothek erwartet für I²C noch IRQ und RESET
// als Konstruktorparameter. 0xFF wird intern als -1 gespeichert: kein realer
// GPIO wird als Reset verwendet; die Kommunikation erfolgt per I²C-Polling.
constexpr uint8_t PN532_NO_PIN = 0xFF;
Adafruit_PN532 nfc(PN532_NO_PIN, PN532_NO_PIN, &Wire);
// TFT_eSPI erhält seine E32R35T-Pins über die Build-Flags in platformio.ini.
TFT_eSPI screen = TFT_eSPI();
AsyncWebServer server(80);
DNSServer dns;
Preferences preferences;
String activeId;
float liveWeight = 0;
float hxCalibration = HX711_CALIBRATION;
uint8_t weightDecimals = 1;
float hxWeightSamples[HX711_AVERAGE_SAMPLES] = {};
uint8_t hxWeightSampleCount = 0;
uint8_t hxWeightSampleIndex = 0;
unsigned long lastLcdWeightUpdate = 0;
unsigned long lastNfc = 0;
uint8_t emptyNfcReads = 0;
bool spoolMenu = false;
uint8_t entryStep = 0;
String selectedMaterial;
unsigned long lastSerialReport = 0;
bool sdReady = false, wifiSetupMode = false;
bool nfcReady = false;
volatile bool internetAvailable = false;
TaskHandle_t networkTaskHandle = nullptr;
bool hxReady = false;
bool hxHasSample = false;
bool ntpConfigured = false;
unsigned long lastNtpAttempt = 0;
unsigned long restartAt = 0;
// Affine Touch-Kalibrierung: unterstützt Versatz, Drehung und vertauschte Achsen.
struct TouchCalibration  {
  float ax=0, bx=0, cx=0, ay=0, by=0, cy=0;
  bool valid=false;
}
touchCal;
uint16_t touchCalRaw[4][2];
uint8_t touchCalStep=255;
constexpr int TOUCH_CAL_X[4]= {
  30,290,290,30
};
constexpr int TOUCH_CAL_Y[4]= {
  30,30,450,450
};
constexpr uint16_t C_BG=0x1082, C_PANEL=0x18E3, C_HEADER=0x0861, C_TEXT=0xFFFF, C_MUTED=0xBDF7;
constexpr uint16_t C_BLUE=0x3D7F, C_GREEN=0x05E0, C_ORANGE=0xFD20, C_RED=0xF986;
constexpr uint16_t C_PRESSED=0x4208;
String esc(const String &s)  {
  String r=s;
  r.replace("\\", "\\\\");
  r.replace("\"", "\\\"");
  return r;
}
void loadCustomValues(const char *key,std::vector<String> &values) {
  values.clear();
  String packed=preferences.getString(key,"");
  int start=0;
  while(start<=packed.length()) {
    int end=packed.indexOf('\n',start);
    String value=packed.substring(start,end<0?packed.length():end);
    value.trim();
    if(value.length())values.push_back(value);
    if(end<0)break;
    start=end+1;
  }
}
void rememberCustomValue(const char *key,std::vector<String> &values,const String &value) {
  String clean=value;clean.trim();
  if(!clean.length())return;
  for(const String &existing:values)if(existing.equalsIgnoreCase(clean))return;
  values.push_back(clean);
  String packed;
  for(const String &entry:values) { if(packed.length())packed+='\n';packed+=entry; }
  preferences.putString(key,packed);
}
String jsonValues(const std::vector<String> &values) {
  String json="[";
  for(size_t i=0;i<values.size();i++) {
    if(i)json+=',';
    json+='"';
    json+=esc(values[i]);
    json+='"';
  }
  return json+"]";
}
void saveSpoolTypes() {
  JsonDocument doc;JsonArray list=doc.to<JsonArray>();
  for(const SpoolType &type:spoolTypes) { JsonObject item=list.add<JsonObject>();item["name"]=type.name;item["weight"]=type.weight; }
  String text;serializeJson(doc,text);preferences.putString("spoolTypes",text);
}
void loadSpoolTypes() {
  spoolTypes.clear();String text=preferences.getString("spoolTypes","");JsonDocument doc;
  if(text.length() && !deserializeJson(doc,text)) for(JsonObject item:doc.as<JsonArray>())spoolTypes.push_back({item["name"]|"Spule",item["weight"]|0.0F});
  if(spoolTypes.empty()) { spoolTypes={{"Standard",0},{"Karton",150},{"Kunststoff",250}};saveSpoolTypes(); }
}
String spoolsJson() {
  String json="[";for(size_t i=0;i<spoolTypes.size();i++){if(i)json+=',';json+="{\"name\":\""+esc(spoolTypes[i].name)+"\",\"weight\":"+String(spoolTypes[i].weight,1)+"}";}return json+"]";
}
String createId()  {
  MD5Builder md5;
  String seed=String((uint32_t)time(nullptr))+"-"+String((uint32_t)ESP.getEfuseMac(),HEX)+"-"+String(esp_random(),HEX);
  md5.begin();
  md5.add(seed);
  md5.calculate();
  return "NFC-"+md5.toString();
}
String clockText()  {
  time_t now=time(nullptr);
  if(now<1700000000)return "--:--";
  tm info;
  localtime_r(&now,&info);
  char out[18];
  strftime(out,sizeof(out),"%d.%m. %H:%M",&info);
  return String(out);
}
// RGB-LED ist gemeinsame Anode: LOW schaltet die jeweilige Farbe ein.
// Blau ist die Ruheanzeige; NFC-Aktionen überblenden die Ruheanzeige kurz.
void rgbOff()  {
  digitalWrite(PIN_RGB_RED,HIGH);
  digitalWrite(PIN_RGB_GREEN,HIGH);
  digitalWrite(PIN_RGB_BLUE,LOW);
}
void rgbReadOk()  {
  digitalWrite(PIN_RGB_RED,HIGH);
  digitalWrite(PIN_RGB_GREEN,LOW);
  digitalWrite(PIN_RGB_BLUE,HIGH);
  delay(180);
  rgbOff();
}
void rgbWriteStart()  {
  digitalWrite(PIN_RGB_RED,LOW);
  digitalWrite(PIN_RGB_GREEN,HIGH);
  digitalWrite(PIN_RGB_BLUE,HIGH);
}
float batteryVoltage()  {
  return analogReadMilliVolts(PIN_BATTERY_ADC) * BATTERY_DIVIDER_FACTOR / 1000.0F;
}
int batteryPercent()  {
  return constrain((int)((batteryVoltage()-3.20F)*100.0F/1.00F),0,100);
}
void drawBattery(int x,int y)  {
  int p=batteryPercent(),w=22;
  screen.drawRect(x,y,w,10,C_TEXT);
  screen.fillRect(x+w,y+3,2,4,C_TEXT);
  screen.fillRect(x+1,y+1,(w-2)*p/100,8,p<20?C_RED:C_GREEN);
}
uint16_t color565(const String &hex) {
  if(hex.length()!=7 || hex[0]!='#')return C_BLUE;
  uint32_t rgb=strtoul(hex.c_str()+1,nullptr,16);
  return ((rgb>>19)&0x1F)<<11 | ((rgb>>10)&0x3F)<<5 | ((rgb>>3)&0x1F);
}
// Herstellersequenz aus LCDWIKI ST7796_Init.txt. Der ST7796U benötigt diese
// Power-, Gamma- und Command-Set-Konfiguration zusätzlich zur Standardinitialisierung.
void lcdCommand(uint8_t command, const uint8_t *data=nullptr, size_t length=0)  {
  SPI.beginTransaction(SPISettings(40000000,MSBFIRST,SPI_MODE0));
  digitalWrite(PIN_TFT_CS,LOW);
  digitalWrite(PIN_TFT_DC,LOW);
  SPI.transfer(command);
  if(length) {
    digitalWrite(PIN_TFT_DC,HIGH);
    SPI.writeBytes(data,length);
  }
  digitalWrite(PIN_TFT_CS,HIGH);
  SPI.endTransaction();
}
void initSt7796u()  {
  const uint8_t d36[]= {
    0x48
  },d3a[]= {
    0x55
  },df0c3[]= {
    0xC3
  },df096[]= {
    0x96
  },db4[]= {
    0x01
  },db7[]= {
    0xC6
  };
  const uint8_t dc0[]= {
    0x80,0x45
  },dc1[]= {
    0x13
  },dc2[]= {
    0xA7
  },dc5[]= {
    0x20
  };
  const uint8_t de8[]= {
    0x40,0x8A,0x00,0x00,0x29,0x19,0xA5,0x33
  };
  const uint8_t de0[]= {
    0xD0,0x08,0x0F,0x06,0x06,0x33,0x30,0x33,0x47,0x17,0x13,0x13,0x2B,0x31
  };
  const uint8_t de1[]= {
    0xD0,0x0A,0x11,0x0B,0x09,0x07,0x2F,0x33,0x47,0x38,0x15,0x16,0x2C,0x32
  };
  const uint8_t df03c[]= {
    0x3C
  },df069[]= {
    0x69
  };
  lcdCommand(0x11);
  delay(120);
  lcdCommand(0x36,d36,1);
  lcdCommand(0x3A,d3a,1);
  lcdCommand(0xF0,df0c3,1);
  lcdCommand(0xF0,df096,1);
  lcdCommand(0xB4,db4,1);
  lcdCommand(0xB7,db7,1);
  lcdCommand(0xC0,dc0,2);
  lcdCommand(0xC1,dc1,1);
  lcdCommand(0xC2,dc2,1);
  lcdCommand(0xC5,dc5,1);
  lcdCommand(0xE8,de8,8);
  lcdCommand(0xE0,de0,14);
  lcdCommand(0xE1,de1,14);
  lcdCommand(0xF0,df03c,1);
  lcdCommand(0xF0,df069,1);
  delay(120);
  lcdCommand(0x29);
}
Roll* findRoll(const String &id)  {
  for (auto &r: rolls) if (r.id == id) return &r;
  return nullptr;
}
void saveDb();
// Eine neue NFC-ID wird sofort auf der SD-Karte dokumentiert. Die Rolle lässt
// sich anschließend im WebUI über „Bearbeiten“ mit Material und Hersteller ergänzen.
Roll* addUnknownRoll(const String &id)  {
  if(!id.length()) return nullptr;
  if(Roll *known=findRoll(id)) return known;
  rolls.push_back( {
    id,"Unbekannt","Unbekannt","#64748b",liveWeight,liveWeight
  });
  saveDb();
  debugPrintf("[DB] Unbekannte NFC-Rolle angelegt: %s\n",id.c_str());
  return &rolls.back();
}
void saveDb()  {
  if (!sdReady)  {
    debugPrintln("[DB] SD-Datenbank deaktiviert");
    return;
  }
  JsonDocument d;
  JsonArray a=d.to<JsonArray>();
  for (auto &r: rolls)  {
    JsonObject o=a.add<JsonObject>();
    o["id"]=r.id;
    o["material"]=r.material;
    o["maker"]=r.maker;
    o["color"]=r.color;
    o["weight"]=r.weight;
    o["previous"]=r.previous;
    o["spoolWeight"]=r.spoolWeight;
    o["spoolType"]=r.spoolType;
  }
  File f=SD.open(DB_FILE, FILE_WRITE);
  if (f)  {
    serializeJson(d,f);
    f.close();
  }
  debugPrintf("[DB] %u Rollen auf SD gespeichert\n", rolls.size());
}
void loadDb()  {
  if (!sdReady) return;
  File f=SD.open(DB_FILE);
  if (!f)  {
    debugPrintln("[DB] Neue Datenbank wird angelegt");
    saveDb();
    return;
  }
  JsonDocument d;
  if (deserializeJson(d,f))  {
    debugPrintln("[DB] FEHLER: filament.json ist ungueltig");
    f.close();
    return;
  }
  f.close();
  for (JsonObject o:d.as<JsonArray>()) {
    Roll roll={o["id"]|"",o["material"]|"",o["maker"]|"",o["color"]|"#3b82f6",o["weight"]|0.0F,o["previous"]|0.0F};
    roll.spoolWeight=o["spoolWeight"]|0.0F;
    roll.spoolType=o["spoolType"]|"Standard";
    rolls.push_back(roll);
  }
  debugPrintf("[DB] %u Rollen geladen\n", rolls.size());
}
String rollsJson()  {
  String s="[";
  for (size_t i=0;
  i<rolls.size();
  i++)  {
    auto&r=rolls[i];
    if(i)s+=',';
    s+="{\"id\":\""+esc(r.id)+"\",\"material\":\""+esc(r.material)+"\",\"maker\":\""+esc(r.maker)+"\",\"color\":\""+esc(r.color)+"\",\"weight\":"+String(r.weight,1)+",\"previous\":"+String(r.previous,1)+",\"spoolWeight\":"+String(r.spoolWeight,1)+",\"spoolType\":\""+esc(r.spoolType)+"\"}";
  }
  return s+"]";
}
// Speichert die Rolleninformationen kompakt in den NTAG-Nutzerseiten 4–39.
bool writeTag(const Roll &roll)  {
  if(!ENABLE_PN532 || !nfcReady)return false;
  rgbWriteStart();
  String material=roll.material.substring(0,10),maker=roll.maker.substring(0,16);
  material.replace("|","_");maker.replace("|","_");
  String text="FS2|"+roll.id+"|"+String(roll.weight,1)+"|"+roll.color+"|"+material+"|"+maker;
  if(text.length()>144) { rgbOff();return false; }
  uint8_t p=4, data[5];
  bool ok=true;
  for (uint16_t offset=0;
  offset<text.length();
  offset+=4,p++)  {
    memset(data,0,5);
    text.substring(offset,offset+4).getBytes(data,5);
    if(!nfc.ntag2xx_WritePage(p,data)) {
      ok=false;
      break;
    }
  }
  rgbOff();
  return ok;
}
String readTag()  {
  if(!ENABLE_PN532 || !nfcReady)return "";
  uint8_t uid[7], len;
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A,uid,&len,80)) return "";
  String raw;
  uint8_t data[4];
  for(uint8_t p=4;
  p<40;
  p++)  {
    if(!nfc.ntag2xx_ReadPage(p,data)) break;
    for(auto b:data) if(b)raw+=(char)b;
  }
  if(raw.startsWith("FS1:")) {
    rgbReadOk();
    return raw.substring(4);
  }
  if(raw.startsWith("FS2|")) {
    int end=raw.indexOf('|',4);
    rgbReadOk();
    return end>4?raw.substring(4,end):"";
  }
  String id="UID-";
  for(uint8_t i=0;
  i<len;
  i++) {
    if(uid[i]<16)id+='0';
    id+=String(uid[i],HEX);
  }
  rgbReadOk();
  return id;
}
void label(const String &text, int x, int y, uint16_t color=C_MUTED)  {
  screen.setTextSize(1);
  screen.setTextColor(color);
  screen.setCursor(x,y);
  screen.print(text);
}
void button(int x,int y,int w,int h,const String &text,uint16_t color)  {
  int size=text.length()>8?1:2;
  int charWidth=size==1?6:12;
  screen.fillRoundRect(x,y,w,h,8,color);
  screen.setTextColor(C_TEXT);
  screen.setTextSize(size);
  int tx=x+(w-text.length()*charWidth)/2;
  screen.setCursor(tx,y+(h-(size==1?8:16))/2);
  screen.print(text);
}
void buttonPressed(int x,int y,int w,int h,const String &text) {
  button(x,y,w,h,text,C_PRESSED);
  delay(60);
}
// Einheitliche Gewichtsformatierung für WebUI und LCD: 0 oder 1 Nachkommastelle
// wird in den Einstellungen gewählt; auf dem LCD wird das deutsche Komma genutzt.
String formatWeightLcd(float value) {
  // Verhindert -0 bzw. -0,0 durch minimale Messabweichungen um den Nullpunkt.
  if(fabsf(value)<(weightDecimals?0.05F:0.5F))value=0.0F;
  String text(value,static_cast<unsigned int>(weightDecimals));
  text.replace(".",",");
  return text;
}
void draw()  {
  if(spoolMenu) {
    screen.fillScreen(C_BG);screen.setTextColor(C_TEXT);screen.setTextSize(2);screen.setCursor(22,30);screen.print("Spulengewicht");screen.setTextSize(1);screen.setCursor(22,55);screen.print("Wert fuer aktive Rolle waehlen");
    for(size_t i=0;i<spoolTypes.size()&&i<6;i++) {
      int bx=20+(i%2)*150,by=90+(i/2)*85;
      screen.fillRoundRect(bx,by,130,62,8,C_BLUE);screen.setTextColor(C_TEXT);
      // Der Spulenname ist bewusst doppelt so groß wie das Gewicht.
      screen.setTextSize(2);screen.setCursor(bx+8,by+10);screen.print(spoolTypes[i].name.substring(0,10));
      screen.setTextSize(1);screen.setCursor(bx+8,by+43);screen.printf("%.0f g",spoolTypes[i].weight);
    }
    return;
  }
  if(touchCalStep<4)  {
    screen.fillScreen(C_BG);
    screen.setTextColor(C_TEXT);
    screen.setTextSize(2);
    screen.setCursor(18,175);
    screen.print("Touch kalibrieren");
    screen.setTextSize(1);
    screen.setCursor(18,205);
    screen.print("Bitte das Fadenkreuz beruehren");
    int x=TOUCH_CAL_X[touchCalStep],y=TOUCH_CAL_Y[touchCalStep];
    screen.drawCircle(x,y,18,C_ORANGE);
    screen.drawFastHLine(x-25,y,51,C_ORANGE);
    screen.drawFastVLine(x,y-25,51,C_ORANGE);
    screen.setCursor(18,230);
    screen.printf("Punkt %d von 4",touchCalStep+1);
    return;
  }
  screen.fillScreen(C_BG);
  screen.fillRect(0,0,320,36,C_HEADER);
  screen.setTextColor(C_TEXT);
  screen.setTextSize(2);
  screen.setCursor(10,10);
  screen.print(LangDE::APP);
  screen.setTextSize(1);
  screen.setCursor(210,14);
  screen.print(clockText());
  drawBattery(290,13);
  screen.fillRoundRect(10,48,145,96,10,C_PANEL);
  screen.fillRoundRect(165,48,145,96,10,C_PANEL);
  label("GESAMTGEWICHT",22,62);
  screen.setTextColor(C_TEXT);
  screen.setTextSize(3);
  screen.setCursor(22,88);
  if(hxHasSample)screen.print(formatWeightLcd(liveWeight));
  else screen.print(formatWeightLcd(0));
  screen.setTextSize(1);screen.print(" g");
  label("MATERIAL",177,62);
  screen.setTextColor(C_TEXT);
  screen.setTextSize(3);
  screen.setCursor(177,88);
  if(hxHasSample)screen.print(formatWeightLcd(max(0.0F,liveWeight-(findRoll(activeId)?findRoll(activeId)->spoolWeight:0.0F))));
  else screen.print(formatWeightLcd(0));
  screen.setTextSize(1);screen.print(" g");
  Roll*r=findRoll(activeId);
  screen.fillRoundRect(10,156,300,170,10,C_PANEL);
  if(r) {
    label("AKTIVE ROLLE",22,170,C_GREEN);
    screen.fillCircle(286,178,10,color565(r->color));
    screen.drawCircle(286,178,10,C_TEXT);
    screen.setTextColor(C_MUTED);
    screen.setTextSize(1);
    screen.setCursor(22,187);
    screen.print("ID: ");
    screen.print(r->id.substring(0,28));
    screen.setTextColor(C_TEXT);
    screen.setTextSize(2);
    screen.setCursor(22,204);
    screen.print(r->material);
    screen.setCursor(22,229);
    screen.print(r->maker);
    label("SPULE: "+r->spoolType,190,222);
    label(String(r->spoolWeight,0)+" g Offset",190,232);
    label("VERBRAUCH SEIT LETZTER WIEGUNG",22,258);
    screen.setTextColor(C_ORANGE);
    screen.setTextSize(2);
    screen.setCursor(22,274);
    // Verbrauch gemäß Datenmodell: aktuelles Materialgewicht minus zuletzt
    // in der Rollen-Datenbank gespeichertes Materialgewicht.
    if(hxHasSample)screen.print(formatWeightLcd(max(0.0F,liveWeight-r->spoolWeight)-r->weight)+" g");
    else screen.print(formatWeightLcd(0)+" g");
  }
  else {
    screen.setTextColor(C_TEXT);
    screen.setTextSize(2);
    screen.setCursor(22,190);
    screen.print(activeId.length()?"Neue Rolle: Auswahl im WebUI":"NFC-Tag an Leser halten");
  }
  button(6,344,74,62,"TARA",C_GREEN);
  // Ohne aktive Rolle ist kein Spulengewicht wählbar. Die frei werdende Fläche
  // nutzt der größere Wiegen-Button; NFC-Tags werden weiterhin automatisch gelesen.
  if(r) {
    button(84,344,152,62,"WIEGEN",C_GREEN);
    button(240,344,74,62,"SPULE",C_BLUE);
  }
  else button(84,344,230,62,"WIEGEN",C_MUTED);
  screen.setTextSize(1);
  screen.setTextColor(C_MUTED);
  screen.setCursor(10,458);
  screen.printf("v%s",FIRMWARE_VERSION);
  String ip=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString();
  screen.setCursor(314-ip.length()*6,458);
  screen.print(ip);
}

// Aktualisiert nur die dynamischen Zahlenfelder. So bleibt die Gewichtsanzeige
// flüssig, ohne dass das komplette LCD bei jedem HX711-Messwert flackert.
void drawLiveWeight() {
  if(spoolMenu || touchCalStep<4)return;

  Roll *r=findRoll(activeId);
  float netWeight=max(0.0F,liveWeight-(r?r->spoolWeight:0.0F));

  screen.fillRect(18,80,135,54,C_PANEL);
  screen.setTextColor(C_TEXT);
  screen.setTextSize(3);
  screen.setCursor(22,88);
  if(hxHasSample)screen.print(formatWeightLcd(liveWeight));
  else screen.print(formatWeightLcd(0));
  screen.setTextSize(1);
  screen.print(" g");

  screen.fillRect(173,80,135,54,C_PANEL);
  screen.setTextColor(C_TEXT);
  screen.setTextSize(3);
  screen.setCursor(177,88);
  if(hxHasSample)screen.print(formatWeightLcd(netWeight));
  else screen.print(formatWeightLcd(0));
  screen.setTextSize(1);
  screen.print(" g");

  if(r) {
    screen.fillRect(20,270,260,27,C_PANEL);
    screen.setTextColor(C_ORANGE);
    screen.setTextSize(2);
    screen.setCursor(22,274);
    if(hxHasSample)screen.print(formatWeightLcd(netWeight-r->weight)+" g");
    else screen.print(formatWeightLcd(0)+" g");
  }
}

// Jede neue Messung ersetzt den ältesten Wert. Der Mittelwert aus dem Fenster
// beruhigt die Gewichtsanzeige ohne die Touchbedienung auszubremsen.
void addWeightSample(float sample) {
  hxWeightSamples[hxWeightSampleIndex] = sample;
  hxWeightSampleIndex = (hxWeightSampleIndex + 1) % HX711_AVERAGE_SAMPLES;
  if(hxWeightSampleCount < HX711_AVERAGE_SAMPLES)hxWeightSampleCount++;
  float sum = 0;
  for(uint8_t i = 0; i < hxWeightSampleCount; i++)sum += hxWeightSamples[i];
  liveWeight = sum / hxWeightSampleCount;
}
// Direkter XPT2046-Zugriff: vermeidet, dass eine Bibliothek den LCD-SPI-Bus erneut umbelegt.
uint16_t xptRead(uint8_t command)  {
  SPI.beginTransaction(SPISettings(2500000,MSBFIRST,SPI_MODE0));
  digitalWrite(PIN_TOUCH_CS,LOW);
  SPI.transfer(command);
  uint16_t value=(SPI.transfer(0)<<8)|SPI.transfer(0);
  digitalWrite(PIN_TOUCH_CS,HIGH);
  SPI.endTransaction();
  return value>>3;
}
void mapTouch(uint16_t rawX,uint16_t rawY,int &x,int &y)  {
  if(touchCal.valid) {
    x=constrain((int)(touchCal.ax*rawX+touchCal.bx*rawY+touchCal.cx),0,319);
    y=constrain((int)(touchCal.ay*rawX+touchCal.by*rawY+touchCal.cy),0,479);
  }
  else {
    x=constrain(map(rawX,TOUCH_MIN_X,TOUCH_MAX_X,0,320),0,319);
    y=constrain(map(rawY,TOUCH_MIN_Y,TOUCH_MAX_Y,0,480),0,479);
  }
}
bool solveTouchAxis(float t0,float t1,float t2,float &a,float &b,float &c)  {
  float x0=touchCalRaw[0][0],x1=touchCalRaw[1][0],x2=touchCalRaw[2][0],y0=touchCalRaw[0][1],y1=touchCalRaw[1][1],y2=touchCalRaw[2][1];
  float d=x0*(y1-y2)+x1*(y2-y0)+x2*(y0-y1);
  if(fabsf(d)<1)return false;
  a=(t0*(y1-y2)+t1*(y2-y0)+t2*(y0-y1))/d;
  b=(x0*(t1-t2)+x1*(t2-t0)+x2*(t0-t1))/d;
  c=(x0*(y1*t2-y2*t1)+x1*(y2*t0-y0*t2)+x2*(y0*t1-y1*t0))/d;
  return true;
}
void finishTouchCalibration()  {
  if(!solveTouchAxis(TOUCH_CAL_X[0],TOUCH_CAL_X[1],TOUCH_CAL_X[2],touchCal.ax,touchCal.bx,touchCal.cx)||!solveTouchAxis(TOUCH_CAL_Y[0],TOUCH_CAL_Y[1],TOUCH_CAL_Y[2],touchCal.ay,touchCal.by,touchCal.cy)) {
    debugPrintln("[TOUCH] Kalibrierung ungueltig – erneut starten");
    touchCalStep=255;
    draw();
    return;
  }
  touchCal.valid=true;
  preferences.putFloat("tax",touchCal.ax);
  preferences.putFloat("tbx",touchCal.bx);
  preferences.putFloat("tcx",touchCal.cx);
  preferences.putFloat("tay",touchCal.ay);
  preferences.putFloat("tby",touchCal.by);
  preferences.putFloat("tcy",touchCal.cy);
  preferences.putBool("tcal",true);
  debugPrintf("[TOUCH] Kalibrierung gespeichert: x=%.4f/%.4f/%.1f y=%.4f/%.4f/%.1f\n",touchCal.ax,touchCal.bx,touchCal.cx,touchCal.ay,touchCal.by,touchCal.cy);
  touchCalStep=255;
  draw();
}
void handleTouch()  {
  static bool wasPressed=false;
  static unsigned long lastIdle=0;
  // Bei diesem Board bleibt T_IRQ bei einigen Touch-Modulen dauerhaft HIGH.
  // Deshalb entscheidet der tatsächlich gelesene XPT2046-Wert, nicht die IRQ-Leitung.
  uint16_t rawX=xptRead(0xD0),rawY=xptRead(0x90);
  bool pressed=(rawX>100 && rawY>100);
  if(!pressed)  {
    wasPressed=false;
    if(millis()-lastIdle>3000) {
      lastIdle=millis();
      debugPrintf("[TOUCH] Bereit (IRQ GPIO%d=%d, raw=%u/%u)\n",PIN_TOUCH_IRQ,digitalRead(PIN_TOUCH_IRQ),rawX,rawY);
    }
    return;
  }
  if(wasPressed)return;
  wasPressed=true;
  if(touchCalStep<4)  {
    touchCalRaw[touchCalStep][0]=rawX;
    touchCalRaw[touchCalStep][1]=rawY;
    debugPrintf("[TOUCH] Kalibrierpunkt %d: raw x=%u y=%u\n",touchCalStep+1,rawX,rawY);
    touchCalStep++;
    if(touchCalStep==4)finishTouchCalibration();
    else draw();
    return;
  }
  int x,y;
  mapTouch(rawX,rawY,x,y);
  debugPrintf("[TOUCH] Berührung: raw x=%u y=%u -> display x=%d y=%d\n",rawX,rawY,x,y);
  if(spoolMenu) {
    int column=x>=160, row=(y-90)/85;
    int index=row*2+column;
    if(index>=0 && index<(int)spoolTypes.size() && index<6 && findRoll(activeId)) {
      findRoll(activeId)->spoolWeight=spoolTypes[index].weight;
      findRoll(activeId)->spoolType=spoolTypes[index].name;
      saveDb();debugPrintf("[TOUCH] Spulenart: %s, Gewicht: %.0f g\n",spoolTypes[index].name.c_str(),spoolTypes[index].weight);
    }
    spoolMenu=false;draw();return;
  }
  if(!findRoll(activeId) && activeId.length() && y>=140 && y<=205 && x>=235) {
    uint8_t pick=constrain((x-240)/75,0,2);
    if(!entryStep) {
      const char* m[]= {
        "PLA","PETG","ABS"
      };
      selectedMaterial=m[pick];
      entryStep=1;
    }
    else {
      const char* h[]= {
        "eSUN","Prusament","Andere"
      };
      rolls.push_back( {
        activeId,selectedMaterial,h[pick],"#3b82f6",liveWeight,liveWeight
      });
      saveDb();
      entryStep=0;
    }
    draw();
    return;
  }
  if(y<340) {
    debugPrintln("[TOUCH] Kein Aktionsbutton");
    return;
  }
  Roll*r=findRoll(activeId);
  if(x<80) {
    debugPrintln("[TOUCH] Button: Tara");
    buttonPressed(6,344,74,62,"TARA");
    if(ENABLE_HX711)scale.tare(1);
    // Alte Mittelwerte dürfen nach einer Tara nicht weiter angezeigt werden.
    hxWeightSampleCount=0;
    hxWeightSampleIndex=0;
    liveWeight=0;
    draw();
    return;
  }
  if(!r && x>=80) {
    debugPrintln("[TOUCH] Wiegen gesperrt: keine aktive Rolle");
    return;
  }
  if(x>=80 && x<240) {
    debugPrintln("[TOUCH] Button: Wiegen");
    buttonPressed(84,344,r?152:230,62,"WIEGEN");
    if(r) {
      r->previous=r->weight;
      r->weight=max(0.0F,liveWeight-r->spoolWeight);
      saveDb();
      bool written=writeTag(*r);
      debugPrintf("[NFC] Daten nach Wiegen schreiben: %s\n",written?"OK":"FEHLER");
      draw();
    }
    else debugPrintln("[TOUCH] Keine aktive Rolle zum Wiegen");
    return;
  }
  if(r && x>=240) {
    debugPrintln("[TOUCH] Button: Spulengewicht");
    buttonPressed(240,344,74,62,"SPULE");
    spoolMenu=true;
    draw();
  }
}
const char WIFI_SETUP[] PROGMEM = R"HTML(<!doctype html><html lang=de><meta name=viewport content="width=device-width,initial-scale=1"><title>WLAN einrichten</title><style>body{margin:0;min-height:100vh;display:grid;place-items:center;background:#f4f7fb;font:16px system-ui;color:#102a56}.box{width:min(92vw,430px);background:#fff;border-radius:16px;padding:28px;box-shadow:0 12px 35px #102a5622}h1{margin:0 0 8px}p{color:#64748b}label{display:block;font-size:13px;font-weight:700;margin:18px 0 5px}select,input,button{box-sizing:border-box;width:100%;padding:12px;border-radius:8px;font:inherit}select,input{border:1px solid #cbd5e1}button{margin-top:20px;border:0;background:#2563eb;color:white;font-weight:700;cursor:pointer}.small{font-size:12px;margin-top:15px}</style><main class=box><h1>WLAN einrichten</h1><p>Wähle dein Netzwerk und gib nur das Passwort ein.</p><form id=f><label>Gefundene WLAN-Netzwerke</label><select id=ssid required><option>Lade Netzwerke&hellip;</option></select><label>WLAN-Passwort</label><input id=pass type=password required autocomplete=current-password placeholder="Passwort"><button>Verbinden</button></form><p class=small id=info>Suche nach WLANs&hellip;</p></main><script>const s=document.querySelector('#ssid'),i=document.querySelector('#info');async function scan(){try{let a=await(await fetch('/api/wifi/networks')).json();s.innerHTML=a.length?a.map(n=>`<option value="${n}">${n}</option>`).join(''):'<option>Keine Netzwerke gefunden</option>';i.textContent=a.length+' Netzwerk(e) gefunden'}catch(e){i.textContent='Suche fehlgeschlagen'}}document.querySelector('#f').onsubmit=async e=>{e.preventDefault();i.textContent='Verbindung wird gespeichert, ESP startet neu&hellip;';let r=await fetch('/api/wifi/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:s.value,password:document.querySelector('#pass').value})});if(!r.ok)i.textContent='Bitte Eingaben prüfen'};scan()</script></html>)HTML";
void startWifiSetup()  {
  wifiSetupMode=true;
  WiFi.mode(WIFI_AP_STA);
  String ap="FilamentScale-"+String((uint32_t)(ESP.getEfuseMac()&0xFFFFFF),HEX);
  WiFi.softAP(ap.c_str(),AP_PASSWORD);
  dns.start(53,"*",WiFi.softAPIP());
  debugPrintf("[WLAN] Einrichtung: %s | http://%s\n",ap.c_str(),WiFi.softAPIP().toString().c_str());
}
// NTP arbeitet intern stets in UTC. Die POSIX-Zeitzone berechnet MEZ/MESZ
// korrekt am jeweiligen Umschalttag (anders als ein festes Sommerzeit-Offset).
constexpr char NTP_TIMEZONE[] = "CET-1CEST,M3.5.0,M10.5.0/3";
bool timeIsValid()  {
  return time(nullptr) > 1700000000;
}
// Konfiguriert den gewählten Server plus zwei Fallbacks und protokolliert DNS.
void configureNtp(bool force=false)  {
  if(ntpConfigured && !force)return;
  if(WiFi.status()!=WL_CONNECTED)  {
    debugPrintln("[NTP] Kein WLAN – Abgleich wird wiederholt");
    return;
  }
  String server=preferences.getString("ntp","de.pool.ntp.org");
  server.trim();
  if(!server.length())server="de.pool.ntp.org";
  IPAddress address;
  if(WiFi.hostByName(server.c_str(),address)) debugPrintf("[NTP] DNS %s -> %s\n",server.c_str(),address.toString().c_str());
  else debugPrintf("[NTP] DNS-Fehler für %s\n",server.c_str());
  configTzTime(NTP_TIMEZONE,server.c_str(),"pool.ntp.org","time.google.com");
  debugPrintf("[NTP] Abgleich gestartet: %s\n",server.c_str());
  ntpConfigured=true;
  lastNtpAttempt=millis();
}
// Bei einem manuellen Abgleich wartet diese Funktion bis zu 20 Sekunden.
// Im Dauerbetrieb erfolgt der nächste Versuch über loop(), ohne das WebUI zu blockieren.
bool syncNtpAtStart(bool waitForAnswer=true)  {
  configureNtp(true);
  if(WiFi.status()!=WL_CONNECTED)return false;
  if(!waitForAnswer)return timeIsValid();
  tm t;
  unsigned long until=millis()+20000;
  while(millis()<until)  {
    if(getLocalTime(&t,1000))  {
      debugPrintf("[NTP] Zeit synchronisiert: %s\n",clockText().c_str());
      return true;
    }
    delay(250);
  }
  debugPrintln("[NTP] Keine Antwort. DNS/Router prüfen: UDP-Port 123 muss erlaubt sein.");
  return false;
}
// Prüft über einen standardisierten 204-Endpunkt, ob WLAN auch Internetzugang hat.
void checkInternet()  {
  if(WiFi.status()!=WL_CONNECTED) {
    internetAvailable=false;
    return;
  }
  HTTPClient http;
  http.setTimeout(3000);
  http.begin("http://connectivitycheck.gstatic.com/generate_204");
  int code=http.GET();
  internetAvailable=(code==204||code==200);
  http.end();
  debugPrintf("[NET] Internet: %s\n",internetAvailable?"erreichbar":"nicht erreichbar");
}
// Core 0: WLAN/WebUI laufen dort bereits intern. Die Internetprüfung wird
// bewusst ebenfalls dort ausgeführt, damit Core 1 frei für LCD, Touch, NFC
// und HX711 bleibt. Der Task wartet zwischen den Prüfungen 30 Sekunden.
void networkMonitorTask(void *) {
  for(;;) {
    checkInternet();
    vTaskDelay(pdMS_TO_TICKS(30000));
  }
}
String scannedNetworks()  {
  int n=WiFi.scanNetworks();
  String out="[";
  for(int i=0;
  i<n;
  i++) {
    if(i)out+=',';
    out+='\"';
    out+=esc(WiFi.SSID(i));
    out+='\"';
  }
  WiFi.scanDelete();
  return out+"]";
}
// Aktueller Zustand für die AJAX-Statusanzeige im WebUI.
String statusJson()  {
  Roll*r=findRoll(activeId);
  float shownWeight=fabsf(liveWeight)<(weightDecimals?0.05F:0.5F)?0.0F:liveWeight;
  String weight=hxHasSample?String(shownWeight,static_cast<unsigned int>(weightDecimals)):String(0.0F,static_cast<unsigned int>(weightDecimals));
  weight.replace(".",",");
  String s="{\"weight\":\""+weight+"\",\"id\":\""+esc(activeId)+"\",\"material\":\""+esc(r?r->material:"")+"\",\"maker\":\""+esc(r?r->maker:"")+"\",\"time\":\""+clockText()+"\",\"internet\":"+(internetAvailable?"true":"false")+",\"battery\":"+String(batteryPercent())+",\"voltage\":"+String(batteryVoltage(),2)+",\"ip\":\""+(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString())+"\"}";
  return s;
}
const char SETTINGS[] PROGMEM = R"HTML(<!doctype html><html lang=de><meta name=viewport content="width=device-width,initial-scale=1"><title>Einstellungen</title><style>body{max-width:700px;margin:30px auto;padding:0 18px;background:#f4f7fb;font:16px system-ui;color:#102a56}.card{background:#fff;padding:22px;margin:16px 0;border-radius:14px;border:1px solid #e2e8f0}h1,h2{margin-top:0}input,select,button{box-sizing:border-box;width:100%;padding:11px;margin:6px 0 12px;font:inherit;border-radius:8px;border:1px solid #cbd5e1}button{background:#2563eb;color:#fff;border:0;font-weight:700;cursor:pointer}a{color:#2563eb}label{font-size:13px;font-weight:bold}.static{display:none}</style><a href=/>&larr; Zur&uuml;ck zur Rollenverwaltung</a><h1>Einstellungen</h1><section class=card><h2>WLAN und IP-Adresse</h2><form id=net><label>WLAN</label><select id=ssid required></select><label>Neues WLAN-Passwort</label><input id=pass type=password placeholder="Leer lassen: gespeichertes Passwort behalten"><label><input id=dhcp type=checkbox checked style="width:auto"> IP automatisch per DHCP beziehen</label><div class=static id=static><label>IP-Adresse</label><input id=ip placeholder="192.168.1.50"><label>Gateway</label><input id=gw placeholder="192.168.1.1"><label>Subnetzmaske</label><input id=mask value="255.255.255.0"></div><button>Netzwerk speichern und neu starten</button></form></section><section class=card><h2>OTA-Firmware-Update</h2><p>Eine in PlatformIO erzeugte Datei <code>firmware.bin</code> auswählen.</p><form id=ota><input id=file type=file accept=.bin required><button>Update installieren</button></form><p id=msg></p></section><script>const $=s=>document.querySelector(s);async function scan(){let a=await(await fetch('/api/wifi/networks')).json();$('#ssid').innerHTML=a.map(x=>`<option>${x}</option>`).join('')}$('#dhcp').onchange=e=>$('#static').style.display=e.target.checked?'none':'block';$('#net').onsubmit=async e=>{e.preventDefault();let o={ssid:$('#ssid').value,password:$('#pass').value,dhcp:$('#dhcp').checked,ip:$('#ip').value,gw:$('#gw').value,mask:$('#mask').value};await fetch('/api/settings/network',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});$('#msg').textContent='Gespeichert – ESP startet neu.'};$('#ota').onsubmit=async e=>{e.preventDefault();let d=new FormData();d.append('firmware',$('#file').files[0]);$('#msg').textContent='Update wird übertragen…';let r=await fetch('/api/ota',{method:'POST',body:d});$('#msg').textContent=r.ok?'Update erfolgreich – ESP startet neu.':'Update fehlgeschlagen'};scan()</script></html>)HTML";
const char INDEX[] PROGMEM = R"HTML(<!doctype html><html lang=de><meta name=viewport content="width=device-width,initial-scale=1"><title>Filament Scale</title><style>:root{--bg:#f4f7fb;--ink:#14213d;--muted:#64748b;--brand:#2563eb;--green:#059669;--line:#e2e8f0}*{box-sizing:border-box}body{margin:0;background:var(--bg);font:15px system-ui,-apple-system,Segoe UI,sans-serif;color:var(--ink)}header{background:linear-gradient(120deg,#102a56,#2563eb);color:#fff;padding:34px max(22px,calc((100vw - 1040px)/2));display:flex;justify-content:space-between;align-items:center}h1{margin:0;font-size:28px;letter-spacing:-.5px}header p{margin:5px 0 0;opacity:.78}.pill{background:#ffffff24;padding:8px 12px;border-radius:99px;font-size:13px}.wrap{max-width:1040px;margin:25px auto;padding:0 18px}.grid{display:grid;grid-template-columns:340px 1fr;gap:20px}.card{background:#fff;border:1px solid var(--line);border-radius:15px;padding:21px;box-shadow:0 8px 24px #1e293b09}h2{font-size:17px;margin:0 0 16px}label{font-weight:650;font-size:13px;display:block;margin:12px 0 5px}input{width:100%;padding:11px 12px;border:1px solid #cbd5e1;border-radius:8px;font:inherit;outline:none}input:focus{border-color:var(--brand);box-shadow:0 0 0 3px #2563eb1c}button{border:0;border-radius:8px;padding:10px 13px;font:inherit;font-weight:650;cursor:pointer;background:var(--brand);color:#fff}.save{width:100%;margin-top:18px}.ghost{color:var(--brand);background:#eff6ff}.danger{color:#dc2626;background:#fef2f2}.tablewrap{overflow:auto}table{width:100%;border-collapse:collapse}th{text-align:left;color:var(--muted);font-size:12px;text-transform:uppercase;letter-spacing:.04em;padding:0 9px 10px}td{padding:13px 9px;border-top:1px solid var(--line)}.mat{font-weight:750;color:var(--brand)}.weight{font-variant-numeric:tabular-nums;font-weight:700}.actions{white-space:nowrap}.actions button{padding:7px 9px;margin-left:4px;font-size:13px}.empty{text-align:center;padding:36px;color:var(--muted)}#toast{position:fixed;right:20px;bottom:20px;background:#102a56;color:#fff;padding:12px 16px;border-radius:8px;opacity:0;transform:translateY(12px);transition:.2s}@media(max-width:720px){header{padding:24px 18px}.grid{grid-template-columns:1fr}.pill{display:none}}</style><header><div><h1>Filament Scale</h1><p>Rollenverwaltung &middot; ESP32</p></div><span class=pill id=status>Verbinde&hellip;</span></header><main class=wrap><div class=grid><section class=card><h2 id=formTitle>Neue Rolle anlegen</h2><form id=form><label>Eindeutige ID</label><input id=id name=id required placeholder="z. B. UID-04A1B2C3"><label>Material</label><input id=material name=material required placeholder="z. B. PLA"><label>Hersteller</label><input id=maker name=maker required placeholder="z. B. Prusament"><button class=save>Speichern</button><button type=button id=cancel class="ghost save" hidden>Abbrechen</button></form></section><section class=card><h2>Gespeicherte Rollen <span id=count></span></h2><div class=tablewrap><table><thead><tr><th>ID</th><th>Material</th><th>Hersteller</th><th>Letztes Gewicht</th><th>Aktion</th></tr></thead><tbody id=rows></tbody></table></div></section></div></main><div id=toast></div><script>const $=s=>document.querySelector(s),form=$('#form'),rows=$('#rows');let data=[];const esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));function note(s){let t=$('#toast');t.textContent=s;t.style.opacity=1;t.style.transform='none';setTimeout(()=>{t.style.opacity=0;t.style.transform='translateY(12px)'},2500)}function reset(){form.reset();$('#id').readOnly=false;$('#formTitle').textContent='Neue Rolle anlegen';$('#cancel').hidden=true}function render(){rows.innerHTML=data.length?data.map((r,i)=>`<tr><td>${esc(r.id)}</td><td class=mat>${esc(r.material)}</td><td>${esc(r.maker)}</td><td class=weight>${Number(r.weight).toFixed(1)} g</td><td class=actions><button class=ghost onclick="edit(${i})">Bearbeiten</button><button onclick="weigh('${encodeURIComponent(r.id)}')">Wiegen</button><button class=danger onclick="removeRoll('${encodeURIComponent(r.id)}')">Löschen</button></td></tr>`).join(''):'<tr><td class=empty colspan=5>Noch keine Rollen gespeichert.</td></tr>';$('#count').textContent=`(${data.length})`}async function load(){try{let r=await fetch('/api/rolls',{cache:'no-store'});data=await r.json();render();$('#status').textContent='System online'}catch(e){$('#status').textContent='Keine Verbindung'}}async function edit(i){let r=data[i];$('#id').value=r.id;$('#id').readOnly=true;$('#material').value=r.material;$('#maker').value=r.maker;$('#formTitle').textContent='Rolle bearbeiten';$('#cancel').hidden=false;scrollTo({top:0,behavior:'smooth'})}form.onsubmit=async e=>{e.preventDefault();let r=await fetch('/api/roll',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(Object.fromEntries(new FormData(form)))});if(r.ok){note('Rolle gespeichert');reset();load()}else note('Speichern fehlgeschlagen')};$('#cancel').onclick=reset;async function removeRoll(id){if(confirm('Diese Rolle wirklich löschen?')){await fetch('/api/roll?id='+id,{method:'DELETE'});note('Rolle gelöscht');load()}}async function weigh(id){await fetch('/api/weigh?id='+id,{method:'POST'});note('Gewicht übernommen');load()}load();setInterval(load,10000)</script></html>)HTML";
const char MAKER_DEFAULTS[] PROGMEM = R"HTML(<script>(()=>{const s=document.querySelector('#maker');if(!s)return;const base=['eSUN','Prusament','Bambu Lab','Polymaker','Sunlu','Anycubic','Creality'];const add=()=>base.forEach(x=>{if(![...s.options].some(o=>o.value===x)){let o=document.createElement('option');o.value=o.textContent=x;s.append(o)}});new MutationObserver(add).observe(s,{childList:true});add()})()</script>)HTML";
const char CUSTOM_GUARD[] PROGMEM = R"HTML(<script>(()=>{for(const s of [document.querySelector('#material'),document.querySelector('#maker')]){if(!s)continue;const add=()=>{if(![...s.options].some(o=>o.value==='__custom__')){let o=document.createElement('option');o.value='__custom__';o.textContent='+ Eigenes '+(s.id==='material'?'Material':'Hersteller')+' hinzufuegen';s.append(o)}};new MutationObserver(add).observe(s,{childList:true});add()}})()</script>)HTML";
const char ID_VISIBILITY[] PROGMEM = R"HTML(<script>(()=>{const id=document.querySelector('#id'),label=id&&id.previousElementSibling;if(!id||!label)return;const hide=()=>{id.required=false;id.hidden=true;label.hidden=true};hide();const old=window.edit;window.edit=i=>{old(i);id.hidden=false;label.hidden=false};document.querySelector('#cancel').addEventListener('click',hide);document.querySelector('#form').addEventListener('submit',()=>setTimeout(hide,50))})()</script>)HTML";
const char MATERIAL_PLUS[] PROGMEM = R"HTML(<script>(()=>{const s=document.querySelector('#material');if(!s)return;const add=()=>{if(![...s.options].some(o=>o.value==='PLA+')){let o=document.createElement('option');o.value=o.textContent='PLA+';s.insertBefore(o,s.options[1]||null)}};new MutationObserver(add).observe(s,{childList:true});add()})()</script>)HTML";
const char STATUS_EXTRA[] PROGMEM = R"HTML(<script>(()=>{const live=document.querySelector('.live');if(!live)return;live.insertAdjacentHTML('beforeend','<div class="card metric"><small>NTP-ZEIT</small><b id="ntpTime">--:--</b></div><div class="card metric"><small>BATTERIE</small><b id="battery">🔋 -- %</b></div>');async function updateExtra(){try{let s=await(await fetch('/api/status',{cache:"no-store"})).json();document.querySelector('#ntpTime').textContent=s.time;document.querySelector('#battery').textContent='🔋 '+s.battery+' % · '+s.voltage+' V'}catch(e){}}updateExtra();setInterval(updateExtra,5000)})()</script>)HTML";
const char COLOR_FIELD[] PROGMEM = R"HTML(<script>(()=>{const f=document.querySelector('#form'),mat=document.querySelector('#material'),maker=document.querySelector('#maker'),id=document.querySelector('#id');if(!f)return;let label=document.createElement('label');label.textContent='Rollenfarbe';let color=document.createElement('input');color.type='color';color.id='color';color.value='#3b82f6';label.append(color);f.append(label);const oldEdit=window.edit;window.edit=i=>{oldEdit(i);color.value=data[i].color||'#3b82f6'};f.onsubmit=async e=>{e.preventDefault();let customMat=document.querySelector('#materialCustom'),customMaker=document.querySelector('#makerCustom');let material=mat.value==='__custom__'?customMat.value.trim():mat.value;let manufacturer=maker.value==='__custom__'?customMaker.value.trim():maker.value;if(!material||!manufacturer){alert('Bitte Material und Hersteller wählen.');return}let r=await fetch('/api/roll',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id:id.value.trim(),material:material,maker:manufacturer,color:color.value})});if(!r.ok){alert('Speichern fehlgeschlagen');return}f.reset();color.value='#3b82f6';load()}})()</script>)HTML";
const char DASHBOARD3[] PROGMEM = R"HTML(<!doctype html><html lang=de><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>Filament Scale</title><style>body{font:15px system-ui;margin:0;background:#f5f7fb;color:#102a56}header{padding:20px;background:#173b78;color:#fff;display:flex;justify-content:space-between}.wrap{max-width:1050px;margin:20px auto;padding:0 16px}.cards,.grid{display:grid;gap:14px}.cards{grid-template-columns:repeat(5,1fr)}.grid{grid-template-columns:330px 1fr}.card{background:#fff;border-radius:12px;padding:16px;border:1px solid #dce4f0}small{color:#64748b;font-weight:bold}b{display:block;font-size:19px;margin-top:6px}label{display:block;margin-top:10px;font-size:13px;font-weight:bold}input,select,button{width:100%;box-sizing:border-box;padding:10px;border-radius:7px;border:1px solid #cbd5e1;font:inherit}button{margin-top:13px;background:#2563eb;color:#fff;border:0;font-weight:bold}.circle{display:inline-block;width:15px;height:15px;border-radius:50%;vertical-align:middle;margin-right:7px;border:1px solid #aaa}table{width:100%;border-collapse:collapse}td,th{padding:10px;border-bottom:1px solid #e4e8ef;text-align:left}.actions button{width:auto;margin:0 2px;padding:6px}@media(max-width:760px){.cards,.grid{grid-template-columns:1fr}.grid{display:block}.card{margin-bottom:12px}}</style><header><div><b>Filament Scale</b><small style="color:#dbeafe">ESP32 Rollenverwaltung</small></div><a href=/settings style="color:white">Einstellungen</a></header><main class=wrap><section class=cards><div class=card><small>NTP-ZEIT</small><b id=time>--:--</b></div><div class=card><small>BATTERIE</small><b id=bat>🔋 --</b></div><div class=card><small>GEWICHT</small><b id=weight>-- g</b></div><div class=card><small>AKTIVE ROLLE</small><b id=active>--</b></div><div class=card><small>IP</small><b id=ip>--</b></div></section><section class=grid><div class=card><h2 id=title>Rolle anlegen</h2><form id=f><label>Rollenfarbe</label><input id=color type=color value=#3b82f6><label>Material</label><select id=mat><option>PLA</option><option>PLA+</option><option>PETG</option><option>ABS</option><option>ASA</option><option>TPU</option><option>PA</option><option>PC</option><option value=own>Eigenes Material …</option></select><input id=ownMat hidden placeholder="Eigenes Material"><label>Hersteller</label><select id=maker></select><input id=ownMaker hidden placeholder="Eigener Hersteller"><button>Speichern</button><button type=button id=cancel hidden>Abbrechen</button></form></div><div class=card><h2>Gespeicherte Rollen</h2><table><thead><tr><th>Rolle</th><th>Material</th><th>Hersteller</th><th>Gewicht</th><th></th></tr></thead><tbody id=rows></tbody></table></div></section></main><script>const $=s=>document.querySelector(s);let data=[],editId='';const esc=x=>String(x).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));function options(sel,v,own){sel.innerHTML=v.map(x=>`<option>${esc(x)}</option>`).join('')+`<option value=own>Eigenes ${own} …</option>`}function formReset(){editId='';$('#f').reset();$('#ownMat').hidden=$('#ownMaker').hidden=true;$('#title').textContent='Rolle anlegen';$('#cancel').hidden=true}function render(){let makers=[...new Set(['eSUN','Prusament','Bambu Lab','Polymaker',...data.map(x=>x.maker)])];options($('#maker'),makers,'Hersteller');$('#rows').innerHTML=data.map(r=>`<tr><td><i class=circle style="background:${esc(r.color||'#3b82f6')}"></i>${esc(r.id)}</td><td>${esc(r.material)}</td><td>${esc(r.maker)}</td><td>${(+r.weight).toFixed(1)} g</td><td class=actions><button onclick="edit('${encodeURIComponent(r.id)}')">Bearbeiten</button></td></tr>`).join('')}async function load(){data=await(await fetch('/api/rolls')).json();render()}async function stat(){let s=await(await fetch('/api/status')).json();$('#time').textContent=s.time;$('#bat').textContent='🔋 '+s.battery+' %';$('#weight').textContent=s.weight+' g';$('#active').textContent=s.id||'--';$('#ip').textContent=s.ip}window.edit=id=>{let r=data.find(x=>x.id===decodeURIComponent(id));editId=r.id;$('#title').textContent='Rolle bearbeiten';$('#color').value=r.color||'#3b82f6';$('#mat').value=r.material;options($('#maker'),[...new Set(data.map(x=>x.maker))],'Hersteller');$('#maker').value=r.maker;$('#cancel').hidden=false};$('#mat').onchange=e=>$('#ownMat').hidden=e.target.value!=='own';$('#maker').onchange=e=>$('#ownMaker').hidden=e.target.value!=='own';$('#cancel').onclick=formReset;$('#f').onsubmit=async e=>{e.preventDefault();let material=$('#mat').value==='own'?$('#ownMat').value:$('#mat').value,maker=$('#maker').value==='own'?$('#ownMaker').value:$('#maker').value;if(!material||!maker)return alert('Material und Hersteller eingeben.');await fetch('/api/roll',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id:editId,material,maker,color:$('#color').value})});formReset();load()};load();stat();setInterval(()=>{load();stat()},5000)</script></html>)HTML";
const char SETTINGS_EXT[] PROGMEM = R"HTML(<script>(()=>{let ota=document.querySelector('#ota'),msg=document.querySelector('#msg');let section=document.createElement('section');section.className='card';section.innerHTML='<h2>NTP-Zeitserver</h2><p>Dieser Server liefert Zeit für automatisch erzeugte Rollen-IDs.</p><input id="ntp" value="de.pool.ntp.org"><button id="ntpSave">NTP-Server speichern</button>';document.querySelector('h1').after(section);document.querySelector('#ntpSave').onclick=async()=>{let r=await fetch('/api/settings/ntp',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({server:document.querySelector('#ntp').value})});msg.textContent=r.ok?'NTP-Server gespeichert.':'Speichern fehlgeschlagen.'};ota.onsubmit=async e=>{e.preventDefault();let d=new FormData();d.append('firmware',document.querySelector('#file').files[0]);msg.textContent='Update wird übertragen … Weiterleitung in 20 Sekunden.';let r=await fetch('/api/ota',{method:'POST',body:d});if(r.ok)setTimeout(()=>location.href='/',20000);else msg.textContent='Update fehlgeschlagen.'}})()</script>)HTML";
const char SETTINGS_SYNC[] PROGMEM = R"HTML(<script>(()=>{let b=document.createElement('button');b.textContent='NTP jetzt synchronisieren';document.querySelector('#ntpSave').after(b);b.onclick=async()=>{let r=await(await fetch('/api/settings/ntp/sync',{method:'POST'})).json();document.querySelector('#msg').textContent=r.ok?'Zeit synchronisiert: '+r.time:'Zeitserver nicht erreichbar.'}})()</script>)HTML";
const char SETTINGS_NET[] PROGMEM = R"HTML(<script>(()=>{let p=document.createElement('p');p.id='internetStatus';document.querySelector('h1').after(p);async function update(){let s=await(await fetch('/api/status')).json();p.textContent='Internetstatus: '+(s.internet?'✓ erreichbar':'✕ nicht erreichbar')+' | ESP-Zeit: '+s.time}update();setInterval(update,10000)})()</script>)HTML";
const char SETTINGS_NTP_VALUE[] PROGMEM = R"HTML(<script>(()=>{fetch('/api/settings/ntp').then(r=>r.json()).then(s=>{let n=document.querySelector('#ntp');if(n)n.value=s.server})})()</script>)HTML";
const char SETTINGS_TOUCH[] PROGMEM = R"HTML(<script>(()=>{let section=document.createElement('section'),b=document.createElement('button');section.className='card';section.id='tools';section.innerHTML='<h2>Tools</h2><p>Kalibriert die Touchpositionen dauerhaft für dieses LCD.</p>';b.textContent='Touchscreen kalibrieren';b.onclick=async()=>{if(!confirm('Die Kalibrierung wird auf dem LCD gestartet.'))return;await fetch('/api/touch/calibrate',{method:'POST'});document.querySelector('#msg').textContent='Bitte die vier Fadenkreuze auf dem LCD nacheinander berühren.'};section.append(b);document.querySelector('#ota').closest('.card').before(section)})()</script>)HTML";
const char SETTINGS_WEIGHT_FORMAT[] PROGMEM = R"HTML(<script>(()=>{let tools=document.querySelector('#tools');if(!tools)return;let box=document.createElement('div');box.innerHTML='<label>Gewichtsanzeige</label><select id="weightDecimals"><option value="1">Eine Nachkommastelle (z. B. 123,4 g)</option><option value="0">Ganze Gramm (z. B. 123 g)</option></select><button id="weightDecimalsSave">Rundung speichern</button>';tools.append(box);let select=document.querySelector('#weightDecimals'),msg=document.querySelector('#msg');fetch('/api/settings/weight-format').then(r=>r.json()).then(x=>select.value=x.decimals);document.querySelector('#weightDecimalsSave').onclick=async()=>{let r=await fetch('/api/settings/weight-format',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({decimals:Number(select.value)})});msg.textContent=r.ok?'Rundung gespeichert.':'Speichern fehlgeschlagen.'}})()</script>)HTML";
const char WEB_WEIGHT_FORMAT[] PROGMEM = R"HTML(<script>(()=>{fetch('/api/settings/weight-format').then(r=>r.json()).then(s=>{let decimals=s.decimals||0,oldRender=window.render;window.render=()=>{oldRender();document.querySelectorAll('#rows tr').forEach((row,i)=>{if(data[i]&&row.cells[3])row.cells[3].textContent=Number(data[i].weight).toFixed(decimals).replace('.',',')+' g'})};window.render()})})()</script>)HTML";
const char SETTINGS_HX711[] PROGMEM = R"HTML(<script>(()=>{let section=document.createElement('section');section.className='card';section.innerHTML='<h2>HX711 kalibrieren</h2><p>Bekanntes Gewicht auflegen und den berechneten Kalibrierwert eintragen.</p><label>Kalibrierwert</label><input id="hxCalibration" type="number" step="0.01"><button id="hxSave">Kalibrierwert speichern</button>';document.querySelector('#ota').closest('.card').before(section);let input=document.querySelector('#hxCalibration'),msg=document.querySelector('#msg');fetch('/api/settings/hx711').then(r=>r.json()).then(x=>input.value=x.factor);document.querySelector('#hxSave').onclick=async()=>{let r=await fetch('/api/settings/hx711',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({factor:Number(input.value)})});msg.textContent=r.ok?'HX711-Kalibrierwert gespeichert und aktiv.':'Ungültiger Kalibrierwert.'}})()</script>)HTML";
const char COLOR_STYLE[] PROGMEM = R"HTML(<style>#color{appearance:none;-webkit-appearance:none;width:54px;height:54px;padding:0;border:3px solid #fff;border-radius:50%;box-shadow:0 0 0 2px #2563eb;cursor:pointer}#color::-webkit-color-swatch-wrapper{padding:0}#color::-webkit-color-swatch{border:0;border-radius:50%}</style>)HTML";
const char WEB_ACTIONS[] PROGMEM = R"HTML(<script>(()=>{const f=document.querySelector('#f');if(!f)return;const editor=f.closest('.card'),title=document.querySelector('#title');f.hidden=true;title.textContent='NFC-Rolle bearbeiten';let read=document.createElement('button');read.type='button';read.textContent='NFC lesen und bearbeiten';read.onclick=async()=>{let r=await(await fetch('/api/nfc/read',{method:'POST'})).json();if(r.id)window.edit(encodeURIComponent(r.id));else alert('Kein NFC-Tag erkannt')};editor.insertBefore(read,f);let box=document.createElement('div');box.hidden=true;box.innerHTML='<button type="button" id="webWeigh">Wiegen</button><button type="button" id="webDelete" style="background:#b91c1c">Löschen</button>';f.append(box);window.deleteRoll=async id=>{if(!confirm('Diese Rolle wirklich löschen?'))return;await fetch('/api/roll?id='+encodeURIComponent(id),{method:'DELETE'});formReset();f.hidden=true;box.hidden=true;title.textContent='NFC-Rolle bearbeiten';load()};const oldRender=window.render;window.render=()=>{oldRender();document.querySelectorAll('#rows tr').forEach((row,i)=>{if(!data[i])return;let b=document.createElement('button');b.textContent='Löschen';b.style.background='#b91c1c';b.onclick=()=>window.deleteRoll(data[i].id);row.querySelector('.actions').append(b)})};const old=window.edit;window.edit=id=>{old(id);f.hidden=false;box.hidden=false;let r=data.find(x=>x.id===decodeURIComponent(id));document.querySelector('#webWeigh').onclick=async()=>{await fetch('/api/weigh?id='+encodeURIComponent(r.id),{method:'POST'});load()};document.querySelector('#webDelete').onclick=()=>window.deleteRoll(r.id)};document.querySelector('#cancel').onclick=()=>{formReset();f.hidden=true;box.hidden=true;title.textContent='NFC-Rolle bearbeiten'};load()})()</script>)HTML";
const char WEB_CUSTOM_OPTIONS[] PROGMEM = R"HTML(<script>(()=>{let makers=[],materials=[];const add=(s,items)=>items.forEach(v=>{if(v&&![...s.options].some(o=>o.value===v)){let o=document.createElement('option');o.value=o.textContent=v;s.insertBefore(o,[...s.options].find(o=>o.value==='own')||null)}});const apply=()=>{add(document.querySelector('#maker'),makers);add(document.querySelector('#mat'),materials)};const previousRender=window.render;window.render=()=>{previousRender();apply()};const previousEdit=window.edit;window.edit=id=>{previousEdit(id);apply();let r=data.find(x=>x.id===decodeURIComponent(id));if(r){document.querySelector('#maker').value=r.maker;document.querySelector('#mat').value=r.material}};fetch('/api/options').then(r=>r.json()).then(o=>{makers=o.makers||[];materials=o.materials||[];apply()})})()</script>)HTML";
const char WEB_EDIT_PRESERVE[] PROGMEM = R"HTML(<script>(()=>{const oldRender=window.render;window.render=()=>{oldRender();if(!editId)return;let r=data.find(x=>x.id===editId);if(!r)return;let maker=document.querySelector('#maker'),mat=document.querySelector('#mat');if(![...maker.options].some(o=>o.value===r.maker)){let o=document.createElement('option');o.value=o.textContent=r.maker;maker.append(o)}if(![...mat.options].some(o=>o.value===r.material)){let o=document.createElement('option');o.value=o.textContent=r.material;mat.insertBefore(o,[...mat.options].find(o=>o.value==='own')||null)}maker.value=r.maker;mat.value=r.material}})()</script>)HTML";
const char WEB_LOCK_EDIT_DROPDOWNS[] PROGMEM = R"HTML(<script>(()=>{const oldRender=window.render;window.render=()=>{if(!editId){oldRender();return}let maker=document.querySelector('#maker'),mat=document.querySelector('#mat'),ownMaker=document.querySelector('#ownMaker'),ownMat=document.querySelector('#ownMat');let state={makerHtml:maker.innerHTML,makerValue:maker.value,matHtml:mat.innerHTML,matValue:mat.value,ownMaker:ownMaker.value,ownMakerHidden:ownMaker.hidden,ownMat:ownMat.value,ownMatHidden:ownMat.hidden};oldRender();maker.innerHTML=state.makerHtml;maker.value=state.makerValue;mat.innerHTML=state.matHtml;mat.value=state.matValue;ownMaker.value=state.ownMaker;ownMaker.hidden=state.ownMakerHidden;ownMat.value=state.ownMat;ownMat.hidden=state.ownMatHidden}})()</script>)HTML";
const char WEB_ACTIVE_HIGHLIGHT[] PROGMEM = R"HTML(<style>#rows tr.active-roll{background:#eff6ff;box-shadow:inset 5px 0 0 var(--roll-color,#2563eb)}#rows tr.active-roll td:first-child{font-weight:700}</style><script>(()=>{const mark=()=>{let id=document.querySelector('#active').textContent;document.querySelectorAll('#rows tr').forEach((row,i)=>{let r=data[i];if(!r)return;let active=r.id===id;row.classList.toggle('active-roll',active);if(active)row.style.setProperty('--roll-color',r.color||'#2563eb')})};const oldRender=window.render;window.render=()=>{oldRender();mark()};setInterval(mark,700)})()</script>)HTML";
const char WEB_SPOOL_WEIGHT[] PROGMEM = R"HTML(<script>(()=>{const f=document.querySelector('#f');let label=document.createElement('label'),input=document.createElement('input');label.textContent='Spulengewicht / Offset (g)';input.id='spoolWeight';input.type='number';input.min='0';input.step='1';input.value='0';label.append(input);f.insertBefore(label,document.querySelector('#mat').previousElementSibling);const oldEdit=window.edit;window.edit=id=>{oldEdit(id);let r=data.find(x=>x.id===decodeURIComponent(id));input.value=r&&r.spoolWeight||0};f.onsubmit=async e=>{e.preventDefault();let material=document.querySelector('#mat').value==='own'?document.querySelector('#ownMat').value:document.querySelector('#mat').value,maker=document.querySelector('#maker').value==='own'?document.querySelector('#ownMaker').value:document.querySelector('#maker').value;if(!editId||!material||!maker)return alert('Bitte eine NFC-Rolle, Material und Hersteller wählen.');let r=await fetch('/api/roll',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id:editId,material,maker,color:document.querySelector('#color').value,spoolWeight:+input.value||0})});if(!r.ok)return alert('Speichern fehlgeschlagen');formReset();f.hidden=true;document.querySelector('#webWeigh').parentElement.hidden=true;document.querySelector('#title').textContent='NFC-Rolle bearbeiten';load()}})()</script>)HTML";
const char WEB_SPOOL_TYPES[] PROGMEM = R"HTML(<script>(()=>{const f=document.querySelector('#f'),weight=document.querySelector('#spoolWeight');let select=document.createElement('select'),label=document.createElement('label');label.textContent='Spulenart';select.id='spoolType';label.append(select);f.insertBefore(label,weight.parentElement);let types=[];const fill=()=>{select.innerHTML=types.map(t=>`<option value="${t.name}">${t.name} – ${t.weight} g</option>`).join('')};const refresh=async()=>{types=await(await fetch('/api/spools')).json();fill()};select.onchange=()=>{let t=types.find(x=>x.name===select.value);if(t)weight.value=t.weight};const oldEdit=window.edit;window.edit=id=>{oldEdit(id);let r=data.find(x=>x.id===decodeURIComponent(id));if(r){if(!types.some(t=>t.name===r.spoolType))types.push({name:r.spoolType,weight:r.spoolWeight});fill();select.value=r.spoolType;weight.value=r.spoolWeight}};f.onsubmit=async e=>{e.preventDefault();let material=document.querySelector('#mat').value==='own'?document.querySelector('#ownMat').value:document.querySelector('#mat').value,maker=document.querySelector('#maker').value==='own'?document.querySelector('#ownMaker').value:document.querySelector('#maker').value;if(!editId||!material||!maker)return alert('Bitte eine NFC-Rolle, Material und Hersteller wählen.');let r=await fetch('/api/roll',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id:editId,material,maker,color:document.querySelector('#color').value,spoolType:select.value,spoolWeight:+weight.value||0})});if(!r.ok)return alert('Speichern fehlgeschlagen');formReset();f.hidden=true;document.querySelector('#webWeigh').parentElement.hidden=true;document.querySelector('#title').textContent='NFC-Rolle bearbeiten';load()};let card=document.createElement('section');card.className='card';card.innerHTML='<h2>Spulenarten</h2><p>Spulenart und Leergewicht verwalten.</p><form id=spoolForm><label>Spulenart</label><input id=spoolName required placeholder="z. B. Bambu Kunststoff"><label>Spulengewicht (g)</label><input id=spoolValue type=number min=0 step=1 required><button>Spulenart speichern</button></form><div id=spoolList></div>';document.querySelector('.wrap').append(card);const list=async()=>{types=await(await fetch('/api/spools')).json();fill();let box=document.querySelector('#spoolList');box.innerHTML='';types.forEach(t=>{let row=document.createElement('p'),del=document.createElement('button');row.textContent=t.name+' – '+t.weight+' g ';del.textContent='Löschen';del.onclick=async()=>{await fetch('/api/spools?name='+encodeURIComponent(t.name),{method:'DELETE'});list()};row.append(del);box.append(row)})};document.querySelector('#spoolForm').onsubmit=async e=>{e.preventDefault();await fetch('/api/spools',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:document.querySelector('#spoolName').value,weight:+document.querySelector('#spoolValue').value||0})});e.target.reset();list()};refresh();list()})()</script>)HTML";
const char WEB_SPOOL_NAV[] PROGMEM = R"HTML(<style>header details{position:relative;margin-left:auto}header summary{list-style:none;cursor:pointer;font-size:26px;line-height:22px;padding:2px 8px}header summary::-webkit-details-marker{display:none}header details nav{position:absolute;right:0;top:36px;width:180px;background:#fff;border-radius:8px;padding:7px;box-shadow:0 8px 20px #0004;z-index:5}header details nav a{display:block;color:#102a56!important;padding:10px;text-decoration:none}</style><script>(()=>{document.querySelector('.wrap>section.card:last-child')?.remove();let header=document.querySelector('header');header.querySelector('a[href="/settings"]').style.display='none';let menu=document.createElement('details');menu.innerHTML='<summary>☰</summary><nav><a href="/settings">Einstellungen</a><a href="/spools">Spulenarten</a></nav>';header.append(menu)})()</script>)HTML";
const char SPOOLS_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang=de><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1"><title>Spulenarten</title><style>body{max-width:700px;margin:30px auto;padding:0 18px;background:#f5f7fb;font:16px system-ui;color:#102a56}.card{background:#fff;border:1px solid #dce4f0;border-radius:12px;padding:18px;margin:16px 0}input,button{box-sizing:border-box;width:100%;padding:10px;margin:6px 0;border-radius:7px;border:1px solid #cbd5e1;font:inherit}button{background:#2563eb;color:#fff;border:0;font-weight:bold}li{display:flex;justify-content:space-between;gap:12px;padding:10px;border-bottom:1px solid #e4e8ef}li button{width:auto;margin:0;background:#b91c1c}</style><a href=/>&larr; Zurück</a><h1>Spulenarten</h1><section class=card><p>Spulenart und Leergewicht verwalten.</p><form id=f><label>Spulenart</label><input id=name required placeholder="z. B. Bambu Kunststoff"><label>Spulengewicht (g)</label><input id=weight type=number min=0 step=1 required><button>Speichern</button></form></section><section class=card><h2>Gespeicherte Spulenarten</h2><ul id=list></ul></section><script>const $=s=>document.querySelector(s);async function load(){let data=await(await fetch('/api/spools')).json();let list=$('#list');list.innerHTML='';data.forEach(x=>{let li=document.createElement('li'),b=document.createElement('button');li.append(document.createTextNode(x.name+' – '+x.weight+' g'));b.textContent='Löschen';b.onclick=async()=>{await fetch('/api/spools?name='+encodeURIComponent(x.name),{method:'DELETE'});load()};li.append(b);list.append(li)})}$('#f').onsubmit=async e=>{e.preventDefault();await fetch('/api/spools',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({name:$('#name').value,weight:+$('#weight').value||0})});e.target.reset();load()};load()</script></html>)HTML";
void web()  {
  server.on("/",HTTP_GET,[](AsyncWebServerRequest*q) {
    if(wifiSetupMode) {
      q->send(200,"text/html; charset=utf-8",WIFI_SETUP);
      return;
    }
    AsyncResponseStream*r=q->beginResponseStream("text/html; charset=utf-8");
    r->print(DASHBOARD3);
    r->print(COLOR_STYLE);
    r->print(WEB_ACTIONS);
    r->print(WEB_CUSTOM_OPTIONS);
    r->print(WEB_EDIT_PRESERVE);
    r->print(WEB_LOCK_EDIT_DROPDOWNS);
    r->print(WEB_ACTIVE_HIGHLIGHT);
    r->print(WEB_SPOOL_WEIGHT);
    r->print(WEB_SPOOL_TYPES);
    r->print(WEB_SPOOL_NAV);
    r->print(WEB_WEIGHT_FORMAT);
    q->send(r);
  });
  server.on("/spools",HTTP_GET,[](AsyncWebServerRequest*q) { q->send(200,"text/html; charset=utf-8",SPOOLS_PAGE); });
  server.on("/setup",HTTP_GET,[](AsyncWebServerRequest*q) {
    q->send(200,"text/html; charset=utf-8",WIFI_SETUP);
  });
  server.on("/settings",HTTP_GET,[](AsyncWebServerRequest*q) {
    AsyncResponseStream*r=q->beginResponseStream("text/html; charset=utf-8");
    r->print(SETTINGS);
    r->print(SETTINGS_EXT);
    r->print(SETTINGS_SYNC);
    r->print(SETTINGS_NET);
    r->print(SETTINGS_NTP_VALUE);
    r->print(SETTINGS_TOUCH);
    r->print(SETTINGS_WEIGHT_FORMAT);
    r->print(SETTINGS_HX711);
    r->printf("<p>Firmware-Version: %s</p>",FIRMWARE_VERSION);
    q->send(r);
  });
  server.on("/api/status",HTTP_GET,[](AsyncWebServerRequest*q) {
    q->send(200,"application/json",statusJson());
  });
  server.on("/api/wifi/networks",HTTP_GET,[](AsyncWebServerRequest*q) {
    q->send(200,"application/json",scannedNetworks());
  });
  server.on("/api/wifi/connect",HTTP_POST,[](AsyncWebServerRequest*q, JsonVariant &body) {
    JsonObject j=body.as<JsonObject>();
    String ssid=j["ssid"]|"",pass=j["password"]|"";
    if(!ssid.length()||!pass.length()) {
      q->send(400);
      return;
    }
    preferences.putString("ssid",ssid);
    preferences.putString("pass",pass);
    debugPrintf("[WLAN] Zugang fuer %s gespeichert; Neustart\n",ssid.c_str());
    restartAt=millis()+1500;
    q->send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/settings/network",HTTP_POST,[](AsyncWebServerRequest*q, JsonVariant &body) {
    JsonObject j=body.as<JsonObject>();
    String ssid=j["ssid"]|"",pass=j["password"]|"";
    if(!ssid.length()) {
      q->send(400);
      return;
    }
    preferences.putString("ssid",ssid);
    if(pass.length())preferences.putString("pass",pass);
    bool dhcp=j["dhcp"]|true;
    preferences.putBool("dhcp",dhcp);
    preferences.putString("ip",j["ip"]|"");
    preferences.putString("gw",j["gw"]|"");
    preferences.putString("mask",j["mask"]|"");
    debugPrintf("[WLAN] Einstellungen fuer %s gespeichert; DHCP: %s\n",ssid.c_str(),dhcp?"an":"aus");
    restartAt=millis()+1500;
    q->send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/rolls",HTTP_GET,[](AsyncWebServerRequest*q) {
    q->send(200,"application/json",rollsJson());
  });
  server.on("/api/options",HTTP_GET,[](AsyncWebServerRequest*q) {
    q->send(200,"application/json",String("{\"makers\":")+jsonValues(customMakers)+",\"materials\":"+jsonValues(customMaterials)+"}");
  });
  server.on("/api/spools",HTTP_GET,[](AsyncWebServerRequest*q) { q->send(200,"application/json",spoolsJson()); });
  server.on("/api/spools",HTTP_POST,[](AsyncWebServerRequest*q,JsonVariant &body) {
    JsonObject data=body.as<JsonObject>();String name=data["name"]|"";name.trim();float weight=max(0.0F,data["weight"]|0.0F);
    if(!name.length()){q->send(400);return;}bool found=false;
    for(SpoolType &type:spoolTypes)if(type.name.equalsIgnoreCase(name)){type.name=name;type.weight=weight;found=true;break;}
    if(!found)spoolTypes.push_back({name,weight});saveSpoolTypes();q->send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/spools",HTTP_DELETE,[](AsyncWebServerRequest*q) { String name=q->arg("name");for(auto it=spoolTypes.begin();it!=spoolTypes.end();++it)if(it->name==name){spoolTypes.erase(it);break;}if(spoolTypes.empty())spoolTypes.push_back({"Standard",0});saveSpoolTypes();q->send(200); });
  server.on("/api/roll",HTTP_POST,[](AsyncWebServerRequest*q, JsonVariant &body) {
    JsonObject j=body.as<JsonObject>();
    String id=j["id"]|"";
    // Rollen entstehen ausschließlich durch eine erkannte NFC-ID.
    // Das WebUI darf anschließend nur bestehende Rollen bearbeiten.
    if(!id.length()) {
      q->send(400,"application/json","{\"error\":\"NFC-ID erforderlich\"}");
      return;
    }
    Roll*r=findRoll(id);
    if(!r) {
      rolls.push_back( {
        id
      });
      r=&rolls.back();
    }
    r->material=j["material"]|"";
    r->maker=j["maker"]|"";
    r->color=j["color"]|"#3b82f6";
    if(j["spoolWeight"].is<float>())r->spoolWeight=max(0.0F,j["spoolWeight"].as<float>());
    if(j["spoolType"].is<const char*>())r->spoolType=j["spoolType"].as<String>();
    rememberCustomValue("makers",customMakers,r->maker);
    rememberCustomValue("materials",customMaterials,r->material);
    saveDb();
    q->send(200,"application/json",String("{\"id\":\"")+id+"\"}");
  });
  server.on("/api/roll",HTTP_DELETE,[](AsyncWebServerRequest*q) {
    String id=q->arg("id");
    for(auto it=rolls.begin();
    it!=rolls.end();
    ++it)if(it->id==id) {
      rolls.erase(it);
      saveDb();
      break;
    }
    q->send(200);
  });
  server.on("/api/weigh",HTTP_POST,[](AsyncWebServerRequest*q) {
    String id=q->arg("id");
    if(Roll*r=findRoll(id)) {
      r->previous=r->weight;
      r->weight=q->hasArg("weight")?q->arg("weight").toFloat():max(0.0F,liveWeight-r->spoolWeight);
      saveDb();
      bool written=writeTag(*r);
      debugPrintf("[NFC] Daten nach Web-Wiegen schreiben: %s\n",written?"OK":"FEHLER");
    }
    q->send(200);
  });
  server.on("/api/nfc/read",HTTP_POST,[](AsyncWebServerRequest*q) {
    String id=readTag();
    if(id.length()) {
      activeId=id;
      addUnknownRoll(id);
      draw();
    }
    q->send(200,"application/json",String("{\"id\":\"")+esc(id)+"\",\"exists\":"+(findRoll(id)?"true":"false")+"}");
  });
  server.on("/api/touch/calibrate",HTTP_POST,[](AsyncWebServerRequest*q) {
    touchCalStep=0;
    debugPrintln("[TOUCH] Kalibrierung gestartet");
    draw();
    q->send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/settings/ntp",HTTP_POST,[](AsyncWebServerRequest*q,JsonVariant &body) {
    String ntp=body.as<JsonObject>()["server"]|"de.pool.ntp.org";
    ntp.trim();
    if(!ntp.length())ntp="de.pool.ntp.org";
    preferences.putString("ntp",ntp);
    ntpConfigured=false;
    restartAt=0;
    configureNtp(true);
    q->send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/settings/ntp",HTTP_GET,[](AsyncWebServerRequest*q) {
    String ntp=preferences.getString("ntp","de.pool.ntp.org");
    q->send(200,"application/json",String("{\"server\":\"")+esc(ntp)+"\",\"time\":\""+clockText()+"\"}");
  });
  server.on("/api/settings/ntp/sync",HTTP_POST,[](AsyncWebServerRequest*q) {
    restartAt=0;
    ntpConfigured=false;
    bool ok=syncNtpAtStart(true);
    q->send(200,"application/json",String("{\"ok\":")+(ok?"true":"false")+",\"time\":\""+clockText()+"\"}");
  });
  server.on("/api/settings/hx711",HTTP_GET,[](AsyncWebServerRequest*q) {
    q->send(200,"application/json",String("{\"factor\":")+String(hxCalibration,2)+"}");
  });
  server.on("/api/settings/hx711",HTTP_POST,[](AsyncWebServerRequest*q,JsonVariant &body) {
    float factor=body.as<JsonObject>()["factor"]|0.0F;
    if(fabsf(factor)<1.0F || fabsf(factor)>1000000.0F) {
      q->send(400,"application/json","{\"ok\":false}");
      return;
    }
    hxCalibration=factor;
    preferences.putFloat("hxcal",hxCalibration);
    if(ENABLE_HX711)scale.set_scale(hxCalibration);
    debugPrintf("[HX711] Kalibrierwert via WebUI gesetzt: %.2f\n",hxCalibration);
    q->send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/settings/weight-format",HTTP_GET,[](AsyncWebServerRequest*q) {
    q->send(200,"application/json",String("{\"decimals\":")+String(weightDecimals)+"}");
  });
  server.on("/api/settings/weight-format",HTTP_POST,[](AsyncWebServerRequest*q,JsonVariant &body) {
    int decimals=body.as<JsonObject>()["decimals"]|1;
    if(decimals!=0 && decimals!=1) {
      q->send(400,"application/json","{\"ok\":false}");
      return;
    }
    weightDecimals=decimals;
    preferences.putUChar("wdec",weightDecimals);
    q->send(200,"application/json","{\"ok\":true}");
  });
  server.on("/api/ota",HTTP_POST,[](AsyncWebServerRequest*q) {
    bool ok=!Update.hasError();
    q->send(ok?200:500,"application/json",ok?"{\"ok\":true}":"{\"ok\":false}");
    if(ok)restartAt=millis()+10000;
  },[](AsyncWebServerRequest*,String,size_t index,uint8_t *data,size_t len,bool final) {
    if(!index) {
      debugPrintln("[OTA] Update gestartet");
      Update.begin(UPDATE_SIZE_UNKNOWN);
    }
    if(!Update.hasError())Update.write(data,len);
    if(final) {
      if(Update.end(true))debugPrintln("[OTA] Update OK");
      else debugPrintf("[OTA] Fehler: %u\n",Update.getError());
    }
  });
  server.onNotFound([](AsyncWebServerRequest*q) {
    if(wifiSetupMode)q->redirect("/setup");
    else q->send(404,"text/plain","Nicht gefunden");
  });
  server.begin();
}
// Hardwarestart: Display/Touch, Status-LED, Waage, SD, NFC und Netzwerk.
void setup()  {
  Serial.begin(115200);
  delay(300);
  debugPrintf("\n=== Filament-Rollenwaage v%s startet ===\n",FIRMWARE_VERSION);
  SPI.begin(PIN_SPI_SCK,PIN_SPI_MISO,PIN_SPI_MOSI);
  pinMode(PIN_TFT_BL,OUTPUT);
  digitalWrite(PIN_TFT_BL,HIGH);
  pinMode(PIN_RGB_RED,OUTPUT);
  pinMode(PIN_RGB_GREEN,OUTPUT);
  pinMode(PIN_RGB_BLUE,OUTPUT);
  rgbOff();
  preferences.begin("filament",false);
  hxCalibration=preferences.getFloat("hxcal",HX711_CALIBRATION);
  weightDecimals=preferences.getUChar("wdec",1);
  if(weightDecimals>1)weightDecimals=1;
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BATTERY_ADC,ADC_11db);
  screen.init();
  screen.setRotation(DISPLAY_ROTATION);
  pinMode(PIN_TOUCH_CS,OUTPUT);
  digitalWrite(PIN_TOUCH_CS,HIGH);
  pinMode(PIN_TOUCH_IRQ,INPUT);
  debugPrintf("[SPI] LCD/Touch: SCK=%d MISO=%d MOSI=%d | Touch CS=%d IRQ=%d\n",PIN_SPI_SCK,PIN_SPI_MISO,PIN_SPI_MOSI,PIN_TOUCH_CS,PIN_TOUCH_IRQ);
  if(ENABLE_HX711) {
    scale.begin(PIN_HX711_DOUT,PIN_HX711_SCK);
    scale.set_scale(hxCalibration);
    scale.tare(1);
    debugPrintf("[HX711] Aktiv, Tara gesetzt, Kalibrierwert: %.2f\n",hxCalibration);
  }
  else debugPrintln("[HX711] Deaktiviert via Config");
  sdSpi.begin(PIN_SD_SCK,PIN_SD_MISO,PIN_SD_MOSI,PIN_SD_CS);
  if(ENABLE_SD_CARD) {
    sdReady=SD.begin(PIN_SD_CS,sdSpi);
    debugPrintf("[SD] %s\n",sdReady?"Karte bereit":"FEHLER: Karte nicht erkannt, Datenbank nur temporaer");
  }
  else debugPrintln("[SD] Deaktiviert via Config");
  loadDb();
  if(ENABLE_PN532 && PN532_I2C_ONLY) {
    Wire.begin(PIN_I2C_SDA,PIN_I2C_SCL);
    nfc.begin();
    uint32_t fw=nfc.getFirmwareVersion();
    nfcReady=fw!=0;
    debugPrintf("[PN532] %s\n",nfcReady?"bereit":"FEHLER: nicht erkannt");
    if(nfcReady)nfc.SAMConfig();
  }
  else debugPrintln("[PN532] Deaktiviert via Config");
  loadCustomValues("makers",customMakers);
  loadCustomValues("materials",customMaterials);
  loadSpoolTypes();
  touchCal.valid=preferences.getBool("tcal",false);
  if(touchCal.valid) {
    touchCal.ax=preferences.getFloat("tax");
    touchCal.bx=preferences.getFloat("tbx");
    touchCal.cx=preferences.getFloat("tcx");
    touchCal.ay=preferences.getFloat("tay");
    touchCal.by=preferences.getFloat("tby");
    touchCal.cy=preferences.getFloat("tcy");
    debugPrintln("[TOUCH] Gespeicherte Kalibrierung geladen");
  }
  String ssid=preferences.getString("ssid"),pass=preferences.getString("pass");
  WiFi.mode(WIFI_STA);
  if(ssid.length()) {
    bool dhcp=preferences.getBool("dhcp",true);
    if(!dhcp) {
      IPAddress ip,gw,mask;
      if(ip.fromString(preferences.getString("ip"))&&gw.fromString(preferences.getString("gw"))&&mask.fromString(preferences.getString("mask"))) {
        WiFi.config(ip,gw,mask);
        debugPrintf("[WLAN] Statische IP: %s\n",ip.toString().c_str());
      }
      else debugPrintln("[WLAN] Ungueltige statische IP, verwende DHCP");
    }
    debugPrintf("[WLAN] Verbinde mit gespeichertem Netzwerk %s\n",ssid.c_str());
    WiFi.begin(ssid.c_str(),pass.c_str());
    unsigned long until=millis()+15000;
    while(WiFi.status()!=WL_CONNECTED&&millis()<until)delay(250);
  }
  if(WiFi.status()==WL_CONNECTED)debugPrintf("[WLAN] Verbunden: http://%s\n",WiFi.localIP().toString().c_str());
  else startWifiSetup();
  syncNtpAtStart();
  web();
  xTaskCreatePinnedToCore(networkMonitorTask,"Netzwerk",4096,nullptr,1,&networkTaskHandle,0);
  debugPrintln("[CPU] Core 0: WLAN/WebUI/Internet | Core 1: LCD/Touch/NFC/HX711");
  debugPrintln("[WEB] Server gestartet");
  draw();
}
void loop()  {
  if(!timeIsValid() && WiFi.status()==WL_CONNECTED && millis()-lastNtpAttempt>15000) syncNtpAtStart(false);
  if(restartAt&&millis()>=restartAt)ESP.restart();
  if(wifiSetupMode)dns.processNextRequest();
  // Touch hat Vorrang vor Wiegen, NFC und LCD-Refresh. Dadurch reagieren die
  // Buttons sofort, auch wenn gerade eine Messung oder NFC-Suche ansteht.
  handleTouch();
  hxReady=ENABLE_HX711&&scale.is_ready();
  if(hxReady) {
    // Gesamtgewicht darf nach einer Tara negativ sein; nur die obere
    // Nennlast der Wägezelle wird begrenzt.
    float sample=min(scale.get_units(1),SCALE_MAX_WEIGHT_G);
    addWeightSample(sample);
    hxHasSample=true;
  }
  if(millis()-lastLcdWeightUpdate>=250) {
    lastLcdWeightUpdate=millis();
    drawLiveWeight();
  }
  if(millis()-lastSerialReport>1000) {
    lastSerialReport=millis();
    debugPrintf("[HX711] Gewicht: %.1f g | aktive ID: %s\n",liveWeight,activeId.length()?activeId.c_str():"--");
  }
  if(millis()-lastNfc>1000) {
    lastNfc=millis();
    String id=readTag();
    if(id.length()) {
      emptyNfcReads=0;
    }
    else if(nfcReady && activeId.length() && ++emptyNfcReads>=3) {
      activeId="";emptyNfcReads=0;
      debugPrintln("[NFC] Drei Leseversuche ohne Daten – aktive Rolle zurückgesetzt");
      draw();
    }
    if(id.length()&&id!=activeId) {
      bool known=findRoll(id);
      activeId=id;
      entryStep=0;
      if(!known)addUnknownRoll(id);
      debugPrintf("[NFC] Tag erkannt: %s (%s)\n",id.c_str(),known?"bekannte Rolle":"unbekannte Rolle angelegt");
      draw();
    }
  }
  delay(30);
}
