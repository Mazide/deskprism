#include <cassert>
#include <cstring>
#include "../status_display.h"
void expected(char* out,const char* label) {memset(out,' ',20);out[20]=0;memcpy(out+(20-strlen(label))/2,label,strlen(label));}
int main() {
 status::Display display; char line[21], want[21];
 display.begin(1000);display.line(1000,line);expected(want,"LOADING");assert(!strcmp(line,want));
 display.show(status::State::WifiConnecting,1250);
 display.line(6000,line);expected(want,"WIFI CONNECTING");assert(!strcmp(line,want));
 display.show(status::State::WifiConnecting,6100);display.line(6200,line);assert(!strcmp(line,want));
 display.show(status::State::WifiFailed,6250);
 display.line(6400,line);char snapshot[21];memcpy(snapshot,line,21);
 display.show(status::State::Ready,6400);display.line(6400,line);assert(!strcmp(line,snapshot));
 display.line(10000,line);expected(want,"READY");assert(!strcmp(line,want));
 display.line(100000,line);assert(!strcmp(line,want));
 display.show(status::State::Loading,100100);
 bool changed=false;char prev[21];display.line(104000,prev);
 for(unsigned t=104001;t<110000;t+=17){display.line(t,line);assert(strlen(line)==20);if(strcmp(line,prev))changed=true;}
 assert(changed);
 display.begin(0xfffffff0u);display.show(status::State::WifiConnected,0xfffffff5u);
 display.line(5000,line);expected(want,"WIFI CONNECTED");assert(!strcmp(line,want));
 scramble::Animation generic;generic.begin();generic.setText("1234567890123456789012345",100);generic.line(10000,line);assert(!strcmp(line,"12345678901234567890"));
 generic.setText(nullptr,11000);generic.line(20000,line);expected(want,"");assert(!strcmp(line,want));
}
