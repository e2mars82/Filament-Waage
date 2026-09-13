#include "App.h"

float batteryVoltage()  {
  return analogReadMilliVolts(PIN_BATTERY_ADC) * BATTERY_DIVIDER_FACTOR / 1000.0F;
}
int batteryPercent()  {
  return constrain((int)((batteryVoltage()-3.20F)*100.0F/1.00F),0,100);
}
void drawBattery(int x,int y)  {
  int p=batteryPercent(),w=22;
  screen.drawRect(x,y,w,10,C_TEXT);
  screen.fillRect(x+w,y+3,2,4,C_TEXT);
  screen.fillRect(x+1,y+1,(w-2)*p/100,8,p<20?C_RED:C_GREEN);
}
uint16_t color565(const String &hex) {
  if(hex.length()!=7 || hex[0]!='#')return C_BLUE;
  uint32_t rgb=strtoul(hex.c_str()+1,nullptr,16);
  return ((rgb>>19)&0x1F)<<11 | ((rgb>>10)&0x3F)<<5 | ((rgb>>3)&0x1F);
}
// Herstellersequenz aus LCDWIKI ST7796_Init.txt. Der ST7796U benötigt diese
// Power-, Gamma- und Command-Set-Konfiguration zusätzlich zur Standardinitialisierung.
void lcdCommand(uint8_t command, const uint8_t *data, size_t length)  {
  SPI.beginTransaction(SPISettings(40000000,MSBFIRST,SPI_MODE0));
  digitalWrite(PIN_TFT_CS,LOW);
  digitalWrite(PIN_TFT_DC,LOW);
  SPI.transfer(command);
  if(length) {
    digitalWrite(PIN_TFT_DC,HIGH);
    SPI.writeBytes(data,length);
  }
  digitalWrite(PIN_TFT_CS,HIGH);
  SPI.endTransaction();
}
void initSt7796u()  {
  const uint8_t d36[]= {
    0x48
  },d3a[]= {
    0x55
  },df0c3[]= {
    0xC3
  },df096[]= {
    0x96
  },db4[]= {
    0x01
  },db7[]= {
    0xC6
  };
  const uint8_t dc0[]= {
    0x80,0x45
  },dc1[]= {
    0x13
  },dc2[]= {
    0xA7
  },dc5[]= {
    0x20
  };
  const uint8_t de8[]= {
    0x40,0x8A,0x00,0x00,0x29,0x19,0xA5,0x33
  };
  const uint8_t de0[]= {
    0xD0,0x08,0x0F,0x06,0x06,0x33,0x30,0x33,0x47,0x17,0x13,0x13,0x2B,0x31
  };
  const uint8_t de1[]= {
    0xD0,0x0A,0x11,0x0B,0x09,0x07,0x2F,0x33,0x47,0x38,0x15,0x16,0x2C,0x32
  };
  const uint8_t df03c[]= {
    0x3C
  },df069[]= {
    0x69
  };
  lcdCommand(0x11);
  delay(120);
  lcdCommand(0x36,d36,1);
  lcdCommand(0x3A,d3a,1);
  lcdCommand(0xF0,df0c3,1);
  lcdCommand(0xF0,df096,1);
  lcdCommand(0xB4,db4,1);
  lcdCommand(0xB7,db7,1);
  lcdCommand(0xC0,dc0,2);
  lcdCommand(0xC1,dc1,1);
  lcdCommand(0xC2,dc2,1);
  lcdCommand(0xC5,dc5,1);
  lcdCommand(0xE8,de8,8);
  lcdCommand(0xE0,de0,14);
  lcdCommand(0xE1,de1,14);
  lcdCommand(0xF0,df03c,1);
  lcdCommand(0xF0,df069,1);
  delay(120);
  lcdCommand(0x29);
}
void label(const String &text, int x, int y, uint16_t color)  {
  screen.setTextSize(1);
  screen.setTextColor(color);
  screen.setCursor(x,y);
  screen.print(text);
}
void button(int x,int y,int w,int h,const String &text,uint16_t color)  {
  int size=text.length()>8?1:2;
  int charWidth=size==1?6:12;
  screen.fillRoundRect(x,y,w,h,8,color);
  screen.setTextColor(C_TEXT);
  screen.setTextSize(size);
  int tx=x+(w-text.length()*charWidth)/2;
  screen.setCursor(tx,y+(h-(size==1?8:16))/2);
  screen.print(text);
}
void buttonPressed(int x,int y,int w,int h,const String &text) {
  button(x,y,w,h,text,C_PRESSED);
  delay(60);
}
// Einheitliche Gewichtsformatierung für WebUI und LCD: 0 oder 1 Nachkommastelle
// wird in den Einstellungen gewählt; auf dem LCD wird das deutsche Komma genutzt.
String formatWeightLcd(float value) {
  // Verhindert -0 bzw. -0,0 durch minimale Messabweichungen um den Nullpunkt.
  if(fabsf(value)<(weightDecimals?0.05F:0.5F))value=0.0F;
  String text(value,static_cast<unsigned int>(weightDecimals));
  text.replace(".",",");
  return text;
}
void draw()  {
  if(spoolMenu) {
    screen.fillScreen(C_BG);screen.setTextColor(C_TEXT);screen.setTextSize(2);screen.setCursor(22,30);screen.print("Spulengewicht");screen.setTextSize(1);screen.setCursor(22,55);screen.print("Wert fuer aktive Rolle waehlen");
    for(size_t i=0;i<spoolTypes.size()&&i<6;i++) {
      int bx=20+(i%2)*150,by=90+(i/2)*85;
      screen.fillRoundRect(bx,by,130,62,8,C_BLUE);screen.setTextColor(C_TEXT);
      // Der Spulenname ist bewusst doppelt so groß wie das Gewicht.
      screen.setTextSize(2);screen.setCursor(bx+8,by+10);screen.print(spoolTypes[i].name.substring(0,10));
      screen.setTextSize(1);screen.setCursor(bx+8,by+43);screen.printf("%.0f g",spoolTypes[i].weight);
    }
    return;
  }
  if(touchCalStep<4)  {
    screen.fillScreen(C_BG);
    screen.setTextColor(C_TEXT);
    screen.setTextSize(2);
    screen.setCursor(18,175);
    screen.print("Touch kalibrieren");
    screen.setTextSize(1);
    screen.setCursor(18,205);
    screen.print("Bitte das Fadenkreuz beruehren");
    int x=TOUCH_CAL_X[touchCalStep],y=TOUCH_CAL_Y[touchCalStep];
    screen.drawCircle(x,y,18,C_ORANGE);
    screen.drawFastHLine(x-25,y,51,C_ORANGE);
    screen.drawFastVLine(x,y-25,51,C_ORANGE);
    screen.setCursor(18,230);
    screen.printf("Punkt %d von 4",touchCalStep+1);
    return;
  }
  screen.fillScreen(C_BG);
  screen.fillRect(0,0,320,36,C_HEADER);
  screen.setTextColor(C_TEXT);
  screen.setTextSize(2);
  screen.setCursor(10,10);
  screen.print(LangDE::APP);
  screen.setTextSize(1);
  screen.setCursor(210,14);
  screen.print(clockText());
  drawBattery(290,13);
  screen.fillRoundRect(10,48,145,96,10,C_PANEL);
  screen.fillRoundRect(165,48,145,96,10,C_PANEL);
  label("GESAMTGEWICHT",22,62);
  screen.setTextColor(C_TEXT);
  screen.setTextSize(3);
  screen.setCursor(22,88);
  if(hxHasSample)screen.print(formatWeightLcd(liveWeight));
  else screen.print(formatWeightLcd(0));
  screen.setTextSize(1);screen.print(" g");
  label("MATERIAL",177,62);
  screen.setTextColor(C_TEXT);
  screen.setTextSize(3);
  screen.setCursor(177,88);
  if(hxHasSample)screen.print(formatWeightLcd(max(0.0F,liveWeight-(findRoll(activeId)?findRoll(activeId)->spoolWeight:0.0F))));
  else screen.print(formatWeightLcd(0));
  screen.setTextSize(1);screen.print(" g");
  Roll*r=findRoll(activeId);
  screen.fillRoundRect(10,156,300,170,10,C_PANEL);
  if(r) {
    label("AKTIVE ROLLE",22,170,C_GREEN);
    screen.fillCircle(286,178,10,color565(r->color));
    screen.drawCircle(286,178,10,C_TEXT);
    screen.setTextColor(C_MUTED);
    screen.setTextSize(1);
    screen.setCursor(22,187);
    screen.print("ID: ");
    screen.print(r->id.substring(0,28));
    screen.setTextColor(C_TEXT);
    screen.setTextSize(2);
    screen.setCursor(22,204);
    screen.print(r->material);
    screen.setCursor(22,229);
    screen.print(r->maker);
    label("SPULE: "+r->spoolType,190,222);
    label(String(r->spoolWeight,0)+" g Offset",190,232);
    label("VERBRAUCH SEIT LETZTER WIEGUNG",22,258);
    screen.setTextColor(C_ORANGE);
    screen.setTextSize(2);
    screen.setCursor(22,274);
    // Verbrauch gemäß Datenmodell: aktuelles Materialgewicht minus zuletzt
    // in der Rollen-Datenbank gespeichertes Materialgewicht.
    if(hxHasSample)screen.print(formatWeightLcd(max(0.0F,liveWeight-r->spoolWeight)-r->weight)+" g");
    else screen.print(formatWeightLcd(0)+" g");
  }
  else {
    screen.setTextColor(C_TEXT);
    screen.setTextSize(2);
    screen.setCursor(22,190);
    screen.print(activeId.length()?"Neue Rolle: Auswahl im WebUI":"NFC-Tag an Leser halten");
  }
  button(6,344,74,62,"TARA",C_GREEN);
  // Ohne aktive Rolle ist kein Spulengewicht wählbar. Die frei werdende Fläche
  // nutzt der größere Wiegen-Button; NFC-Tags werden weiterhin automatisch gelesen.
  if(r) {
    button(84,344,152,62,"WIEGEN",C_GREEN);
    button(240,344,74,62,"SPULE",C_BLUE);
  }
  else button(84,344,230,62,"WIEGEN",C_MUTED);
  screen.setTextSize(1);
  screen.setTextColor(C_MUTED);
  screen.setCursor(10,458);
  screen.printf("v%s",FIRMWARE_VERSION);
  String ip=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString();
  screen.setCursor(314-ip.length()*6,458);
  screen.print(ip);
}

