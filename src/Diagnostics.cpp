#include "App.h"

// Sämtliche Diagnoseausgaben laufen über diese zwei Funktionen. Mit ENABLE_DEBUG
// in Config.h lassen sie sich abschalten, ohne die serielle Schnittstelle zu ändern.
void debugPrintf(const char *format, ...)  {
  if(!ENABLE_DEBUG)return;
  char buffer[256];
  va_list args;
  va_start(args,format);
  vsnprintf(buffer,sizeof(buffer),format,args);
  va_end(args);
  Serial.print(buffer);
}
void debugPrintln(const char *text)  {
  if(ENABLE_DEBUG)Serial.println(text);
}
