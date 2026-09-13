#pragma once

// Deutsche Texte der Benutzeroberfläche. Die englischen Entsprechungen liegen
// mit denselben Namen in lang_en.h, damit beide Oberflächen vollständig bleiben.
namespace LangDE {
constexpr char CODE[] = "de";
constexpr char APP[] = "Filament Scale";
constexpr char BOOT[] = "Filament-Rollenwaage";

constexpr char WEIGHT[] = "Gewicht";
constexpr char NET_WEIGHT[] = "Nettogewicht";
constexpr char CONSUMPTION[] = "Verbrauch";
constexpr char TARE[] = "Tara";
constexpr char WEIGH[] = "Wiegen";
constexpr char SAVE[] = "Speichern";
constexpr char CANCEL[] = "Abbrechen";
constexpr char DELETE[] = "Löschen";
constexpr char EDIT[] = "Bearbeiten";

constexpr char NFC[] = "NFC";
constexpr char NFC_READ[] = "NFC lesen";
constexpr char NFC_WRITE[] = "NFC schreiben";
constexpr char ACTIVE_ROLL[] = "Aktive Rolle";
constexpr char SAVED_ROLLS[] = "Gespeicherte Rollen";
constexpr char UNKNOWN[] = "Unbekannt";
constexpr char ID[] = "Eindeutige ID";
constexpr char MATERIAL[] = "Material";
constexpr char MANUFACTURER[] = "Hersteller";
constexpr char COLOR[] = "Farbe";

constexpr char SPOOL[] = "Spule";
constexpr char SPOOL_TYPE[] = "Spulenart";
constexpr char SPOOL_WEIGHT[] = "Spulengewicht";
constexpr char SPOOL_TYPES[] = "Spulenarten";

constexpr char SETTINGS[] = "Einstellungen";
constexpr char LED_UI_JSON[] = R"JSON({
  "label": "RGB-LED aktivieren",
  "description": "Farben für Ruhe, NFC-Lesen und Schreiben frei wählen. Alle LED-Einstellungen werden dauerhaft gespeichert. Die LCD-Beleuchtung bleibt unverändert.",
  "idle": "Ruhezustand",
  "read": "NFC lesen",
  "write": "NFC schreiben",
  "saveColors": "LED-Farben speichern",
  "loading": "LED-Einstellung wird geladen …",
  "saving": "LED-Einstellung wird gespeichert …",
  "on": "RGB-LED aktiviert. Einstellung bleibt nach einem Neustart erhalten.",
  "off": "RGB-LED deaktiviert. Alle drei Farben bleiben ausgeschaltet.",
  "loadError": "LED-Einstellung konnte nicht geladen werden. Bitte erneut versuchen.",
  "saveError": "Speichern nicht bestätigt. Bitte den aktuellen Zustand erneut laden.",
  "retry": "Zustand erneut laden"
})JSON";
constexpr char NETWORK[] = "Netzwerk";
constexpr char WIFI[] = "WLAN";
constexpr char TIME[] = "Uhrzeit";
constexpr char BATTERY[] = "Batterie";
constexpr char CONNECTED[] = "Verbunden";
constexpr char NOT_AVAILABLE[] = "Nicht verfügbar";
}