// Aktualisiert nur die dynamischen Zahlenfelder. So bleibt die Gewichtsanzeige
// flüssig, ohne dass das komplette LCD bei jedem HX711-Messwert flackert.
void drawLiveWeight() {
  if(spoolMenu || touchCalStep<4)return;

  Roll *r=findRoll(activeId);
  float netWeight=max(0.0F,liveWeight-(r?r->spoolWeight:0.0F));

  screen.fillRect(18,80,135,54,C_PANEL);
  screen.setTextColor(C_TEXT);
  screen.setTextSize(3);
  screen.setCursor(22,88);
  if(hxHasSample)screen.print(formatWeightLcd(liveWeight));
  else screen.print(formatWeightLcd(0));
  screen.setTextSize(1);
  screen.print(" g");

  screen.fillRect(173,80,135,54,C_PANEL);
  screen.setTextColor(C_TEXT);
  screen.setTextSize(3);
  screen.setCursor(177,88);
  if(hxHasSample)screen.print(formatWeightLcd(netWeight));
  else screen.print(formatWeightLcd(0));
  screen.setTextSize(1);
  screen.print(" g");

  if(r) {
    screen.fillRect(20,270,260,27,C_PANEL);
    screen.setTextColor(C_ORANGE);
    screen.setTextSize(2);
    screen.setCursor(22,274);
    if(hxHasSample)screen.print(formatWeightLcd(netWeight-r->weight)+" g");
    else screen.print(formatWeightLcd(0)+" g");
  }
}

