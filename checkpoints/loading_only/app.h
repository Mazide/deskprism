#pragma once

#include <stdint.h>

namespace desk {
enum class Mode : uint8_t { Clock, Pomodoro, Stopwatch, Snap, Breathe, Count };

struct App {
  Mode mode = Mode::Clock;
  bool needsRedraw = true;

  void navigate(int direction) {
    const int count = static_cast<int>(Mode::Count);
    mode = static_cast<Mode>((static_cast<int>(mode) + direction + count) % count);
    needsRedraw = true;
  }

  void action() {
   
  }

  void update(uint32_t now) {
    (void)now;  // TODO: nonblocking timers and animation updates.
  }
};
}  // namespace desk
