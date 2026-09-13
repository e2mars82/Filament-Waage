#include "App.h"

String esc(const String &s)  {
  String r=s;
  r.replace("\\", "\\\\");
  r.replace("\"", "\\\"");
  return r;
}
void loadCustomValues(const char *key,std::vector<String> &values) {
  values.clear();
  String packed=preferences.getString(key,"");
  int start=0;
  while(start<=packed.length()) {
    int end=packed.indexOf('\n',start);
    String value=packed.substring(start,end<0?packed.length():end);
    value.trim();
    if(value.length())values.push_back(value);
    if(end<0)break;
    start=end+1;
  }
}
void rememberCustomValue(const char *key,std::vector<String> &values,const String &value) {
  String clean=value;clean.trim();
  if(!clean.length())return;
  for(const String &existing:values)if(existing.equalsIgnoreCase(clean))return;
  values.push_back(clean);
  String packed;
  for(const String &entry:values) { if(packed.length())packed+='\n';packed+=entry; }
  preferences.putString(key,packed);
}
String jsonValues(const std::vector<String> &values) {
  String json="[";
  for(size_t i=0;i<values.size();i++) {
    if(i)json+=',';
    json+='"';
    json+=esc(values[i]);
    json+='"';
  }
  return json+"]";
}
void saveSpoolTypes() {
  JsonDocument doc;JsonArray list=doc.to<JsonArray>();
  for(const SpoolType &type:spoolTypes) { JsonObject item=list.add<JsonObject>();item["name"]=type.name;item["weight"]=type.weight; }
  String text;serializeJson(doc,text);preferences.putString("spoolTypes",text);
}
void loadSpoolTypes() {
  spoolTypes.clear();String text=preferences.getString("spoolTypes","");JsonDocument doc;
  if(text.length() && !deserializeJson(doc,text)) for(JsonObject item:doc.as<JsonArray>())spoolTypes.push_back({item["name"]|"Spule",item["weight"]|0.0F});
  if(spoolTypes.empty()) { spoolTypes={{"Standard",0},{"Karton",150},{"Kunststoff",250}};saveSpoolTypes(); }
}
String spoolsJson() {
  String json="[";for(size_t i=0;i<spoolTypes.size();i++){if(i)json+=',';json+="{\"name\":\""+esc(spoolTypes[i].name)+"\",\"weight\":"+String(spoolTypes[i].weight,1)+"}";}return json+"]";
}
String createId()  {
  MD5Builder md5;
  String seed=String((uint32_t)time(nullptr))+"-"+String((uint32_t)ESP.getEfuseMac(),HEX)+"-"+String(esp_random(),HEX);
  md5.begin();
  md5.add(seed);
  md5.calculate();
  return "NFC-"+md5.toString();
}
Roll* findRoll(const String &id)  {
  for (auto &r: rolls) if (r.id == id) return &r;
  return nullptr;
}
void saveDb();
// Eine neue NFC-ID wird sofort auf der SD-Karte dokumentiert. Die Rolle lässt
// sich anschließend im WebUI über „Bearbeiten“ mit Material und Hersteller ergänzen.
Roll* addUnknownRoll(const String &id)  {
  if(!id.length()) return nullptr;
  if(Roll *known=findRoll(id)) return known;
  rolls.push_back( {
    id,"Unbekannt","Unbekannt","#64748b",liveWeight,liveWeight
  });
  saveDb();
  debugPrintf("[DB] Unbekannte NFC-Rolle angelegt: %s\n",id.c_str());
  return &rolls.back();
}
void saveDb()  {
  if (!sdReady)  {
    debugPrintln("[DB] SD-Datenbank deaktiviert");
    return;
  }
  JsonDocument d;
  JsonArray a=d.to<JsonArray>();
  for (auto &r: rolls)  {
    JsonObject o=a.add<JsonObject>();
    o["id"]=r.id;
    o["material"]=r.material;
    o["maker"]=r.maker;
    o["color"]=r.color;
    o["weight"]=r.weight;
    o["previous"]=r.previous;
    o["spoolWeight"]=r.spoolWeight;
    o["spoolType"]=r.spoolType;
  }
  File f=SD.open(DB_FILE, FILE_WRITE);
  if (f)  {
    serializeJson(d,f);
    f.close();
  }
  debugPrintf("[DB] %u Rollen auf SD gespeichert\n", rolls.size());
}
void loadDb()  {
  if (!sdReady) return;
  File f=SD.open(DB_FILE);
  if (!f)  {
    debugPrintln("[DB] Neue Datenbank wird angelegt");
    saveDb();
    return;
  }
  JsonDocument d;
  if (deserializeJson(d,f))  {
    debugPrintln("[DB] FEHLER: filament.json ist ungueltig");
    f.close();
    return;
  }
  f.close();
  for (JsonObject o:d.as<JsonArray>()) {
    Roll roll={o["id"]|"",o["material"]|"",o["maker"]|"",o["color"]|"#3b82f6",o["weight"]|0.0F,o["previous"]|0.0F};
    roll.spoolWeight=o["spoolWeight"]|0.0F;
    roll.spoolType=o["spoolType"]|"Standard";
    rolls.push_back(roll);
  }
  debugPrintf("[DB] %u Rollen geladen\n", rolls.size());
}
String rollsJson()  {
  String s="[";
  for (size_t i=0;
  i<rolls.size();
  i++)  {
    auto&r=rolls[i];
    if(i)s+=',';
    s+="{\"id\":\""+esc(r.id)+"\",\"material\":\""+esc(r.material)+"\",\"maker\":\""+esc(r.maker)+"\",\"color\":\""+esc(r.color)+"\",\"weight\":"+String(r.weight,1)+",\"previous\":"+String(r.previous,1)+",\"spoolWeight\":"+String(r.spoolWeight,1)+",\"spoolType\":\""+esc(r.spoolType)+"\"}";
  }
  return s+"]";
}
