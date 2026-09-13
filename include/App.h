#pragma once

// Gemeinsame Modelle, Zustand und Modulschnittstellen.
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
#include "SensorTiming.h"
#include "lang/lang_de.h"
#include "lang/lang_en.h"
#include <stdarg.h>
#include <atomic>
#include <freertos/semphr.h>

struct Roll  {
  String id, material, maker, color = "#3b82f6";
  float weight = 0, previous = 0;
  float spoolWeight = 0;
  String spoolType = "Standard";
};
struct SpoolType { String name; float weight; };
constexpr char DB_FILE[] = "/filament.json";
struct TouchCalibration  {
  float ax=0, bx=0, cx=0, ay=0, by=0, cy=0;
  bool valid=false;
};
extern TouchCalibration touchCal;
constexpr int TOUCH_CAL_X[4]= {
  30,290,290,30
};
constexpr int TOUCH_CAL_Y[4]= {
  30,30,450,450
};
constexpr uint16_t C_BG=0x1082, C_PANEL=0x18E3, C_HEADER=0x0861, C_TEXT=0xFFFF, C_MUTED=0xBDF7;
constexpr uint16_t C_BLUE=0x3D7F, C_GREEN=0x05E0, C_ORANGE=0xFD20, C_RED=0xF986;
constexpr uint16_t C_PRESSED=0x4208;

extern std::vector<Roll> rolls;
extern std::vector<String> customMakers, customMaterials;
extern std::vector<SpoolType> spoolTypes;
extern HX711 scale;
extern SPIClass sdSpi;
extern Adafruit_PN532 nfc;
extern TFT_eSPI screen;
extern AsyncWebServer server;
extern DNSServer dns;
extern Preferences preferences;
extern String activeId;
extern float liveWeight;
extern float hxCalibration;
extern uint8_t weightDecimals;
extern SensorTiming sensorTiming;
extern SemaphoreHandle_t sensorMutex;
extern std::atomic<bool> tareRequested;
extern std::atomic<float> requestedHxCalibration;
extern String lastScannedId;
extern uint32_t lastScanFinishedAt;
extern bool scanCompleted;
extern bool hxDiscardPending;
extern std::atomic<bool> rgbEnabled;
extern bool rgbReadActive;
extern uint32_t rgbReadStartedAt;
extern unsigned long lastLcdWeightUpdate;
extern uint8_t emptyNfcReads;
extern bool spoolMenu;
extern uint8_t entryStep;
extern String selectedMaterial;
extern unsigned long lastSerialReport;
extern bool sdReady, wifiSetupMode;
extern bool nfcReady;
extern volatile bool internetAvailable;
extern TaskHandle_t networkTaskHandle;
extern bool hxReady;
extern bool hxHasSample;
extern bool ntpConfigured;
extern unsigned long lastNtpAttempt;
extern unsigned long restartAt;
extern uint16_t touchCalRaw[4][2];
extern uint8_t touchCalStep;

class SensorLock {
 public:
  explicit SensorLock(TickType_t waitTicks = 0)
      : locked_(sensorMutex && xSemaphoreTake(sensorMutex, waitTicks) == pdTRUE) {}
  ~SensorLock() { if(locked_)xSemaphoreGive(sensorMutex); }
  explicit operator bool() const { return locked_; }
  SensorLock(const SensorLock &) = delete;
  SensorLock &operator=(const SensorLock &) = delete;
 private:
  bool locked_;
};


void debugPrintf(const char *format, ...);
void debugPrintln(const char *text);
String esc(const String &s);
void loadCustomValues(const char *key,std::vector<String> &values);
void rememberCustomValue(const char *key,std::vector<String> &values,const String &value);
String jsonValues(const std::vector<String> &values);
void saveSpoolTypes();
void loadSpoolTypes();
String spoolsJson();
String createId();
Roll* findRoll(const String &id);
Roll* addUnknownRoll(const String &id);
void saveDb();
void loadDb();
String rollsJson();
void rgbOff();
void rgbReadOk();
void rgbWriteStart();
void initStatusLed();
void loadLedSettings();
String ledSettingsJson();
void handleLedSettings(AsyncWebServerRequest *request, JsonVariant &body);
bool initNfc();
bool nfcFieldIsOff();
String scanNfc();
float batteryVoltage();
int batteryPercent();
void drawBattery(int x,int y);
uint16_t color565(const String &hex);
void lcdCommand(uint8_t command, const uint8_t *data=nullptr, size_t length=0);
void initSt7796u();
void label(const String &text, int x, int y, uint16_t color=C_MUTED);
void button(int x,int y,int w,int h,const String &text,uint16_t color);
void buttonPressed(int x,int y,int w,int h,const String &text);
String formatWeightLcd(float value);
void draw();
void drawLiveWeight();
uint16_t xptRead(uint8_t command);
void mapTouch(uint16_t rawX,uint16_t rawY,int &x,int &y);
bool solveTouchAxis(float t0,float t1,float t2,float &a,float &b,float &c);
void finishTouchCalibration();
void handleTouch();
void startWeightMeasurements(uint32_t now);
void serviceSensors();
bool writeTag(const Roll &roll);
String readTagHardware();
String clockText();
void startWifiSetup();
bool timeIsValid();
void configureNtp(bool force=false);
bool syncNtpAtStart(bool waitForAnswer=true);
void checkInternet();
void networkMonitorTask(void *);
String scannedNetworks();
String statusJson();
void web();
void setup();
void loop();
