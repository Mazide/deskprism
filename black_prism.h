#pragma once

#include <stdint.h>
#include "black_prism_frames.h"

// Original Black Prism glyph/color frames, generated offline from upstream TS.
// See reference/, tools/ and THIRD_PARTY_NOTICES.txt. Playback needs no network.
namespace prism {
constexpr int width = 240, height = 120;
class Animation {
 public:
  uint16_t pixels[width * height] = {};
  void begin() {}

  void render(uint32_t elapsedMs) {
    unsigned frame = elapsedMs * 15 / 1000;
    if (frame >= frameCount) frame = frameCount - 1;
    const uint32_t end = pgm_read_dword(frameOffsets + frame + 1);
    uint32_t at = pgm_read_dword(frameOffsets + frame);
    unsigned pixel = 0;
    while (at + 2 < end && pixel < width * height) {
      const unsigned count = pgm_read_byte(frameData + at);
      const uint16_t color = pgm_read_byte(frameData + at + 1) |
                            (uint16_t(pgm_read_byte(frameData + at + 2)) << 8);
      at += 3;
      for (unsigned i = 0; i < count && pixel < width * height; ++i) pixels[pixel++] = color;
    }
  }
};
} // namespace prism
