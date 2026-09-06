# Filament-Rollenwaage (ESP32)

Firmware fuer einen ESP32-32E: ST7796U-Display, HX711-Waage, SD-Karte und PN532-NFC. Sie speichert Rollen lokal in `/filament.json`, zeigt sie am Touchscreen an und stellt ein WebUI bereit.

## Pinbelegung: LCDWIKI E32R35T

Die folgende Belegung basiert auf Seite 9 bis 11 des Datenblatts `E32R35T_E32N35T_Specification_V1.0.pdf`.

| Funktion | GPIO | Hinweis |
| --- | ---: | --- |
| LCD CS / DC / Reset | 15 / 2 / EN | Reset ist mit ESP-Reset verbunden |
| LCD Hintergrundbeleuchtung | 27 | High = eingeschaltet |
| RGB-LED Rot / Grün / Blau | 22 / 16 / 17 | gemeinsame Anode, Low = eingeschaltet |
| LCD SPI SCK / MOSI / MISO | 14 / 13 / 12 | interne Display-SPI-Leitungen |
| Touch CS / IRQ | 33 / 36 | resistiver Touch des E32R35T |
| MicroSD CS / SCK / MOSI / MISO | 5 / 18 / 23 / 19 | eigener SPI-Bus |
| PN532 I²C SDA / SCL / IRQ / Reset | 32 / 25 / 35 / 4 | PN532-Jumper auf **I²C** setzen |
| HX711 DOUT / SCK | 39 / 21 | GPIO39 ist nur Eingang und ideal für DOUT |

Die Verkabelung des PN532 muss I²C verwenden: `SDA → GPIO32`, `SCL → GPIO25`, `IRQ → GPIO35`, `RSTO → GPIO4`, `GND → GND`, `VCC → 3,3 V`. Der PN532-SPI-Modus wird von der Firmware nicht verwendet. Der SPI-Bus bleibt ausschließlich für Display und SD-Karte aktiv. GPIO4 ist auf dem Board sonst Audio-Enable; Audio bleibt mit dieser Konfiguration deaktiviert.

Die Displayausrichtung wird in `include/Config.h` über `DISPLAY_ROTATION_DEGREES` gesetzt. Zulässige Werte sind `0`, `90`, `180` und `270`; `0` ist die Standardansicht.

Die SD-Karte läuft bewusst über den zweiten SPI-Controller (HSPI), während LCD und Touch VSPI nutzen. Diese Trennung ist erforderlich, damit die SD-Initialisierung das LCD nicht auf ein weißes Bild zurücksetzt.

Die Batterieanzeige verwendet GPIO34 und den in `Config.h` einstellbaren `BATTERY_DIVIDER_FACTOR`. Mit einem Multimeter kann dieser Faktor bei Bedarf nachkalibriert werden.

`ENABLE_PN532` schaltet NFC vollständig ein oder aus. Die aktuelle Firmwareversion steht zentral als `FIRMWARE_VERSION` in `include/Config.h` und beginnt bei `0.8`; bei der nächsten Veröffentlichung wird sie um `0.1` erhöht.

Ab Firmware `1.0` verwendet das Projekt TFT_eSPI mit den vom Hersteller-Demo vorgegebenen Build-Flags für den ST7796U. Dadurch entspricht die Display-Initialisierung dem funktionierenden LCDDemo-Beispiel.

`ENABLE_DEBUG` in `Config.h` aktiviert oder deaktiviert sämtliche Diagnoseausgaben. Rollen speichern zusätzlich eine frei wählbare Farbe in der SD-Datenbank.

## Vor dem ersten Upload

1. `include/Config.h` an die tatsaechliche Verkabelung anpassen (die Pins sind ein sinnvolles Beispiel). Dort lassen sich `ENABLE_HX711` und `ENABLE_SD_CARD` getrennt aktivieren oder deaktivieren. Ohne SD-Karte bleiben Rollen nur bis zum Neustart im Speicher.
2. WLAN-Zugangsdaten muessen nicht in der Firmware stehen: Beim ersten Start startet der ESP den Hotspot `FilamentScale-XXXX` mit dem Passwort aus `AP_PASSWORD`. Nach Verbindung damit `http://192.168.4.1` oeffnen, das gefundene WLAN auswaehlen und nur dessen Passwort eingeben. Die Daten werden dauerhaft im ESP32 gespeichert.
3. Der Touch-Teil setzt einen **XPT2046** voraus, wie er auf vielen 3,5-Zoll-ST7796U-Modulen sitzt. Bei einem anderen Touch-Controller muss nur der Touch-Abschnitt in `src/main.cpp` ersetzt werden.
4. HX711 mit einem bekannten Gewicht kalibrieren und `HX711_CALIBRATION` eintragen.

