#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_ST7789.h>

constexpr int sck = 5, mosi = 4, cs = 7, dc = 20, rst = 21;
constexpr uint32_t initHz = 1000000;
constexpr int sizePx = 240;

class TimingDisplay : public Adafruit_ST7789 {
 public:
  TimingDisplay() : Adafruit_ST7789(&SPI, cs, dc, rst) {}

  void useMode3(uint32_t hz) {
    hwspi.settings = SPISettings(hz, MSBFIRST, SPI_MODE3);
  }

 protected:
  void begin(uint32_t) override { Adafruit_ST77xx::begin(initHz); }
};

TimingDisplay display;

void runTrial(const char* label, uint8_t initMode, uint32_t drawHz) {
  display.init(sizePx, sizePx, initMode);
  display.setRotation(0);
  display.invertDisplay(true);
  display.useMode3(drawHz);

  const uint16_t colors[] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE};
  uint16_t row[sizePx];
  for (int x = 0; x < sizePx; ++x) row[x] = colors[x / 60];
  const uint32_t started = micros();
  display.startWrite();
  display.setAddrWindow(0, 0, sizePx, sizePx);
  for (int y = 0; y < sizePx; ++y) display.writePixels(row, sizePx);
  display.endWrite();
  const uint32_t elapsed = micros() - started;

  display.fillRect(0, 92, sizePx, 56, ST77XX_BLACK);
  display.setCursor(10, 108);
  display.setTextSize(2);
  display.setTextColor(ST77XX_WHITE);
  display.print(label);
  Serial.printf("[timing] %s frame_us=%lu\n", label, (unsigned long)elapsed);
  delay(6000);
}

void setup() {
  Serial.begin(115200);
  SPI.begin(sck, -1, mosi, cs);
}

void loop() {
  runTrial("A  M3 / 1 MHz", SPI_MODE3, initHz);
  runTrial("B  M0 / 1 MHz", SPI_MODE0, initHz);
  runTrial("C  M3 / 8 MHz", SPI_MODE3, 8000000);
}
