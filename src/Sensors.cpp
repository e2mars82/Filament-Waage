#include "App.h"

// Aufrufer hält sensorMutex. Auch bei einer verzögerten loop() darf der
// erste nach NFC festgehaltene ADC-Wert nicht in den Mittelwert gelangen.
void startWeightMeasurements(uint32_t now) {
  sensorTiming.startMeasurements(now);
  hxDiscardPending=true;
}
// Zeitgesteuerter Sensorablauf statt delay(500)/delay(100). Während der
// Beruhigung und zwischen Messungen bleiben Touch und Anzeige bedienbar.
void serviceSensors() {
  SensorLock lock;
  if(!lock)return;

  uint32_t now=millis();
  if(rgbReadActive && uint32_t(now-rgbReadStartedAt)>=180)rgbOff();
  const float requestedFactor=requestedHxCalibration.load();
  if(ENABLE_HX711 && scale.get_scale()!=requestedFactor) {
    scale.set_scale(requestedFactor);
    startWeightMeasurements(now);
    debugPrintln("[HX711] Kalibrierung übernommen, neue Messreihe gestartet");
  }

  if(sensorTiming.pollDue(now)) {
    sensorTiming.pollStarted(now);
    debugPrintf("[NFC] Lesezyklus gestartet bei %lu ms\n",(unsigned long)now);
    String id=scanNfc();
    const uint32_t finishedAt=millis();
    lastScannedId=id;
    lastScanFinishedAt=finishedAt;
    scanCompleted=true;
    // Bei aktivem NFC startet endFieldSession() die Pause erst nach Feld AUS.
    if(ENABLE_HX711 && !ENABLE_PN532)startWeightMeasurements(finishedAt);
    debugPrintf("[NFC] Lesen beendet bei %lu ms; Gewicht in %lu ms\n",
                (unsigned long)finishedAt,(unsigned long)HX711_AFTER_NFC_DELAY_MS);

    bool redraw=false;
    if(id.length())emptyNfcReads=0;
    else if(nfcReady && activeId.length() && ++emptyNfcReads>=3) {
      activeId="";
      emptyNfcReads=0;
      debugPrintln("[NFC] Drei Leseversuche ohne Daten – aktive Rolle zurückgesetzt");
      redraw=true;
    }
    if(id.length() && id!=activeId) {
      const bool known=findRoll(id)!=nullptr;
      activeId=id;
      entryStep=0;
      if(!known)addUnknownRoll(id);
      debugPrintf("[NFC] Tag erkannt: %s (%s)\n",id.c_str(),
                  known?"bekannte Rolle":"unbekannte Rolle angelegt");
      redraw=true;
    }
    if(redraw)draw();
    // Erst im nächsten Schleifendurchlauf den ADC bedienen, nie während NFC.
    return;
  }

  if(!ENABLE_HX711 || !sensorTiming.measuring())return;
  if(!nfcFieldIsOff()) {
    sensorTiming.cancelMeasurements();
    return;
  }
  hxReady=scale.is_ready();
  if(hxDiscardPending && hxReady) {
    (void)scale.read();
    hxDiscardPending=false;
    return;
  }
  if(sensorTiming.settling(now)) {
    // Der HX711 hält einen fertigen Wert bis zum Auslesen fest. Deshalb
    // während der Pause alte/gestörte Wandlungen verwerfen, nicht mitteln.
    // is_ready() verhindert blockierendes Warten auf einen fehlenden Sensor.
    if(hxReady)(void)scale.read();
    return;
  }
  if(!sensorTiming.sampleDue(now))return;
  if(sensorTiming.timedOut(now)) {
    sensorTiming.cancelMeasurements();
    hxReady=false;
    debugPrintln("[HX711] Messreihe abgebrochen: Bereitschaft/Zeitfenster überschritten; letzter Wert bleibt");
    return;
  }
  if(!hxReady)return;

  const long raw=scale.read();
  if(tareRequested.exchange(false)) {
    // Tara aus genau einem frischen Messwert, erst im störungsarmen Fenster.
    scale.set_offset(raw);
    liveWeight=0;
    hxHasSample=true;
    startWeightMeasurements(millis());
    debugPrintln("[HX711] Tara aus einem Messwert gesetzt; Messreihe neu gestartet");
    return;
  }
  const float sample=min(float((double(raw)-scale.get_offset())/scale.get_scale()),
                         SCALE_MAX_WEIGHT_G);
  if(!isfinite(sample)) {
    sensorTiming.cancelMeasurements();
    debugPrintln("[HX711] Ungültiger Messwert verworfen");
    return;
  }
  sensorTiming.recordSample(millis(),sample);
  debugPrintf("[HX711] Messung %u/%u bei %lu ms: %.1f g\n",
              sensorTiming.count(),HX711_AVERAGE_SAMPLES,(unsigned long)millis(),sample);
  if(!sensorTiming.measuring()) {
    // Nur die drei Werte dieses Zyklus veröffentlichen, keine Mischung mit
    // alten Werten oder kurzzeitige Nullwerte zwischen den Messungen.
    liveWeight=sensorTiming.mean();
    hxHasSample=true;
    debugPrintf("[HX711] Neuer Mittelwert: %.1f g\n",liveWeight);
  }
}
