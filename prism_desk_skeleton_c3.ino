#include <Arduino.h>
#include <algorithm>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <sys/time.h>
#include <time.h>
#include "config.h"
#include "device_model.h"
#include "frame_canvas.h"
#include "display_transform.h"
#include "display_brightness.h"
#include "status_display.h"
#include "statistics_store.h"
#include "network_service.h"
#include "ble_transport.h"
#include "command_protocol.h"

class PrismDisplay : public Adafruit_ST7789 {
 public:
  using Adafruit_ST7789::Adafruit_ST7789;

 protected:
  // init() calls begin() before sending the first panel command.
  void begin(uint32_t) override { Adafruit_ST77xx::begin(config::tftInitHz); }
};
PrismDisplay display(&SPI,config::tftCs,config::tftDc,config::tftRst);
FrameCanvas canvas;
optics::Brightness brightness;
status::Display statuses;
desk::App app;
StatisticsStore statisticsStore;
network::Service networking;
network::Snapshot connection;
ble::Transport bluetooth;
QueueHandle_t commands=nullptr;
protocol::LineBuffer usbFrame;
bool storageOk=true,bleOk=false,networkOk=false;
uint32_t lastSaveAttempt=0,lastHealth=0,completedFrames=0;
char usbOutput[768]={};size_t usbOutputSize=0,usbOutputAt=0;

struct Button {
  int pin;
  bool raw=false,pressed=false,longSent=false;
  uint32_t changed=0,downAt=0;
  void begin() {pinMode(pin,INPUT_PULLUP);raw=pressed=digitalRead(pin)==LOW;changed=downAt=millis();}
  // 1: short release, 2: 0.75s hold release, 3: 2s hold (once).
  unsigned poll(uint32_t now) {
    const bool value=digitalRead(pin)==LOW;
    if(value!=raw) {raw=value;changed=now;}
    if(raw!=pressed && uint32_t(now-changed)>=config::debounceMs) {
      pressed=raw;
      if(pressed) {downAt=now;longSent=false;}
      else if(!longSent) return uint32_t(now-downAt)>=750?2:1;
    }
    if(pressed && !longSent && uint32_t(now-downAt)>=2000) {longSent=true;return 3;}
    return 0;
  }
};
Button buttons[]={{config::buttonBack},{config::buttonNext},{config::buttonAction}};

