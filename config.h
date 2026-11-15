#pragma once

#include <stdint.h>

namespace config {
constexpr int tftSck = 5;
constexpr int tftMosi = 4;
constexpr int tftCs = 7;
constexpr int tftDc = 20;
constexpr int tftRst = 21;
constexpr int buttonBack = 0;
constexpr int buttonNext = 1;
constexpr int buttonAction = 2;
constexpr uint16_t screenWidth = 240;
constexpr uint16_t screenHeight = 240;
constexpr uint16_t canvasWidth = 120;
constexpr uint16_t canvasHeight = 120;
constexpr const char* timezone = "GMT0BST,M3.5.0/1,M10.5.0/2";
constexpr uint32_t tftInitHz = 1000000;
constexpr uint32_t tftDrawHz = 80000000;
constexpr uint32_t debounceMs = 30;
constexpr uint32_t introDurationMs = 6000;
constexpr uint32_t introFrameMs = 1000 / 15;
enum class IntroEffect : uint8_t { Scramble, Count };
constexpr IntroEffect bootIntro = IntroEffect::Scramble;
constexpr bool demoMode = false;
// Physical panel stays at rotation 0; output transform handles prism optics.
// Logical image: horizontal mirror, then 90 degrees counterclockwise.
constexpr bool mirrorForPrism = true;
// Raise image exposure for viewing the reflection; white is already full scale.
// Neutral output: gain=1, gamma=1. BLK has no PWM connection.
constexpr float imageGain = 1.7f;
constexpr float imageGamma = 0.7f;
}  // namespace config
