#include "App.h"

String clockText()  {
  time_t now=time(nullptr);
  if(now<1700000000)return "--:--";
  tm info;
  localtime_r(&now,&info);
  char out[18];
  strftime(out,sizeof(out),"%d.%m. %H:%M",&info);
  return String(out);
}
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
void configureNtp(bool force)  {
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
bool syncNtpAtStart(bool waitForAnswer)  {
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
