#pragma once

#include "status_display.h"

// Active effect only; the framebuffer is reserved separately in FrameCanvas.
struct IntroStorage {
  status::Display scramble;
};
