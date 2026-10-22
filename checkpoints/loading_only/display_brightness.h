#pragma once

#include <math.h>
#include <stdint.h>

// Image exposure only: BLK is wired to 3V3, outside software control.
// Small lookup tables avoid floating point work in the SPI output loop.
namespace optics {
class Brightness {
 public:
  void begin(float gain, float gamma) {
    for (unsigned i = 0; i < 32; ++i) redBlue[i] = level(i, 31, gain, gamma);
    for (unsigned i = 0; i < 64; ++i) green[i] = level(i, 63, gain, gamma);
  }

  uint16_t apply(uint16_t color) const {
    return (uint16_t(redBlue[color >> 11]) << 11) |
           (uint16_t(green[(color >> 5) & 63]) << 5) | redBlue[color & 31];
  }

 private:
  uint8_t redBlue[32] = {};
  uint8_t green[64] = {};
  static uint8_t level(unsigned value, unsigned maximum, float gain, float gamma) {
    const float raised = gain * powf(float(value) / maximum, gamma);
    return uint8_t(fminf(1.0f, raised) * maximum + 0.5f);
  }
};
}