Die Touch-Bibliothek wird absichtlich direkt aus dem offiziellen GitHub-Projekt bezogen. Das vermeidet einen gelegentlich auftretenden `UnknownPackageError` der PlatformIO-Registrierung.

## Bedienung

- Rolle auflegen: Das Gewicht wird laufend gelesen. Ein NFC-Tag wird erkannt; bekannte Rollen zeigen Material, Hersteller, Restgewicht und Verbrauch seit der letzten Wiegung.
- Unbekannter Tag: Im WebUI eine Rolle anlegen; dann die Rolle auflegen und **NFC schreiben** am Display tippen. Die eindeutige Kennung wird auf den Tag geschrieben und in der SD-Datenbank gespeichert.
- Im WebUI lassen sich Rollen anlegen, bearbeiten und loeschen. Die Aktion **Wiegen** setzt Gewicht und Verbrauch neu.

> NFC-Schreiben ist fuer NTAG21x/Ultralight-kompatible Tags umgesetzt (vier Datenbytes pro Page). MIFARE Classic, DESFire oder gesperrte Tags brauchen eine andere Schreibroutine bzw. Schluesselverwaltung.

## Diagnose ueber die serielle Schnittstelle

Im PlatformIO-Monitor **115200 Baud** waehlen. Beim Einschalten erscheinen Status von SD, HX711, PN532 und WLAN. Danach wird einmal pro Sekunde Gewicht und aktive Rollen-ID ausgegeben. Jeder Touch sowie jeder erkannte oder beschriebene NFC-Tag wird ebenfalls protokolliert. Die Rohwerte des Touchscreens helfen beim Eintragen von `TOUCH_MIN_*` und `TOUCH_MAX_*` in `Config.h`.

## WebUI: Einstellungen und OTA

Unter **Einstellungen** lassen sich ein anderes WLAN, DHCP oder eine statische IP-Adresse konfigurieren. Die Aenderung startet den ESP kontrolliert neu. Dieselbe Seite enthaelt ein OTA-Update: In PlatformIO zuerst `Build` ausfuehren und dann die erzeugte `.pio/build/esp32dev/firmware.bin` hochladen. Das OTA-Update ist nur im lokalen WLAN bzw. ESP-Access-Point erreichbar; das WebUI sollte daher nicht ins Internet weitergeleitet werden.

Die Rollenmaske verwendet Dropdowns fuer gaengige 3D-Druck-Materialien (einschliesslich PLA, PETG, ABS, ASA, TPU, PA, PC, PEEK und Verbundmaterialien) sowie Hersteller. Bereits gespeicherte Hersteller werden automatisch in die Auswahl aufgenommen.

Neue Rollen erhalten automatisch eine Kennung im Format `NFC-<vollständiger-MD5-Hash>`. Sie entsteht aus der NTP-Zeit, der ESP32-Kennung und einem Zufallswert mit MD5. Der NTP-Server ist unter **Einstellungen** konfigurierbar und standardmaessig `de.pool.ntp.org`.

Die Pinbelegung befindet sich in `boards/E32R35T_E32N35T.h`. Sprachdaten liegen getrennt in `include/lang/lang_de.h` und `include/lang/lang_en.h`. Das WebUI liefert `UTF-8` aus, damit Umlaute wie ä, ö, ü und ß korrekt angezeigt werden.

Im Bearbeitungsmodus stehen **Wiegen**, **NFC schreiben** und **NFC lesen** zur Verfuegung. NFC lesen sucht die gespeicherte Rollen-ID und waehlt sie automatisch zur Bearbeitung aus. Nach einem OTA-Update wird nach 20 Sekunden auf die Hauptseite weitergeleitet.
