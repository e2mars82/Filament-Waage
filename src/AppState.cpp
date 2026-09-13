#include "App.h"

std::vector<Roll> rolls;
std::vector<String> customMakers, customMaterials;
std::vector<SpoolType> spoolTypes;
HX711 scale;
// Display/Touch verwenden VSPI. Die SD-Karte muss deshalb HSPI verwenden;
// sonst wird der Display-SPI-Bus beim Initialisieren der SD-Karte umgeschaltet.
SPIClass sdSpi(HSPI);
// Die installierte Adafruit-Bibliothek erwartet für I²C noch IRQ und RESET
// als Konstruktorparameter. IRQ bleibt unbenutzt (0xFF); der optionale Reset
// steuert ausschließlich RSTPD_N. Die Kommunikation erfolgt per I²C-Polling.
constexpr uint8_t PN532_NO_PIN = 0xFF;
Adafruit_PN532 nfc(PN532_NO_PIN, ENABLE_PN532 && ENABLE_PN532_RESET ? PN532_RESET_PIN : PN532_NO_PIN, &Wire);
// TFT_eSPI erhält seine E32R35T-Pins über die Build-Flags in platformio.ini.
TFT_eSPI screen = TFT_eSPI();
AsyncWebServer server(80);
DNSServer dns;
Preferences preferences;
String activeId;
float liveWeight = 0;
float hxCalibration = HX711_CALIBRATION;
uint8_t weightDecimals = 1;
SensorTiming sensorTiming(NFC_READ_INTERVAL_MS, HX711_AFTER_NFC_DELAY_MS,
                         HX711_SAMPLE_INTERVAL_MS, HX711_READY_TIMEOUT_MS,
                         HX711_AVERAGE_SAMPLES);
SemaphoreHandle_t sensorMutex = nullptr;
std::atomic<bool> tareRequested{false};
std::atomic<float> requestedHxCalibration{HX711_CALIBRATION};
String lastScannedId;
uint32_t lastScanFinishedAt = 0;
bool scanCompleted = false;
bool hxDiscardPending = false;
// Bis zum Laden der gespeicherten Einstellung bleiben alle LED-Farben aus.
std::atomic<bool> rgbEnabled{false};
bool rgbReadActive = false;
uint32_t rgbReadStartedAt = 0;
unsigned long lastLcdWeightUpdate = 0;
uint8_t emptyNfcReads = 0;
bool spoolMenu = false;
uint8_t entryStep = 0;
String selectedMaterial;
unsigned long lastSerialReport = 0;
bool sdReady = false, wifiSetupMode = false;
bool nfcReady = false;
volatile bool internetAvailable = false;
TaskHandle_t networkTaskHandle = nullptr;
bool hxReady = false;
bool hxHasSample = false;

bool ntpConfigured = false;
unsigned long lastNtpAttempt = 0;
unsigned long restartAt = 0;
TouchCalibration touchCal;
uint16_t touchCalRaw[4][2];
uint8_t touchCalStep=255;
