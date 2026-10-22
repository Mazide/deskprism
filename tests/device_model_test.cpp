#include <cassert>
#include <string>
#include "../device_model.h"
#include "../command_frame.h"
#include "../command_protocol.h"
int main() {
 desk::App preview;
 preview.showWidget(100,true);
 assert(preview.mode==desk::Mode::Widget && preview.widgetPreview);
 preview.update(8099,0);assert(preview.mode==desk::Mode::Widget);
 preview.update(8100,0);assert(preview.mode==desk::Mode::Snap);
 preview.selectMode(desk::Mode::Clock);preview.showWidget(10000,true);
 preview.showWidget(17000,true);preview.update(18000,0);
 assert(preview.mode==desk::Mode::Widget);
 preview.update(25000,0);assert(preview.mode==desk::Mode::Clock);
 preview.selectMode(desk::Mode::Widget);preview.showWidget(30000,true);
 preview.update(50000,0);assert(preview.mode==desk::Mode::Widget && !preview.widgetPreview);
 preview.selectMode(desk::Mode::Snap);preview.showWidget(60000,true);preview.navigate(1);
 const auto manual=preview.mode;preview.update(80000,0);assert(preview.mode==manual);
 preview.selectMode(desk::Mode::Clock);preview.showWidget(0xfffffff0u,true);
 preview.update(7983,0);assert(preview.mode==desk::Mode::Widget);
 preview.update(7984,0);assert(preview.mode==desk::Mode::Clock);
 desk::App app;
 app.timer.workMs=1000;app.timer.breakMs=500;app.timer.reset(0);
 app.mode=desk::Mode::Pomodoro;app.action(100);
 app.navigate(1);assert(!app.update(600,20261022));assert(app.timer.remainingMs==500);
 assert(app.update(1100,20261022));assert(app.stats.today(20261022).sessions==1);
 assert(!app.timer.running && app.timer.awaiting && app.timer.phase==desk::Phase::Break);
 assert(!app.update(30000,20261022));assert(app.stats.data.sessions==1);
 app.action(30000);assert(!app.alarm && !app.timer.running && app.mode==desk::Mode::Pomodoro);
 app.action(30001);assert(app.timer.running);
 assert(!app.update(30501,20261022));assert(app.timer.phase==desk::Phase::Focus && !app.timer.running);
 app.timer.click(30600);app.update(31000,20261022);app.timer.click(31000);
 assert(!app.timer.running && app.timer.remainingMs==600);
 app.update(99999,20261022);assert(app.timer.remainingMs==600);
 app.timer.click(100000);assert(app.update(100600,20261023));
 assert(app.stats.today(20261022).sessions==1 && app.stats.today(20261023).sessions==1);
 app.timer.reset(0xfffffff0u);app.timer.click(0xfffffff0u);
 assert(app.update(1000,0));assert(app.stats.data.undatedSessions==1 && app.stats.data.sessions==3);
 desk::Statistics stats;
 for(unsigned day=1;day<=9;++day) stats.record(20261000+day,1500);
 assert(stats.data.sessions==9 && stats.today(20261001).sessions==0 && stats.today(20261009).seconds==1500);
 assert(stats.today(0).sessions==0);
 protocol::LineBuffer frame;char output[protocol::maxMessage];
 for(char c:std::string("{\"v\":1}\n")) {auto result=frame.push(c,output);if(c=='\n')assert(result==protocol::Feed::Message);}
 assert(!strcmp(output,"{\"v\":1}"));
 for(unsigned i=0;i<500;++i)frame.push('x',output);
 assert(frame.push('\n',output)==protocol::Feed::TooLong);
 frame.push('x',output);assert(frame.push('\n',output)==protocol::Feed::Message && !strcmp(output,"x"));
 protocol::Command command;
 assert(!protocol::parse("{\"v\":1,\"id\":3,\"type\":\"widget\",\"title\":\"CPU\",\"value\":\"42%\"}",command));
 assert(command.kind==protocol::Kind::Widget && !strcmp(command.value,"42%") && command.id==3);
 assert(protocol::parse("{\"v\":1,\"type\":\"widget\",\"title\":\"A\\u0000B\",\"value\":\"42\"}",command));
 assert(protocol::parse("{\"v\":2,\"type\":\"get\"}",command));
 assert(protocol::parse("{\"v\":1,\"type\":\"time\",\"unix\":0}",command));
 assert(protocol::parse("{\"v\":1,\"type\":\"pomodoro\",\"action\":\"configure\",\"work_min\":0,\"break_min\":5}",command));
 assert(protocol::parse("{\"v\":1,\"type\":\"wifi\",\"ssid\":\"home\",\"password\":\"short\"}",command));
 assert(!protocol::parse("{\"v\":1,\"type\":\"wifi\",\"ssid\":\"home\",\"password\":\"\"}",command));
}
