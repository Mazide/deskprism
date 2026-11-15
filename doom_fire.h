#pragma once

#include <stdint.h>
#include <string.h>

// DOOM/PSX-style heat propagation: hot bottom row, random drift and cooling.
// Algorithm reference: https://fabiensanglard.net/doom_fire_psx/
// Independent implementation; generated flames, no game assets or recordings.
namespace doomfire {
class Animation {
 public:
  static constexpr int size = 80;
  static constexpr int outputSize = 240;

  void begin(uint32_t seed = 1) {
    randomState = seed ? seed : 1;
    memset(heat, 0, sizeof(heat));
    memset(heat + (size - 1) * size, 36, size);
    lastStep = 0;
    for (unsigned i = 0; i < 37; ++i) palette[i] = color(i);
    // Start with established flames rather than several seconds of black.
    for (unsigned i = 0; i < 90; ++i) step();
  }

  // For uncapped playback, advance the simulation once per completed frame.
  void advanceFrame() { step(); }
  uint16_t pixelAt(int x, int y) const { return palette[heat[y * size + x]]; }

  void render(uint32_t elapsedMs, uint16_t* output) {
    // 30 simulation ticks/s, independent of display refresh; bound catch-up.
    unsigned steps = (elapsedMs - lastStep) / 33;
    if (steps > 4) {
      steps = 4;
      lastStep = elapsedMs - steps * 33;
    }
    while (steps--) { step(); lastStep += 33; }
    for (int y = 0; y < outputSize; ++y)
      for (int x = 0; x < outputSize; ++x)
        output[y * outputSize + x] = pixelAt(x / 3, y / 3);
  }

 private:
  uint8_t heat[size * size] = {};
  uint16_t palette[37] = {};
  uint32_t randomState = 1, lastStep = 0;

  unsigned random() {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return randomState;
  }

  void step() {
    for (int y = 1; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        const unsigned drift = random() & 3;
        const int targetX = (x + 1 - int(drift) + size) % size;
        const unsigned value = heat[y * size + x];
        const unsigned decay = drift & 1;
        heat[(y - 1) * size + targetX] = value > decay ? value - decay : 0;
      }
    }
  }

  static uint16_t color(unsigned level) {
    // 37 heat levels: black -> deep red -> orange -> yellow -> white.
    static const uint8_t stops[5][3] = {
      {0,0,0}, {160,12,0}, {255,90,0}, {255,210,24}, {255,255,255}
    };
    const unsigned band = level == 36 ? 3 : level / 9;
    const unsigned mix = level - band * 9;
    unsigned rgb[3];
    for (unsigned c = 0; c < 3; ++c)
      rgb[c] = stops[band][c] + (stops[band+1][c] - stops[band][c]) * mix / 9;
    return ((rgb[0] >> 3) << 11) | ((rgb[1] >> 2) << 5) | (rgb[2] >> 3);
  }
};
}
