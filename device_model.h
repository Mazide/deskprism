#pragma once
#include <stdint.h>
#include <string.h>

namespace desk {
enum class Mode : uint8_t { Clock, Snap, Pomodoro, Stats, Widget, Bluetooth, WifiSetup, Count };
enum class Phase : uint8_t { Focus, Break };

struct DayStats { uint32_t day=0, sessions=0, seconds=0; };
struct StatsData {
  uint32_t magic=0x50525331, version=1;
  DayStats days[7] = {};
  uint32_t sessions=0, seconds=0, undatedSessions=0, undatedSeconds=0;
};

class Statistics {
 public:
  StatsData data;
  bool dirty=false;
  DayStats today(uint32_t day) const {
    if (day) for (const auto& row:data.days) if(row.day==day) return row;
    return {day,0,0};
  }
  void record(uint32_t day,uint32_t seconds) {
    ++data.sessions; data.seconds+=seconds; dirty=true;
    if (!day) { ++data.undatedSessions; data.undatedSeconds+=seconds; return; }
    DayStats* selected=nullptr;
    for(auto& row:data.days) if(row.day==day) {selected=&row;break;}
    if(!selected) {
      selected=&data.days[0];
      for(auto& row:data.days) if(row.day<selected->day) selected=&row;
      *selected={day,0,0};
    }
    ++selected->sessions; selected->seconds+=seconds;
  }
};

class Pomodoro {
 public:
  Phase phase=Phase::Focus;
  uint32_t workMs=25*60000, breakMs=5*60000, remainingMs=25*60000;
  bool running=false, awaiting=false;

  void click(uint32_t now) {
    // A completed phase waits for an explicit press before the next phase.
    running=!running; awaiting=false; last=now;
  }
  bool update(uint32_t now) {
    if(!running) return false;
    const uint32_t delta=now-last; last=now;
    if(delta<remainingMs) { remainingMs-=delta; return false; }
    const bool focusDone=phase==Phase::Focus;
    running=false; awaiting=true;
    phase=focusDone?Phase::Break:Phase::Focus;
    remainingMs=focusDone?breakMs:workMs;
    return focusDone;
  }
  void reset(uint32_t now) {
    phase=Phase::Focus; remainingMs=workMs; running=false; awaiting=false; last=now;
  }
  bool configure(unsigned workMinutes,unsigned breakMinutes,uint32_t now) {
    if(running || workMinutes<1 || workMinutes>180 || breakMinutes<1 || breakMinutes>60) return false;
    workMs=workMinutes*60000; breakMs=breakMinutes*60000; reset(now); return true;
  }
 private:
  uint32_t last=0;
};

struct Widget {
  char title[21]="WIDGET", value[21]="NO DATA", detail[21]="SEND JSON VIA BLE";
  bool received=false;
};

struct App {
  Mode mode=Mode::Snap;
  Pomodoro timer;
  Statistics stats;
  Widget widget;
  bool alarm=false;
  bool widgetPreview=false;
  Mode returnMode=Mode::Snap;
  uint32_t widgetShownAt=0;
  void selectMode(Mode selected) { widgetPreview=false;mode=selected; }
  void showWidget(uint32_t now,bool temporary) {
    if(!temporary) {selectMode(Mode::Widget);return;}
    if(mode!=Mode::Widget) {returnMode=mode;widgetPreview=true;}
    if(widgetPreview) widgetShownAt=now;
    mode=Mode::Widget;
  }
  void cancelWidgetPreview() {widgetPreview=false;}
  void navigate(int direction) {
    cancelWidgetPreview();
    constexpr int count=int(Mode::Count);
    mode=Mode((int(mode)+direction+count)%count);
  }
  bool update(uint32_t now,uint32_t day) {
    if(widgetPreview && uint32_t(now-widgetShownAt)>=8000) selectMode(returnMode);
    if(timer.update(now)) {
      stats.record(day,timer.workMs/1000); alarm=true; return true;
    }
    return false;
  }
  void action(uint32_t now) {
    cancelWidgetPreview();
    if(alarm) { mode=Mode::Pomodoro;alarm=false;return; }
    if(mode==Mode::Pomodoro) timer.click(now);
  }
};
}
