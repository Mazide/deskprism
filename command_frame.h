#pragma once
#include <stddef.h>
#include <string.h>

namespace protocol {
constexpr size_t maxMessage=384;
enum class Feed { None, Message, TooLong };
class LineBuffer {
 public:
  Feed push(char c,char* output) {
    if(c=='\r') return Feed::None;
    if(c=='\n') {
      const bool tooLong=dropping;
      if(!tooLong && used) { memcpy(output,data,used);output[used]=0; }
      const bool message=used!=0;reset();
      return tooLong?Feed::TooLong:message?Feed::Message:Feed::None;
    }
    if(dropping) return Feed::None;
    if(used>=maxMessage-1 || c==0) {dropping=true;return Feed::None;}
    data[used++]=c;return Feed::None;
  }
  void reset() {used=0;dropping=false;}
 private:
  char data[maxMessage]={};size_t used=0;bool dropping=false;
};
struct Packet {char json[maxMessage]={};bool ble=false;};
}
