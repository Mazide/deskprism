#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_ST7789.h>

constexpr int sck = 5, mosi = 4, cs = 7, dc = 20, rst = 21;
constexpr int width = 240, height = 240, headerHeight = 28;
constexpr uint32_t phaseMs = 10000;

class FpsDisplay : public Adafruit_ST7789 {
 public:
  FpsDisplay() : Adafruit_ST7789(&SPI, cs, dc, rst) {}

 protected:
  void begin(uint32_t) override { Adafruit_ST77xx::begin(1000000); }
};

FpsDisplay display;
GFXcanvas16 header(width, headerHeight);
uint16_t checker[2][width];
const uint16_t bars[] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_YELLOW};

void setHeader(unsigned mhz, float fps) {
  header.fillScreen(ST77XX_BLACK);
  header.setCursor(4, 6);
  header.setTextColor(ST77XX_WHITE);
  header.setTextSize(2);
  header.print(mhz);
  header.print(" MHz   ");
  if (fps > 0) header.print(fps, 1);
  else header.print("--");
  header.print(" FPS");
}

void drawFrame(unsigned markerX) {
  uint16_t band[width];
  for (int x = 0; x < width; ++x) {
    band[x] = (unsigned(x) + width - markerX) % width < 6
                ? ST77XX_WHITE : bars[x / 60];
  }
  display.startWrite();
  display.setAddrWindow(0, 0, width, height);
  for (int y = 0; y < headerHeight; ++y) {
    display.writePixels(header.getBuffer() + y * width, width);
  }
  for (int y = headerHeight; y < height - 48; ++y) display.writePixels(band, width);
  for (int y = height - 48; y < height; ++y) display.writePixels(checker[(y / 4) % 2], width);
  display.endWrite();
}

void benchmark(unsigned mhz) {
  display.setSPISpeed(mhz * 1000000UL);
  setHeader(mhz, 0);
  const uint32_t start = micros();
  uint32_t sampleStart = start;
  unsigned totalFrames = 0, sampleFrames = 0;
  uint32_t elapsed;
  do {
    const unsigned markerX = ((micros() - start) / 10000) % width;
    drawFrame(markerX);
    ++totalFrames;
    ++sampleFrames;
    delay(1);
    const uint32_t now = micros();
    elapsed = now - start;
    if (now - sampleStart >= 1000000) {
      setHeader(mhz, sampleFrames * 1000000.0f / (now - sampleStart));
      sampleStart = now;
      sampleFrames = 0;
    }
  } while (elapsed < phaseMs * 1000);
  Serial.printf("[fps] %u MHz frames=%u elapsed_us=%lu fps=%.2f\n",
                mhz, totalFrames, (unsigned long)elapsed, totalFrames * 1000000.0 / elapsed);
}

void setup() {
  Serial.begin(115200);
  SPI.begin(sck, -1, mosi, cs);
  display.init(width, height, SPI_MODE3);
  display.setRotation(0);
  display.invertDisplay(true);
  for (int x = 0; x < width; ++x) {
    checker[0][x] = (x / 4) % 2 ? ST77XX_WHITE : ST77XX_BLACK;
    checker[1][x] = ~checker[0][x];
  }
}

void loop() {
  benchmark(16);
  benchmark(20);
}
