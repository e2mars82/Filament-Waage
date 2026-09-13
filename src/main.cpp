#include "App.h"

// Hardwarestart: Display/Touch, Status-LED, Waage, SD, NFC und Netzwerk.
void setup()  {
  // GPIO27 ist aktiv HIGH: als allererste Aktion dunkel halten, noch vor
  // Serial-Start, Wartezeiten und Initialisierung der übrigen Hardware.
  pinMode(PIN_TFT_BL,OUTPUT);
  digitalWrite(PIN_TFT_BL,LOW);
  Serial.begin(115200);
  delay(300);
  sensorMutex=xSemaphoreCreateMutex();
  if(!sensorMutex) {
    debugPrintln("[SENSOREN] FEHLER: Mutex konnte nicht angelegt werden");
  }
  debugPrintf("\n=== Filament-Rollenwaage v%s startet ===\n",FIRMWARE_VERSION);
  // Touch teilt sich den LCD-Bus und darf während der LCD-Initialisierung
  // nicht selektiert sein.
  pinMode(PIN_TOUCH_CS,OUTPUT);
  digitalWrite(PIN_TOUCH_CS,HIGH);
  pinMode(PIN_TOUCH_IRQ,INPUT);
  SPI.begin(PIN_SPI_SCK,PIN_SPI_MISO,PIN_SPI_MOSI);
  initStatusLed();
  preferences.begin("filament",false);
  loadLedSettings();
  hxCalibration=preferences.getFloat("hxcal",HX711_CALIBRATION);
  requestedHxCalibration.store(hxCalibration);
  weightDecimals=preferences.getUChar("wdec",1);
  if(weightDecimals>1)weightDecimals=1;
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_BATTERY_ADC,ADC_11db);
  screen.init();
  screen.setRotation(DISPLAY_ROTATION);
  // Nach Reset undefinierter Bildspeicher: unsichtbar vollständig löschen.
  screen.fillScreen(TFT_BLACK);
  debugPrintf("[SPI] LCD/Touch: SCK=%d MISO=%d MOSI=%d | Touch CS=%d IRQ=%d\n",PIN_SPI_SCK,PIN_SPI_MISO,PIN_SPI_MOSI,PIN_TOUCH_CS,PIN_TOUCH_IRQ);
  if(ENABLE_HX711) {
    scale.begin(PIN_HX711_DOUT,PIN_HX711_SCK);
    scale.set_scale(hxCalibration);
    // Auch beim ESP-Neustart könnte der separat versorgte PN532 noch ein
    // aktives Feld haben. Start-Tara daher erst im bestätigten Ruhefenster.
    tareRequested.store(true);
    debugPrintf("[HX711] Aktiv, Start-Tara vorgemerkt, Kalibrierwert: %.2f\n",hxCalibration);
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
    initNfc();
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
  // draw() überträgt das erste vollständige Bild synchron. Erst danach
  // beleuchten, damit weder weißer Startbildschirm noch Zufallsdaten sichtbar sind.
  digitalWrite(PIN_TFT_BL,HIGH);
  debugPrintln("[LCD] Erstes Bild fertig – Hintergrundbeleuchtung EIN");
}
void loop()  {
  if(!timeIsValid() && WiFi.status()==WL_CONNECTED && millis()-lastNtpAttempt>15000) syncNtpAtStart(false);
  if(restartAt&&millis()>=restartAt)ESP.restart();
  if(wifiSetupMode)dns.processNextRequest();
  // Touch hat Vorrang vor Wiegen, NFC und LCD-Refresh. Dadurch reagieren die
  // Buttons sofort, auch wenn gerade eine Messung oder NFC-Suche ansteht.
  handleTouch();
  serviceSensors();
  if(millis()-lastLcdWeightUpdate>=250) {
    lastLcdWeightUpdate=millis();
    drawLiveWeight();
  }
  if(millis()-lastSerialReport>1000) {
    lastSerialReport=millis();
    debugPrintf("[HX711] Gewicht: %.1f g | aktive ID: %s\n",liveWeight,activeId.length()?activeId.c_str():"--");
  }
  // Kurzer Scheduler-Yield; kein 30-ms-Raster mehr für 100-ms-Messabstände.
  delay(1);
}
