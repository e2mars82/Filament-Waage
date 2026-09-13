#pragma once

// English UI texts. Keep this list in sync with lang_de.h.
namespace LangEN {
constexpr char CODE[] = "en";
constexpr char APP[] = "Filament Scale";
constexpr char BOOT[] = "Filament spool scale";

constexpr char WEIGHT[] = "Weight";
constexpr char NET_WEIGHT[] = "Net weight";
constexpr char CONSUMPTION[] = "Consumption";
constexpr char TARE[] = "Tare";
constexpr char WEIGH[] = "Weigh";
constexpr char SAVE[] = "Save";
constexpr char CANCEL[] = "Cancel";
constexpr char DELETE[] = "Delete";
constexpr char EDIT[] = "Edit";

constexpr char NFC[] = "NFC";
constexpr char NFC_READ[] = "Read NFC";
constexpr char NFC_WRITE[] = "Write NFC";
constexpr char ACTIVE_ROLL[] = "Active roll";
constexpr char SAVED_ROLLS[] = "Saved rolls";
constexpr char UNKNOWN[] = "Unknown";
constexpr char ID[] = "Unique ID";
constexpr char MATERIAL[] = "Material";
constexpr char MANUFACTURER[] = "Manufacturer";
constexpr char COLOR[] = "Color";

constexpr char SPOOL[] = "Spool";
constexpr char SPOOL_TYPE[] = "Spool type";
constexpr char SPOOL_WEIGHT[] = "Spool weight";
constexpr char SPOOL_TYPES[] = "Spool types";

constexpr char SETTINGS[] = "Settings";
constexpr char LED_UI_JSON[] = R"JSON({
  "label": "Enable RGB LED",
  "description": "Choose colours for idle, NFC reading and writing. All LED settings are saved persistently. The LCD backlight is unaffected.",
  "idle": "Idle",
  "read": "Read NFC",
  "write": "Write NFC",
  "saveColors": "Save LED colours",
  "loading": "Loading LED setting …",
  "saving": "Saving LED setting …",
  "on": "RGB LED enabled. The setting is retained after a restart.",
  "off": "RGB LED disabled. All three colours remain off.",
  "loadError": "Could not load the LED setting. Please try again.",
  "saveError": "Save not confirmed. Please reload the current state.",
  "retry": "Reload state"
})JSON";
constexpr char NETWORK[] = "Network";
constexpr char WIFI[] = "Wi-Fi";
constexpr char TIME[] = "Time";
constexpr char BATTERY[] = "Battery";
constexpr char CONNECTED[] = "Connected";
constexpr char NOT_AVAILABLE[] = "Not available";
}
