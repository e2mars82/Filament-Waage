#include "App.h"
#include "NfcProtocol.h"

namespace {
bool fieldOffConfirmed = false;

// RFConfiguration wie im Beispiel: 0x32 / CfgItem / Nutzdaten.
// Zusätzlich die vollständige Antwort konsumieren und prüfen; nur ein ACK
// bestätigt noch nicht, dass der gewünschte Befehl ausgeführt wurde.
bool rfConfiguration(uint8_t *command, uint8_t length) {
  if(!nfc.sendCommandCheckAck(command,length,PN532_SCAN_TIMEOUT_MS))return false;
  uint8_t frame[10] = {};
  if(Wire.requestFrom(int(PN532_I2C_ADDRESS),int(sizeof(frame)))!=sizeof(frame)) {
    while(Wire.available())Wire.read();
    return false;
  }
  for(auto &byte:frame)byte=Wire.read();
  // I²C-RDY + 00 00 FF + LEN/LCS + D5 33 + DCS + Postamble.
  return validRfConfigurationResponse(frame,sizeof(frame));
}

bool setRFField(bool enabled) {
  uint8_t command[] = {0x32,0x01,uint8_t(enabled ? 0x01 : 0x00)};
  fieldOffConfirmed=false;
  const bool ok=rfConfiguration(command,sizeof(command));
  fieldOffConfirmed=ok && !enabled;
  debugPrintf("[NFC] Feld %s: %s bei %lu ms\n",enabled?"EIN":"AUS",
              ok?"bestätigt":"FEHLER",(unsigned long)millis());
  return ok;
}

bool beginFieldSession() {
  sensorTiming.cancelMeasurements();
  if(!nfcReady && !initNfc())return false;
  if(!setRFField(true))return false;
  delay(PN532_FIELD_SETTLE_MS); // Nur die kurze 10-ms-Feldstabilisierung.
  return true;
}

bool endFieldSession() {
  bool off=setRFField(false); // Immer versuchen, auch nach Scan-/Schreibfehlern.
  if(!off && ENABLE_PN532_RESET) {
    debugPrintln("[NFC] Feld AUS nicht bestätigt: Hardware-Reset als Rückfallebene");
    off=initNfc();
  }
  if(off && ENABLE_HX711)startWeightMeasurements(millis());
  else if(!off) {
    sensorTiming.cancelMeasurements();
    debugPrintln("[HX711] Keine Messung: NFC-Feld AUS nicht bestätigt");
  }
  return off;
}
} // namespace

bool nfcFieldIsOff() { return !ENABLE_PN532 || fieldOffConfirmed; }

// Nur beim Start oder mit gehaltenem sensorMutex aufrufen.
bool initNfc() {
  if(!ENABLE_PN532)return true;
  fieldOffConfirmed=false;
  if(ENABLE_PN532_RESET && PN532_RESET_PIN==26) {
    // GPIO26 ist sonst Audio-DAC: Verstärker deaktivieren (Enable aktiv LOW).
    pinMode(4,OUTPUT);
    digitalWrite(4,HIGH);
  }
  nfc.begin(); // Bibliothek pulst den konfigurierten RSTPD_N-Eingang.
  nfcReady=nfc.getFirmwareVersion()!=0;
  if(nfcReady)nfcReady=nfc.SAMConfig();
  // Keine endlose Suche nach Ablauf des Host-Timeouts im Hintergrund.
  uint8_t retries[] = {0x32,0x05,0xFF,0x01,0x00};
  if(nfcReady)nfcReady=rfConfiguration(retries,sizeof(retries));
  const bool off=setRFField(false);
  nfcReady=nfcReady && off;
  if(!nfcReady && ENABLE_PN532_RESET) {
    // Defekten Leser in Reset halten. Ohne bestätigte Antwort trotzdem keine
    // Gewichtswerte freigeben: die Resetleitung könnte unverdrahtet sein.
    digitalWrite(PN532_RESET_PIN,LOW);
  }
  debugPrintf("[PN532] %s | Reset GPIO%d: %s\n",nfcReady?"bereit, Feld AUS":"Initialisierung fehlgeschlagen",
              PN532_RESET_PIN,ENABLE_PN532_RESET?"aktiv":"deaktiviert");
  return nfcReady;
}

String scanNfc() {
  if(!ENABLE_PN532)return "";
  String id;
  if(beginFieldSession())id=readTagHardware();
  if(!endFieldSession())return "";
  return id;
}

// Speichert die Rolleninformationen kompakt in den NTAG-Nutzerseiten 4–39.
bool writeTag(const Roll &roll)  {
  if(!ENABLE_PN532)return false;
  SensorLock lock(pdMS_TO_TICKS(1000));
  if(!lock) {
    debugPrintln("[NFC] Schreiben nicht möglich: Sensorzugriff belegt");
    return false;
  }
  String material=roll.material.substring(0,10),maker=roll.maker.substring(0,16);
  material.replace("|","_");maker.replace("|","_");
  String text="FS2|"+roll.id+"|"+String(roll.weight,1)+"|"+roll.color+"|"+material+"|"+maker;
  if(text.length()>144)return false;
  // Nach Feld AUS ist die alte Tag-Auswahl ungültig: neu auswählen und ID
  // vergleichen, damit niemals versehentlich eine andere Rolle beschrieben wird.
  bool selected=beginFieldSession();
  if(selected)selected=readTagHardware()==roll.id;
  if(!selected) {
    endFieldSession();
    rgbOff();
    return false;
  }
  rgbWriteStart();
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
  const bool off=endFieldSession();
  rgbOff();
  return ok && off;
}
// Ausschließlich durch serviceSensors() unter sensorMutex aufrufen.
String readTagHardware()  {
  if(!ENABLE_PN532 || !nfcReady)return "";
  uint8_t uid[7], len;
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A,uid,&len,PN532_SCAN_TIMEOUT_MS)) return "";
  String raw;
  uint8_t data[4];
  for(uint8_t p=4;
  p<40;
  p++)  {
    if(!nfc.ntag2xx_ReadPage(p,data)) break;
    for(auto b:data) if(b)raw+=(char)b;
    // Für die Rollenauswahl genügt die Kennung. Nicht jedes Mal alle 36
    // Speicherseiten lesen: Das verkürzt die NFC-Last und die blockierende Zeit.
    if(raw.startsWith("FS2|") && raw.indexOf('|',4)>4)break;
    if(p==4 && !raw.startsWith("FS1:") && !raw.startsWith("FS2|"))break;
    if(raw.startsWith("FS1:") && memchr(data,0,sizeof(data)))break;
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
