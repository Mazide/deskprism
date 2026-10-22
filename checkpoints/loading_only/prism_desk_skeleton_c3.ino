#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <esp_heap_caps.h>
#include "config.h"
#include "app.h"
#include "scramble.h"
#include "intro_storage.h"
#include "frame_canvas.h"
#include "display_transform.h"
#include "display_brightness.h"

Adafruit_ST7789 display(&SPI, config::tftCs, config::tftDc, config::tftRst);
FrameCanvas canvas;
optics::Brightness brightness;
desk::App app;
IntroStorage effects;
bool introActive = true;
bool introFrameReported = false;
config::IntroEffect currentIntro = config::bootIntro;
uint32_t introStartedAt = 0;
uint32_t lastIntroFrame = 0;

struct Button {
  int pin;
  bool rawPressed = false;
  bool stablePressed = false;
  uint32_t changedAt = 0;

  void begin() {
    pinMode(pin, INPUT_PULLUP);
    rawPressed = stablePressed = digitalRead(pin) == LOW;
    changedAt = millis();
  }

  bool poll(uint32_t now) {
    const bool pressed = digitalRead(pin) == LOW;
    if (pressed != rawPressed) {
      rawPressed = pressed;
      changedAt = now;
    }
    if (rawPressed != stablePressed && uint32_t(now - changedAt) >= config::debounceMs) {
      stablePressed = rawPressed;
      return stablePressed;  // One event per press, without auto-repeat.
    }
    return false;
  }
};

Button buttons[] = {{config::buttonBack}, {config::buttonNext}, {config::buttonAction}};

void presentCanvas() {
  const uint16_t* source = canvas.getBuffer();
  uint16_t line[config::screenWidth];
  display.startWrite();
  display.setAddrWindow(0, 0, config::screenWidth, config::screenHeight);
  for (int y = 0; y < config::screenHeight; ++y) {
    for (int x = 0; x < config::screenWidth; ++x) {
      line[x] = brightness.apply(source[optics::sourceIndex(x, y, config::screenWidth, config::mirrorForPrism)]);
    }
    display.writePixels(line, config::screenWidth);
  }
  display.endWrite();
}

void drawPlaceholder() {
  static const char* const names[] = {"CLOCK", "POMODORO", "STOPWATCH", "SNAP", "BREATHE"};
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setTextColor(ST77XX_WHITE);
  canvas.setTextSize(1);
  canvas.setCursor(16, 24);
  canvas.print("PRISM DESK / SKELETON");
  canvas.setTextSize(2);
  canvas.setCursor(16, 100);
  canvas.print(names[static_cast<uint8_t>(app.mode)]);
  canvas.setTextSize(1);
  canvas.setCursor(16, 132);
  canvas.print("Not implemented yet");
  canvas.setCursor(16, 216);
  canvas.print("< BACK   NEXT >   ACTION");
}

void startIntro(config::IntroEffect effect) {
  currentIntro = effect;
  canvas.fillScreen(ST77XX_BLACK);
  effects.scramble.begin(millis());
  introFrameReported = false;
  introStartedAt = millis();
  lastIntroFrame = introStartedAt - config::introFrameMs;
}

void advanceIntro(int direction) {
  const int count = static_cast<int>(config::IntroEffect::Count);
  const int effect = (static_cast<int>(currentIntro) + direction + count) % count;
  startIntro(static_cast<config::IntroEffect>(effect));
}

void setup() {
  Serial.begin(115200);
  for (auto& button : buttons) button.begin();
  brightness.begin(config::imageGain, config::imageGamma);
  SPI.begin(config::tftSck, -1, config::tftMosi, config::tftCs);
  display.init(config::screenWidth, config::screenHeight, SPI_MODE0);
  display.setSPISpeed(config::spiHz);
  display.setRotation(0);
  display.invertDisplay(true);
  display.setTextWrap(false);
  display.fillScreen(ST77XX_BLACK);
  if (!canvas.getBuffer()) {
    // Report an allocation failure visibly instead of dereferencing nullptr.
    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);
    display.setCursor(12, 100);
    display.print("NO FRAMEBUFFER");
    while (true) delay(100);
  }
  canvas.fillScreen(ST77XX_BLACK);
  canvas.setTextWrap(false);
  startIntro(config::bootIntro);
}

void loop() {
  uint32_t now = millis();
  const bool back = buttons[0].poll(now);
  const bool next = buttons[1].poll(now);
  const bool action = buttons[2].poll(now);
  if (config::demoMode) {
    if (back) advanceIntro(-1);
    else if (next) advanceIntro(1);
    else if (action) startIntro(currentIntro);
    // Scramble loops on its own; do not interrupt it with a 6-second restart.
    // Initializing an effect can take time; refresh after its timer is reset.
    now = millis();
  }
  if (introActive) {
    const uint32_t elapsed = now - introStartedAt;
    if (!config::demoMode && (back || next || action || elapsed >= config::introDurationMs)) {
      introActive = false;
      app.needsRedraw = true;
    } else {
      if (uint32_t(now - lastIntroFrame) >= config::introFrameMs) {
        lastIntroFrame = now;
        char line[scramble::Animation::cols + 1];
        effects.scramble.line(now, line);
        canvas.fillScreen(ST77XX_BLACK);
        canvas.setTextColor(ST77XX_WHITE);
        canvas.setTextSize(2);
        canvas.setCursor(effects.scramble.textOffset(), 112);
        canvas.print(line);
        presentCanvas();
        if (!introFrameReported && Serial.availableForWrite() >= 96) {
          Serial.printf("[demo] effect=%u frame=ok heap=%u largest=%u\n",
                        unsigned(currentIntro), unsigned(ESP.getFreeHeap()),
                        unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
          introFrameReported = true;
        }
      }
      delay(1);
      return;
    }
    // The press that skips the intro is consumed here.
  } else {
    if (back) app.navigate(-1);
    if (next) app.navigate(1);
    if (action) app.action();
  }
  app.update(now);
  if (app.needsRedraw) {
    drawPlaceholder();
    presentCanvas();
    app.needsRedraw = false;
  }
  delay(1);
}
