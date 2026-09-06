#pragma once

// LCDWIKI E32R35T / E32N35T - Datenblatt Seite 9 bis 11
namespace BoardE32R35T {
constexpr int LCD_CS = 15;
constexpr int LCD_DC = 2;
constexpr int LCD_RESET = -1;       // LCD-Reset ist mit EN verbunden
constexpr int LCD_BACKLIGHT = 27;
constexpr int LCD_SCK = 14;
constexpr int LCD_MISO = 12;
constexpr int LCD_MOSI = 13;
// Suffix _PIN verhindert einen Namenskonflikt mit dem TFT_eSPI-Makro TOUCH_CS.
constexpr int TOUCH_CS_PIN = 33;
constexpr int TOUCH_IRQ_PIN = 36;
constexpr int SD_CS = 5;
constexpr int SD_SCK = 18;
constexpr int SD_MISO = 19;
constexpr int SD_MOSI = 23;
constexpr int I2C_SDA = 32;
constexpr int I2C_SCL = 25;
// HX711: DOUT ist ein Eingang (GPIO39), SCK ein Ausgang (GPIO21).
constexpr int HX711_DOUT = 39;
constexpr int HX711_SCK = 21;
}
