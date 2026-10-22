#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <string.h>

namespace protocol {
enum class Kind { Widget, Wifi, Time, Pomodoro, State, Stats, BleOpen };
struct Command {
  Kind kind=Kind::State;
  uint32_t id=0;
  char title[21]={},value[21]={},detail[21]={};
  char ssid[33]={},password[65]={},action[12]={};
  uint64_t epoch=0;
  unsigned workMinutes=25,breakMinutes=5;
};
inline bool text(JsonVariantConst field,char* result,size_t capacity,bool ascii=false,bool empty=false) {
  if(!field.is<const char*>()) return false;
  JsonString string=field.as<JsonString>();
  const size_t length=string.size();
  if((!length && !empty) || length>=capacity || strlen(string.c_str())!=length) return false;
  for(size_t i=0;i<length;++i) if(ascii && (string.c_str()[i]<32 || string.c_str()[i]>126)) return false;
  memcpy(result,string.c_str(),length);result[length]=0;return true;
}
inline const char* parse(const char* message,Command& result) {
  JsonDocument doc;
  if(deserializeJson(doc,message) || !doc.is<JsonObject>()) return "invalid_json";
  if(!doc["id"].isNull() && !doc["id"].is<uint32_t>()) return "invalid_id";
  result.id=doc["id"]|uint32_t(0);
  if(!doc["v"].is<unsigned>() || doc["v"].as<unsigned>()!=1) return "unsupported_version";
  if(!doc["type"].is<const char*>()) return "missing_type";
  const char* type=doc["type"];
  if(!strcmp(type,"widget")) {
    result.kind=Kind::Widget;
    if(!text(doc["title"],result.title,sizeof(result.title),true) ||
       !text(doc["value"],result.value,sizeof(result.value),true)) return "invalid_widget_text";
    if(!doc["detail"].isNull() && !text(doc["detail"],result.detail,sizeof(result.detail),true,true)) return "invalid_widget_text";
  } else if(!strcmp(type,"wifi")) {
    result.kind=Kind::Wifi;
    if(!text(doc["ssid"],result.ssid,sizeof(result.ssid)) ||
       !text(doc["password"],result.password,sizeof(result.password),false,true)) return "invalid_wifi";
    const size_t n=strlen(result.password);
    if(n && n<8) return "invalid_wifi_password";
    if(n==64) for(unsigned i=0;i<64;++i)
      if(!strchr("0123456789abcdefABCDEF",result.password[i])) return "invalid_wifi_password";
  } else if(!strcmp(type,"time")) {
    result.kind=Kind::Time;
    if(!doc["unix"].is<uint64_t>()) return "invalid_time";
    result.epoch=doc["unix"].as<uint64_t>();
    if(result.epoch<1704067200ULL || result.epoch>=4102444800ULL) return "invalid_time";
  } else if(!strcmp(type,"pomodoro")) {
    result.kind=Kind::Pomodoro;
    if(!text(doc["action"],result.action,sizeof(result.action),true)) return "invalid_action";
    if(strcmp(result.action,"start") && strcmp(result.action,"pause") && strcmp(result.action,"resume") &&
       strcmp(result.action,"reset") && strcmp(result.action,"configure")) return "invalid_action";
    if(!strcmp(result.action,"configure")) {
      if(!doc["work_min"].is<unsigned>() || !doc["break_min"].is<unsigned>()) return "invalid_duration";
      result.workMinutes=doc["work_min"];result.breakMinutes=doc["break_min"];
      if(result.workMinutes<1 || result.workMinutes>180 || result.breakMinutes<1 || result.breakMinutes>60) return "invalid_duration";
    }
  } else if(!strcmp(type,"get")) {
    const char* what=doc["what"]|"state";
    if(!strcmp(what,"state")) result.kind=Kind::State;
    else if(!strcmp(what,"stats")) result.kind=Kind::Stats;
    else return "invalid_query";
  } else if(!strcmp(type,"ble_open")) result.kind=Kind::BleOpen;
  else return "unknown_type";
  return nullptr;
}
}
