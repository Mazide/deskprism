#include "network_service.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include <time.h>
#include <math.h>
#include "config.h"

// Keep the Finnhub key in an ignored local header.
#if __has_include("secrets.h")
#include "secrets.h"
#else
namespace Secrets { constexpr const char* FINNHUB_TOKEN=""; }
#endif
extern const uint8_t certStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t certEnd[] asm("_binary_x509_crt_bundle_end");

namespace network {
bool Service::begin() {
  updates=xQueueCreate(1,sizeof(Snapshot)); credentials=xQueueCreate(1,sizeof(Credentials));
  if(!updates || !credentials) return false;
  return xTaskCreate(task,"prism-network",12288,this,1,nullptr)==pdPASS;
}
bool Service::configure(const char* ssid,const char* password) {
  if(!credentials || !ssid || !password || !ssid[0] || strlen(ssid)>32 || strlen(password)>64) return false;
  Credentials value{}; strlcpy(value.ssid,ssid,sizeof(value.ssid));strlcpy(value.password,password,sizeof(value.password));
  return xQueueOverwrite(credentials,&value)==pdTRUE;
}
bool Service::poll(Snapshot& value) { return updates && xQueueReceive(updates,&value,0)==pdTRUE; }
void Service::task(void* self) { static_cast<Service*>(self)->run();vTaskDelete(nullptr); }

static bool fetch(Snapshot& snapshot) {
  WiFiClientSecure client;
  client.setCACertBundle(certStart,certEnd-certStart);
  client.setHandshakeTimeout(8);
  HTTPClient http;http.setConnectTimeout(8000);http.setTimeout(8000);
  if(!http.begin(client,"https://finnhub.io/api/v1/quote?symbol=SNAP")) return false;
  http.addHeader("X-Finnhub-Token",Secrets::FINNHUB_TOKEN);
  const int code=http.GET();
  if(code!=HTTP_CODE_OK) { http.end();return false; }
  JsonDocument doc;
  const auto error=deserializeJson(doc,http.getString());http.end();
  if(error || !doc["c"].is<float>() || !doc["dp"].is<float>() || !doc["t"].is<uint32_t>()) return false;
  const float price=doc["c"].as<float>(), change=doc["dp"].as<float>();
  const uint32_t timestamp=doc["t"].as<uint32_t>();
  if(!isfinite(price) || price<=0 || !isfinite(change) || timestamp<1704067200 || timestamp>uint64_t(time(nullptr))+300) return false;
  snapshot.price=price;snapshot.change=change;snapshot.quoteTime=timestamp;
  snapshot.receivedAt=millis();snapshot.quoteValid=true;return true;
}
void Service::run() {
  Snapshot value;
  WiFi.persistent(true);WiFi.mode(WIFI_STA);
  // This Super Mini cannot broadcast reliably at the default RF power.
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.setAutoReconnect(true);WiFi.begin();
  WiFiManager portal;
  portal.setDebugOutput(false);
  portal.setConfigPortalBlocking(false);
  portal.setConfigPortalTimeout(180);
  portal.setConnectTimeout(20);
  // WiFiManager restarts the radio when switching from station to AP mode.
  portal.setAPCallback([](WiFiManager*) { WiFi.setTxPower(WIFI_POWER_8_5dBm); });
  portal.setAPStaticIPConfig(IPAddress(192,168,4,1),IPAddress(192,168,4,1),IPAddress(255,255,255,0));
  bool saved=portal.getWiFiIsSaved(),offered=false;
  uint32_t portalStarted=0;
  uint32_t started=millis(),lastAttempt=0,lastPublish=millis();
  bool first=true,wasConnected=false;
  State previous=State::Connecting;
  for(;;) {
    Credentials requested{};
    if(xQueueReceive(credentials,&requested,0)==pdTRUE) {
      if(portal.getConfigPortalActive()) portal.stopConfigPortal();
      value.portal=false;memset(value.portalPassword,0,sizeof(value.portalPassword));
      WiFi.disconnect(false,false);
      WiFi.begin(requested.ssid,requested.password);
      memset(&requested,0,sizeof(requested));
      started=millis();first=true;wasConnected=false;
      saved=true;offered=false;
    }
    const bool requestedPortal=portalRequested.exchange(false);
    const bool unavailable=WiFi.status()!=WL_CONNECTED;
    if(!portal.getConfigPortalActive() &&
       (requestedPortal || (!offered && unavailable && (!saved || uint32_t(millis()-started)>=20000)))) {
      snprintf(value.portalPassword,sizeof(value.portalPassword),"%08X",unsigned(esp_random()));
      // Keep the radio running while WiFiManager disables STA, so the power
      // limit is already active before the first access-point beacon.
      WiFi.enableAP(true);
      WiFi.setTxPower(WIFI_POWER_8_5dBm);
      portal.startConfigPortal("Prism-Setup",value.portalPassword);
      const IPAddress apIp=WiFi.softAPIP();
      const bool apStarted=(WiFi.getMode()&WIFI_AP) && apIp!=IPAddress(0,0,0,0);
      wifi_config_t apConfig{};
      const esp_err_t apResult=esp_wifi_get_config(WIFI_IF_AP,&apConfig);
      if(Serial.availableForWrite()>=128)
        Serial.printf("[portal] ap=%u ip=%u.%u.%u.%u heap=%u config=%d ssid=%.*s hidden=%u channel=%u mode=%u\n",apStarted,apIp[0],apIp[1],apIp[2],apIp[3],unsigned(ESP.getFreeHeap()),int(apResult),int(apConfig.ap.ssid_len),apConfig.ap.ssid,apConfig.ap.ssid_hidden,apConfig.ap.channel,unsigned(WiFi.getMode()));
      if(!apStarted) portal.stopConfigPortal();
      offered=true;portalStarted=millis();
    }
    const bool hadPortal=value.portal;
    portal.process();
    if(portal.getConfigPortalActive() && uint32_t(millis()-portalStarted)>=180000) portal.stopConfigPortal();
    value.portal=portal.getConfigPortalActive();
    if(hadPortal && !value.portal) {
      memset(value.portalPassword,0,sizeof(value.portalPassword));
      saved=portal.getWiFiIsSaved();first=true;started=millis();
    }
    value.wifi=WiFi.status()==WL_CONNECTED;
    const uint32_t now=millis();
    if(value.portal) {
      value.state=State::SetupPortal;
    } else if(!value.wifi) {
      if(!saved) value.state=State::NoCredentials;
      else value.state=uint32_t(now-started)>20000?State::Failed:State::Connecting;
      wasConnected=false;
    } else {
      if(!wasConnected) {
        configTzTime(config::timezone,"pool.ntp.org","time.nist.gov");
        wasConnected=true;first=true;
      }
      if(time(nullptr)<1704067200) value.state=State::Syncing;
      else if(!Secrets::FINNHUB_TOKEN[0]) value.state=State::MissingKey;
      else {
        const bool manual=refreshRequested.exchange(false);
        if(first || uint32_t(now-lastAttempt)>=60000 || (manual && uint32_t(now-lastAttempt)>=5000)) {
          first=false;lastAttempt=now;
          value.state=fetch(value)?State::Ready:State::QuoteFailed;
        }
      }
    }
    if(value.state!=previous || uint32_t(now-lastPublish)>=1000) {
      xQueueOverwrite(updates,&value);previous=value.state;lastPublish=now;
    }
    delay(100);
  }
}
}
