#include "App.h"

#include "web/WebPages.h"

// Aktueller Zustand für die AJAX-Statusanzeige im WebUI.
String statusJson()  {
  Roll*r=findRoll(activeId);
  float shownWeight=fabsf(liveWeight)<(weightDecimals?0.05F:0.5F)?0.0F:liveWeight;
  String weight=hxHasSample?String(shownWeight,static_cast<unsigned int>(weightDecimals)):String(0.0F,static_cast<unsigned int>(weightDecimals));
  weight.replace(".",",");
  String s="{\"weight\":\""+weight+"\",\"id\":\""+esc(activeId)+"\",\"material\":\""+esc(r?r->material:"")+"\",\"maker\":\""+esc(r?r->maker:"")+"\",\"time\":\""+clockText()+"\",\"internet\":"+(internetAvailable?"true":"false")+",\"battery\":"+String(batteryPercent())+",\"voltage\":"+String(batteryVoltage(),2)+",\"ip\":\""+(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString())+"\"}";
  return s;
}
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
    r->print("<script type=\"application/json\" id=\"rgbLedTexts\">");
    r->print(LangDE::LED_UI_JSON);
    r->print("</script>");
    r->print(SETTINGS_LED);
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
    // Keine zusätzliche NFC-Abfrage aus dem Web-Task: Sie würde die 500-ms-
    // Ruhephase unterbrechen. Die zyklische Erkennung aktualisiert die Auswahl.
    String id;
    {
      SensorLock lock(pdMS_TO_TICKS(1000));
      if(!lock) {
        q->send(503,"application/json","{\"id\":\"\",\"error\":\"Sensoren belegt, bitte erneut versuchen\"}");
        return;
      }
      if(scanCompleted && uint32_t(millis()-lastScanFinishedAt)<=NFC_READ_INTERVAL_MS)
        id=lastScannedId;
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
    requestedHxCalibration.store(hxCalibration);
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
  server.on("/api/settings/led",HTTP_GET,[](AsyncWebServerRequest*q) {
    SensorLock lock(pdMS_TO_TICKS(1000));
    if(!lock) { q->send(503,"application/json","{\"error\":\"sensor busy\"}");return; }
    q->send(200,"application/json",ledSettingsJson());
  });
  server.on("/api/settings/led",HTTP_POST,[](AsyncWebServerRequest*q,JsonVariant &body) {
    handleLedSettings(q,body);
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
