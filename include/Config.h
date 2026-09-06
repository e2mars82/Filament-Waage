#pragma once
#include "E32R35T_E32N35T.h"

// --- Funktionen einzeln aktivieren/deaktivieren ---
constexpr bool ENABLE_HX711 = true;
constexpr bool ENABLE_SD_CARD = true;
// Der PN532 wird ausschließlich über I²C betrieben. Es gibt keine NFC-SPI-Pins.
constexpr bool PN532_I2C_ONLY = true;
constexpr bool ENABLE_PN532 = true;
constexpr bool ENABLE_DEBUG = true;
// Bei jeder veröffentlichten Firmwareänderung um 0.1 erhöhen.
constexpr char FIRMWARE_VERSION[] = "4.8";
// Maximale Nennlast der verwendeten Wägezelle. Werte darüber werden nicht
// angezeigt, damit fehlerhafte Messwerte keine falschen Rollenwerte erzeugen.
constexpr float SCALE_MAX_WEIGHT_G = 5000.0F;
// Anzahl der Einzelmessungen für den gleitenden Durchschnitt.
constexpr uint8_t HX711_AVERAGE_SAMPLES = 3;
// Querformat für das gedrehte LCD: 90 Grad ergibt 480×320 Pixel.
constexpr int DISPLAY_ROTATION_DEGREES = 0;
static_assert(DISPLAY_ROTATION_DEGREES == 0 || DISPLAY_ROTATION_DEGREES == 90 || DISPLAY_ROTATION_DEGREES == 180 || DISPLAY_ROTATION_DEGREES == 270, "DISPLAY_ROTATION_DEGREES muss 0, 90, 180 oder 270 sein");
constexpr uint8_t DISPLAY_ROTATION = DISPLAY_ROTATION_DEGREES / 90;

// --- Netzwerk ---
// Die Zugangsdaten werden bei der ersten Einrichtung im WLAN-Manager abgefragt
// und dauerhaft im ESP32 gespeichert. Diese zwei Werte werden nicht benoetigt.
constexpr char AP_PASSWORD[] = "filament123"; // Fallback: FilamentScale-XXXX

// E32R35T gemäß E32R35T_E32N35T_Specification_V1.0.pdf, Seite 9-11.
// Display und Touch verwenden den internen HSPI-Bus; die SD-Karte den VSPI-Bus.
constexpr int PIN_SPI_SCK = BoardE32R35T::LCD_SCK;
constexpr int PIN_SPI_MISO = BoardE32R35T::LCD_MISO;
constexpr int PIN_SPI_MOSI = BoardE32R35T::LCD_MOSI;
constexpr int PIN_TFT_CS = BoardE32R35T::LCD_CS, PIN_TFT_DC = BoardE32R35T::LCD_DC, PIN_TFT_RST = BoardE32R35T::LCD_RESET;
constexpr int PIN_TFT_BL = BoardE32R35T::LCD_BACKLIGHT;
constexpr int PIN_RGB_RED = 22, PIN_RGB_GREEN = 16, PIN_RGB_BLUE = 17; // gemeinsame Anode: LOW = an
constexpr int PIN_TOUCH_CS = BoardE32R35T::TOUCH_CS_PIN, PIN_TOUCH_IRQ = BoardE32R35T::TOUCH_IRQ_PIN;
constexpr int PIN_SD_CS = BoardE32R35T::SD_CS;
constexpr int PIN_SD_SCK = BoardE32R35T::SD_SCK, PIN_SD_MISO = BoardE32R35T::SD_MISO, PIN_SD_MOSI = BoardE32R35T::SD_MOSI;

// PN532 im I²C-Modus: SDA/SCL an den externen I²C-Anschluss.
constexpr int PIN_I2C_SDA = BoardE32R35T::I2C_SDA, PIN_I2C_SCL = BoardE32R35T::I2C_SCL;
// HX711: DOUT muss ein Eingangs-Pin sein; GPIO39 ist dafür vorgesehen.
constexpr int PIN_HX711_DOUT = BoardE32R35T::HX711_DOUT, PIN_HX711_SCK = BoardE32R35T::HX711_SCK;
// IO34 misst die Batteriespannung über den Board-Spannungsteiler.
// Falls die Anzeige abweicht, diesen Faktor mit einem Multimeter kalibrieren.
constexpr int PIN_BATTERY_ADC = 34;
constexpr float BATTERY_DIVIDER_FACTOR = 2.0F;

constexpr float HX711_CALIBRATION = -7050.0F; // mit bekanntem Gewicht kalibrieren
constexpr float EMPTY_ROLL_THRESHOLD_G = 5.0F;
// Werte ermitteln mit Serial-Ausgabe beim Antippen; bei Bedarf tauschen/invertieren.
constexpr int TOUCH_MIN_X = 250, TOUCH_MAX_X = 3850;
constexpr int TOUCH_MIN_Y = 250, TOUCH_MAX_Y = 3850;
