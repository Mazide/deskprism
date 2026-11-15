#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_ST7789.h>

constexpr int sck = 5, mosi = 4, cs = 7, dc = 20, rst = 21;
constexpr uint32_t holdMs = 6000;

class ProbeDisplay : public Adafruit_ST7789 {
 public:
  ProbeDisplay() : Adafruit_ST7789(&SPI, cs, dc, rst) {}
  uint32_t initHz = 0;

 protected:
  // init() calls begin() before sending the ST7789 setup commands.
  void begin(uint32_t) override { Adafruit_ST77xx::begin(initHz); }
};

struct Profile {
  const char* label;
  uint8_t mode;
  uint32_t initHz;
  uint32_t drawHz;
};

const Profile profiles[] = {
  {"1  NORMAL", SPI_MODE0, 0, 20000000}, // Original mode and library default init.
  {"2  SLOW M0", SPI_MODE0, 1000000, 1000000},
  {"3  SLOW M3", SPI_MODE3, 1000000, 1000000},
};
const unsigned profileCount = sizeof(profiles) / sizeof(profiles[0]);
ProbeDisplay display;
unsigned current = 0;
uint32_t drawnAt = 0;

void drawProfile() {
  const Profile& profile = profiles[current];
  Serial.printf("[tft_probe] init %s\n", profile.label);
  display.initHz = profile.initHz;
  display.init(240, 240, profile.mode);
  display.setSPISpeed(profile.drawHz);
  display.setRotation(0);
  display.invertDisplay(true);
  const uint16_t colors[] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE};
  for (int i = 0; i < 4; ++i) display.fillRect(60 * i, 0, 60, 240, colors[i]);
  display.fillRect(0, 94, 240, 52, ST77XX_BLACK);
  display.setTextColor(ST77XX_WHITE);
  display.setTextSize(3);
  display.setCursor(12, 108);
  display.print(profile.label);
  drawnAt = millis();
  Serial.printf("[tft_probe] pattern sent %s uptime_ms=%lu\n", profile.label,
                (unsigned long)drawnAt);
}

void setup() {
  Serial.begin(115200);
  SPI.begin(sck, -1, mosi, cs);
  drawProfile();
}

void loop() {
  if (millis() - drawnAt >= holdMs) {
    current = (current + 1) % profileCount;
    drawProfile();
  }
  delay(10);
}
