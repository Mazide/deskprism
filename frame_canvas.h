#pragma once

#include <Adafruit_GFX.h>
#include "config.h"

// Reserve the contiguous buffer at link time, not from a fragmented
// startup heap. The base class must not allocate or free this storage.
class FrameCanvas : public GFXcanvas16 {
 public:
  FrameCanvas() : GFXcanvas16(config::canvasWidth, config::canvasHeight, false) {
    buffer = pixels;
  }
 private:
  uint16_t pixels[config::canvasWidth * config::canvasHeight] = {};
};