// Direkter XPT2046-Zugriff: vermeidet, dass eine Bibliothek den LCD-SPI-Bus erneut umbelegt.
uint16_t xptRead(uint8_t command)  {
  SPI.beginTransaction(SPISettings(2500000,MSBFIRST,SPI_MODE0));
  digitalWrite(PIN_TOUCH_CS,LOW);
  SPI.transfer(command);
  uint16_t value=(SPI.transfer(0)<<8)|SPI.transfer(0);
  digitalWrite(PIN_TOUCH_CS,HIGH);
  SPI.endTransaction();
  return value>>3;
}
void mapTouch(uint16_t rawX,uint16_t rawY,int &x,int &y)  {
  if(touchCal.valid) {
    x=constrain((int)(touchCal.ax*rawX+touchCal.bx*rawY+touchCal.cx),0,319);
    y=constrain((int)(touchCal.ay*rawX+touchCal.by*rawY+touchCal.cy),0,479);
  }
  else {
    x=constrain(map(rawX,TOUCH_MIN_X,TOUCH_MAX_X,0,320),0,319);
    y=constrain(map(rawY,TOUCH_MIN_Y,TOUCH_MAX_Y,0,480),0,479);
  }
}
bool solveTouchAxis(float t0,float t1,float t2,float &a,float &b,float &c)  {
  float x0=touchCalRaw[0][0],x1=touchCalRaw[1][0],x2=touchCalRaw[2][0],y0=touchCalRaw[0][1],y1=touchCalRaw[1][1],y2=touchCalRaw[2][1];
  float d=x0*(y1-y2)+x1*(y2-y0)+x2*(y0-y1);
  if(fabsf(d)<1)return false;
  a=(t0*(y1-y2)+t1*(y2-y0)+t2*(y0-y1))/d;
  b=(x0*(t1-t2)+x1*(t2-t0)+x2*(t0-t1))/d;
  c=(x0*(y1*t2-y2*t1)+x1*(y2*t0-y0*t2)+x2*(y0*t1-y1*t0))/d;
  return true;
}
void finishTouchCalibration()  {
  if(!solveTouchAxis(TOUCH_CAL_X[0],TOUCH_CAL_X[1],TOUCH_CAL_X[2],touchCal.ax,touchCal.bx,touchCal.cx)||!solveTouchAxis(TOUCH_CAL_Y[0],TOUCH_CAL_Y[1],TOUCH_CAL_Y[2],touchCal.ay,touchCal.by,touchCal.cy)) {
    debugPrintln("[TOUCH] Kalibrierung ungueltig – erneut starten");
    touchCalStep=255;
    draw();
    return;
  }
  touchCal.valid=true;
  preferences.putFloat("tax",touchCal.ax);
  preferences.putFloat("tbx",touchCal.bx);
  preferences.putFloat("tcx",touchCal.cx);
  preferences.putFloat("tay",touchCal.ay);
  preferences.putFloat("tby",touchCal.by);
  preferences.putFloat("tcy",touchCal.cy);
  preferences.putBool("tcal",true);
  debugPrintf("[TOUCH] Kalibrierung gespeichert: x=%.4f/%.4f/%.1f y=%.4f/%.4f/%.1f\n",touchCal.ax,touchCal.bx,touchCal.cx,touchCal.ay,touchCal.by,touchCal.cy);
  touchCalStep=255;
  draw();
}
void handleTouch()  {
  static bool wasPressed=false;
  static unsigned long lastIdle=0;
  // Bei diesem Board bleibt T_IRQ bei einigen Touch-Modulen dauerhaft HIGH.
  // Deshalb entscheidet der tatsächlich gelesene XPT2046-Wert, nicht die IRQ-Leitung.
  uint16_t rawX=xptRead(0xD0),rawY=xptRead(0x90);
  bool pressed=(rawX>100 && rawY>100);
  if(!pressed)  {
    wasPressed=false;
    if(millis()-lastIdle>3000) {
      lastIdle=millis();
      debugPrintf("[TOUCH] Bereit (IRQ GPIO%d=%d, raw=%u/%u)\n",PIN_TOUCH_IRQ,digitalRead(PIN_TOUCH_IRQ),rawX,rawY);
    }
    return;
  }
  if(wasPressed)return;
  wasPressed=true;
  if(touchCalStep<4)  {
    touchCalRaw[touchCalStep][0]=rawX;
    touchCalRaw[touchCalStep][1]=rawY;
    debugPrintf("[TOUCH] Kalibrierpunkt %d: raw x=%u y=%u\n",touchCalStep+1,rawX,rawY);
    touchCalStep++;
    if(touchCalStep==4)finishTouchCalibration();
    else draw();
    return;
  }
  int x,y;
  mapTouch(rawX,rawY,x,y);
  debugPrintf("[TOUCH] Berührung: raw x=%u y=%u -> display x=%d y=%d\n",rawX,rawY,x,y);
  if(spoolMenu) {
    int column=x>=160, row=(y-90)/85;
    int index=row*2+column;
    if(index>=0 && index<(int)spoolTypes.size() && index<6 && findRoll(activeId)) {
      findRoll(activeId)->spoolWeight=spoolTypes[index].weight;
      findRoll(activeId)->spoolType=spoolTypes[index].name;
      saveDb();debugPrintf("[TOUCH] Spulenart: %s, Gewicht: %.0f g\n",spoolTypes[index].name.c_str(),spoolTypes[index].weight);
    }
    spoolMenu=false;draw();return;
  }
  if(!findRoll(activeId) && activeId.length() && y>=140 && y<=205 && x>=235) {
    uint8_t pick=constrain((x-240)/75,0,2);
    if(!entryStep) {
      const char* m[]= {
        "PLA","PETG","ABS"
      };
      selectedMaterial=m[pick];
      entryStep=1;
    }
    else {
      const char* h[]= {
        "eSUN","Prusament","Andere"
      };
      rolls.push_back( {
        activeId,selectedMaterial,h[pick],"#3b82f6",liveWeight,liveWeight
      });
      saveDb();
      entryStep=0;
    }
    draw();
    return;
  }
  if(y<340) {
    debugPrintln("[TOUCH] Kein Aktionsbutton");
    return;
  }
  Roll*r=findRoll(activeId);
  if(x<80) {
    debugPrintln("[TOUCH] Button: Tara");
    buttonPressed(6,344,74,62,"TARA");
    if(ENABLE_HX711) {
      tareRequested.store(true);
      debugPrintln("[HX711] Tara vorgemerkt für den nächsten sicheren Messwert");
    }
    draw();
    return;
  }
  if(!r && x>=80) {
    debugPrintln("[TOUCH] Wiegen gesperrt: keine aktive Rolle");
    return;
  }
  if(x>=80 && x<240) {
    debugPrintln("[TOUCH] Button: Wiegen");
    buttonPressed(84,344,r?152:230,62,"WIEGEN");
    if(r) {
      r->previous=r->weight;
      r->weight=max(0.0F,liveWeight-r->spoolWeight);
      saveDb();
      bool written=writeTag(*r);
      debugPrintf("[NFC] Daten nach Wiegen schreiben: %s\n",written?"OK":"FEHLER");
      draw();
    }
    else debugPrintln("[TOUCH] Keine aktive Rolle zum Wiegen");
    return;
  }
  if(r && x>=240) {
    debugPrintln("[TOUCH] Button: Spulengewicht");
    buttonPressed(240,344,74,62,"SPULE");
    spoolMenu=true;
    draw();
  }
}
