#pragma once
#include <Preferences.h>
#include "device_model.h"

class StatisticsStore {
 public:
  bool begin(desk::Statistics& stats) {
    opened=prefs.begin("prism-widgets",false);
    if(!opened) return false;
    const size_t length=prefs.getBytesLength("stats-v1");
    if(!length) return true;
    if(length!=sizeof(desk::StatsData)) return false;
    desk::StatsData stored;
    if(prefs.getBytes("stats-v1",&stored,sizeof(stored))!=sizeof(stored)) return false;
    if(stored.magic!=0x50525331 || stored.version!=1) return false;
    for(const auto& row:stored.days)
      if(row.day && (row.day<20240101 || row.day>21001231)) return false;
    stats.data=stored; stats.dirty=false; return true;
  }
  bool save(desk::Statistics& stats) {
    if(!stats.dirty) return true;
    if(!opened || prefs.putBytes("stats-v1",&stats.data,sizeof(stats.data))!=sizeof(stats.data)) return false;
    stats.dirty=false; return true;
  }
 private:
  Preferences prefs;
  bool opened=false;
};