bool wallTime(tm& result) {
  const time_t now=time(nullptr);
  return now>=1704067200 && uint64_t(now)<4102444800ULL && localtime_r(&now,&result);
}
uint32_t currentDay() {
  tm local{};if(!wallTime(local)) return 0;
  return (local.tm_year+1900)*10000+(local.tm_mon+1)*100+local.tm_mday;
}
void showStatus(status::State value) {statuses.show(value,millis());}
void presentRows(unsigned first, unsigned count) {
  const uint16_t* source=canvas.getBuffer();
  // A logical pixel covers 2 x 2 panel pixels. Prepare both rows once.
  uint16_t rows[2 * config::screenWidth];
  display.startWrite();display.setAddrWindow(0,first,config::screenWidth,count);
  for(unsigned y=first;y<first+count;y+=2) {
    for(int x=0;x<config::canvasWidth;++x) {
      const uint16_t pixel=brightness.apply(source[optics::sourceIndex(x,y/2,config::canvasWidth,config::mirrorForPrism)]);
      rows[2*x]=rows[2*x+1]=pixel;
    }
    memcpy(rows+config::screenWidth,rows,config::screenWidth*sizeof(uint16_t));
    display.writePixels(rows,2*config::screenWidth);
  }
  display.endWrite();
}
void centered(int y,const char* value,unsigned scale=1,uint16_t color=ST77XX_WHITE) {
  canvas.setTextSize(scale);canvas.setTextColor(color);
  const int width=int(strlen(value))*6*scale;
  canvas.setCursor((config::canvasWidth-width)/2,y);canvas.print(value);
}
void openBluetooth() {
  if(bluetooth.open(millis())) {app.selectMode(desk::Mode::Bluetooth);showStatus(status::State::BleReady);}
}
void sendReply(JsonDocument& response,bool viaBle) {
  char output[513];
  if(measureJson(response)>512) {response.clear();response["v"]=1;response["ok"]=false;response["error"]="response_too_large";}
  const size_t length=serializeJson(response,output,sizeof(output));
  if(viaBle) bluetooth.reply(output);
  else {memcpy(usbOutput,output,length);usbOutput[length]='\n';usbOutputSize=length+1;usbOutputAt=0;}
}
void processCommand(const protocol::Packet& packet) {
  protocol::Command command;
  const char* error=protocol::parse(packet.json,command);
  JsonDocument response;response["v"]=1;response["id"]=command.id;response["ok"]=!error;
  if(error) {response["error"]=error;sendReply(response,packet.ble);return;}
  const uint32_t now=millis();
  switch(command.kind) {
    case protocol::Kind::Widget:
      strlcpy(app.widget.title,command.title,sizeof(app.widget.title));
      strlcpy(app.widget.value,command.value,sizeof(app.widget.value));
      strlcpy(app.widget.detail,command.detail,sizeof(app.widget.detail));
      app.widget.received=true;app.showWidget(now,packet.ble);break;
    case protocol::Kind::Wifi:
      if(!networking.configure(command.ssid,command.password)) error="wifi_configuration_rejected";
      else {showStatus(status::State::WifiConnecting);response["accepted"]=true;}
      break;
    case protocol::Kind::Time: {
      timeval value{time_t(command.epoch),0};
      if(settimeofday(&value,nullptr)) error="time_rejected";
      else showStatus(status::State::Ready);
      break;
    }
    case protocol::Kind::Pomodoro:
      if(!strcmp(command.action,"configure")) {
        if(!app.timer.configure(command.workMinutes,command.breakMinutes,now)) error="timer_running";
      } else if(!strcmp(command.action,"reset")) {app.timer.reset(now);app.alarm=false;}
      else if(!strcmp(command.action,"pause")) {if(app.timer.running) app.timer.click(now);}
      else if(!app.timer.running) {app.timer.click(now);app.alarm=false;}
      app.mode=desk::Mode::Pomodoro;break;
    case protocol::Kind::BleOpen:
      openBluetooth();response["available"]=bleOk;
      // Local USB possession can bootstrap pairing; never return credentials.
      if(!packet.ble && bleOk) response["passkey"]=bluetooth.passkey();
      break;
    case protocol::Kind::State: {
      response["mode"]=unsigned(app.mode);
      response["wifi"]=connection.wifi;response["ble"]=bluetooth.connected();
      response["wifi_portal"]=connection.portal;
      response["time_valid"]=currentDay()!=0;response["storage_ok"]=storageOk;
      auto timer=response["pomodoro"].to<JsonObject>();
      timer["phase"]=app.timer.phase==desk::Phase::Focus?"focus":"break";
      timer["running"]=app.timer.running;timer["remaining_s"]=(app.timer.remainingMs+999)/1000;
      timer["awaiting"]=app.timer.awaiting;
      if(connection.quoteValid) {
        auto quote=response["snap"].to<JsonObject>();quote["price"]=connection.price;
        quote["change_pct"]=connection.change;quote["unix"]=connection.quoteTime;
        quote["age_s"]=uint32_t(now-connection.receivedAt)/1000;
      }
      break;
    }
    case protocol::Kind::Stats: {
      response["today"]=currentDay();response["sessions"]=app.stats.data.sessions;
      response["seconds"]=app.stats.data.seconds;
      response["undated_sessions"]=app.stats.data.undatedSessions;
      response["undated_seconds"]=app.stats.data.undatedSeconds;
      response["saved"]=!app.stats.dirty;
      auto days=response["days"].to<JsonArray>();
      for(const auto& day:app.stats.data.days) if(day.day) {
        auto row=days.add<JsonObject>();row["day"]=day.day;row["sessions"]=day.sessions;row["seconds"]=day.seconds;
      }
      break;
    }
  }
  if(error) {response["ok"]=false;response["error"]=error;}
  sendReply(response,packet.ble);
  // Do not retain a second in-RAM copy of submitted Wi-Fi credentials.
  memset(command.password,0,sizeof(command.password));
}
void serviceCommands() {
  // HWCDC's connection check re-arms TX after a host reconnect. Checking only
  // availableForWrite() can leave a full old TX ring stalled indefinitely.
  const bool usbConnected=bool(Serial);
  if(usbConnected && usbOutputAt<usbOutputSize) {
    const size_t available=Serial.availableForWrite();
    const size_t left=usbOutputSize-usbOutputAt;
    if(available) usbOutputAt+=Serial.write(reinterpret_cast<const uint8_t*>(usbOutput+usbOutputAt),available<left?available:left);
  }
  if(usbOutputAt==usbOutputSize) {
    usbOutputAt=usbOutputSize=0;
    protocol::Packet packet;
    for(unsigned i=0;i<256 && Serial.available();++i) {
      const auto result=usbFrame.push(char(Serial.read()),packet.json);
      if(result==protocol::Feed::Message) {processCommand(packet);break;}
      if(result==protocol::Feed::TooLong) {
        JsonDocument response;response["v"]=1;response["ok"]=false;response["error"]="message_too_long";sendReply(response,false);break;
      }
    }
  }
  protocol::Packet packet;
  if(commands && xQueueReceive(commands,&packet,0)==pdTRUE) processCommand(packet);
  if(bluetooth.takeError()) {
    JsonDocument response;response["v"]=1;response["ok"]=false;response["error"]="rx_overflow";sendReply(response,true);
  }
}
void updateNetwork() {
  static network::State previous=network::State::Connecting;
  static bool first=true;
  const bool wasPortal=connection.portal;
  if(!networking.poll(connection)) return;
  if(connection.portal && !wasPortal) app.selectMode(desk::Mode::WifiSetup);
  else if(!connection.portal && wasPortal && app.mode==desk::Mode::WifiSetup) app.selectMode(desk::Mode::Snap);
  if(first || connection.state!=previous) {
    first=false;previous=connection.state;
    switch(connection.state) {
      case network::State::Connecting:showStatus(status::State::WifiConnecting);break;
      case network::State::NoCredentials:showStatus(status::State::WifiSetup);break;
      case network::State::Syncing:showStatus(status::State::TimeSync);break;
      case network::State::Ready:showStatus(status::State::Ready);break;
      case network::State::Failed:showStatus(status::State::WifiFailed);break;
      case network::State::MissingKey:showStatus(status::State::MissingKey);break;
      case network::State::QuoteFailed:showStatus(status::State::QuoteFailed);break;
      case network::State::SetupPortal:showStatus(status::State::WifiSetup);break;
    }
    if(Serial.availableForWrite()>=96) Serial.printf("[network] state=%u wifi=%u quote=%u\n",unsigned(connection.state),connection.wifi,connection.quoteValid);
  }
}
void draw(uint32_t now) {
  canvas.fillScreen(ST77XX_BLACK);
  static const char* const names[]={"CLOCK","SNAP","FOCUS","TODAY","WIDGET","BLUETOOTH","WIFI SETUP"};
  centered(3,names[unsigned(app.mode)]);
  canvas.drawFastHLine(4,15,112,0x4208);
  char text[40];tm local{};
  switch(app.mode) {
    case desk::Mode::Clock:
      if(wallTime(local)) {
        snprintf(text,sizeof(text),"%02d:%02d",local.tm_hour,local.tm_min);centered(39,text,3);
        snprintf(text,sizeof(text),"%04d-%02d-%02d",local.tm_year+1900,local.tm_mon+1,local.tm_mday);centered(75,text);
      } else {centered(43,"--:--",3);centered(77,"WAITING FOR TIME");}
      break;
    case desk::Mode::Snap:
      if(connection.quoteValid) {
        snprintf(text,sizeof(text),"$%.2f",double(connection.price));centered(32,text,strlen(text)<=10?2:1);
        snprintf(text,sizeof(text),"%+.2f%%",double(connection.change));centered(57,text,1,connection.change>=0?ST77XX_GREEN:ST77XX_RED);
        const time_t quoteAt=connection.quoteTime;tm quote{};localtime_r(&quoteAt,&quote);
        snprintf(text,sizeof(text),"LAST %02d/%02d %02d:%02d",quote.tm_mday,quote.tm_mon+1,quote.tm_hour,quote.tm_min);centered(76,text);
        if(!connection.wifi || uint32_t(now-connection.receivedAt)>180000) centered(89,"CACHED / OLD DATA");
      } else {
        centered(36,"--.--",3);centered(72,"NO QUOTE YET");
        centered(86,connection.wifi?"PRESS TO RETRY":"SET UP WI-FI");
      }
      break;
    case desk::Mode::Pomodoro: {
      centered(23,app.timer.phase==desk::Phase::Focus?"WORK":"BREAK");
      const unsigned seconds=(app.timer.remainingMs+999)/1000;
      snprintf(text,sizeof(text),"%02u:%02u",seconds/60,seconds%60);centered(42,text,3);
      centered(77,app.timer.awaiting?"PHASE DONE - PRESS":app.timer.running?"RUNNING":"PRESS TO START");
      centered(90,"HOLD 0.75s TO RESET");break;
    }
    case desk::Mode::Stats: {
      const uint32_t day=currentDay();const auto today=app.stats.today(day);
      if(day) {snprintf(text,sizeof(text),"%04u-%02u-%02u",day/10000,day/100%100,day%100);centered(24,text);}
      else centered(24,"DATE NOT SET");
      snprintf(text,sizeof(text),"%u SESSIONS",today.sessions);centered(43,text,strlen(text)<=10?2:1);
      snprintf(text,sizeof(text),"%u FOCUS MIN",today.seconds/60);centered(68,text);
      snprintf(text,sizeof(text),"ALL TIME: %u",app.stats.data.sessions);centered(82,text);
      if(app.stats.data.undatedSessions) {snprintf(text,sizeof(text),"UNDATED: %u",app.stats.data.undatedSessions);centered(94,text);}
      break;
    }
    case desk::Mode::Widget:
      centered(25,app.widget.title);
      centered(46,app.widget.value,strlen(app.widget.value)<=10?2:1);
      centered(79,app.widget.detail);break;
    case desk::Mode::Bluetooth:
      centered(24,"PRISM DESK");
      if(bluetooth.connected()) {centered(47,"CONNECTED");centered(76,"READY FOR DATA");}
      else if(bluetooth.windowOpen(now)) {
        centered(41,"PAIR CODE");snprintf(text,sizeof(text),"%06u",bluetooth.passkey());centered(57,text,2);
        centered(86,"CONNECT FROM PC");
      } else {centered(44,bleOk?"HOLD ACTION 2s":"BLE UNAVAILABLE");centered(74,"TO CONNECT");}
      break;
    case desk::Mode::WifiSetup:
      if(connection.portal) {
        centered(24,"Prism-Setup");centered(41,"PASSWORD");
        centered(55,connection.portalPassword,2);
        centered(81,"192.168.4.1");centered(94,"CHOOSE YOUR WIFI");
      } else {
        centered(36,"PRESS ACTION");centered(57,"TO SET UP WIFI");
        centered(82,"HOLD < AND > 2s");
      }
      break;
    case desk::Mode::Count:break;
  }
  if(app.alarm && (now/500)%2==0) {
    canvas.fillRect(0,99,120,9,ST77XX_WHITE);
    centered(100,"FOCUS DONE",1,ST77XX_BLACK);
  }
  char statusLine[scramble::Animation::cols+1];statuses.line(now,statusLine);
  canvas.setTextSize(1);canvas.setTextColor(ST77XX_WHITE);
  canvas.setCursor(statuses.textOffset(1),111);canvas.print(statusLine);
}
void setup() {
  Serial.begin(115200);
  setenv("TZ",config::timezone,1);tzset();
  for(auto& button:buttons) button.begin();
  brightness.begin(config::imageGain,config::imageGamma);statuses.begin(millis());
  SPI.begin(config::tftSck,-1,config::tftMosi,config::tftCs);
  display.init(config::screenWidth,config::screenHeight,SPI_MODE3);
  display.setRotation(0);display.invertDisplay(true);
  display.setSPISpeed(config::tftDrawHz);
  canvas.setTextWrap(false);canvas.fillScreen(ST77XX_BLACK);centered(52,"LOADING",2);
  presentRows(0,config::screenHeight);
  storageOk=statisticsStore.begin(app.stats);
  commands=xQueueCreate(4,sizeof(protocol::Packet));
  bleOk=bluetooth.begin(commands);networkOk=networking.begin();
  if(!storageOk) showStatus(status::State::StorageError);
  Serial.printf("[boot] widgets ble=%u network=%u storage=%u heap=%u largest=%u\n",bleOk,networkOk,storageOk,unsigned(ESP.getFreeHeap()),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  lastHealth=millis();
}
void loop() {
  const uint32_t now=millis();
  // Account for elapsed time before handling a pause/reset command.
  if(app.update(now,currentDay())) {
    lastSaveAttempt=now;storageOk=statisticsStore.save(app.stats);
    showStatus(storageOk?status::State::StatsSaved:status::State::StorageError);
  }
  if(app.stats.dirty && uint32_t(now-lastSaveAttempt)>=5000) {
    lastSaveAttempt=now;storageOk=statisticsStore.save(app.stats);
    if(storageOk) showStatus(status::State::StatsSaved);
  }
  const unsigned back=buttons[0].poll(now),next=buttons[1].poll(now),action=buttons[2].poll(now);
  if(back || next || action) app.cancelWidgetPreview();
  static bool wifiComboHandled=false;
  if(buttons[0].pressed && buttons[1].pressed) {
    if(!wifiComboHandled && uint32_t(now-buttons[0].downAt)>=2000 && uint32_t(now-buttons[1].downAt)>=2000) {
      wifiComboHandled=true;networking.openPortal();app.selectMode(desk::Mode::WifiSetup);
    }
  } else wifiComboHandled=false;
  if(back==1 || back==2) app.navigate(-1);
  if(next==1 || next==2) app.navigate(1);
  if(action==3) openBluetooth();
  else if(action==2 && app.mode==desk::Mode::Pomodoro) {app.timer.reset(now);app.alarm=false;}
  else if(action==1) {
    if(app.mode==desk::Mode::WifiSetup) networking.openPortal();
    else if(app.mode==desk::Mode::Bluetooth) openBluetooth();
    else if(app.mode==desk::Mode::Snap && !app.alarm) networking.refresh();
    else app.action(now);
  }
  serviceCommands();bluetooth.update(millis());updateNetwork();
  static bool wasBleConnected=false;
  if(wasBleConnected!=bluetooth.connected()) {
    wasBleConnected=bluetooth.connected();showStatus(wasBleConnected?status::State::BleConnected:status::State::BleReady);
  }
  // Service buttons and USB between 40-row strips.
  static unsigned nextRow=0;
  static_assert(config::screenHeight%2==0, "2x scaling requires an even panel height");
  if(nextRow==0) draw(now);
  const unsigned rows=std::min(40u,config::screenHeight-nextRow);
  presentRows(nextRow,rows);
  nextRow+=rows;
  if(nextRow==config::screenHeight) {nextRow=0;++completedFrames;}
  if(uint32_t(now-lastHealth)>=30000 && !usbOutputSize && Serial.availableForWrite()>=96) {
    const uint32_t measuredAt=millis();
    const float fps=1000.0f*completedFrames/uint32_t(measuredAt-lastHealth);
    lastHealth=measuredAt;completedFrames=0;
    Serial.printf("[health] frame=ok fps=%.2f heap=%u largest=%u\n",fps,unsigned(ESP.getFreeHeap()),unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  }
  delay(1);
}
