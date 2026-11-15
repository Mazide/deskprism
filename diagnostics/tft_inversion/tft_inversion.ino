#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_ST7789.h>

constexpr int sck = 5, mosi = 4, cs = 7, dc = 20, rst = 21;
constexpr uint32_t spiHz = 1000000;

class InversionDisplay : public Adafruit_ST7789 {
 public:
  InversionDisplay() : Adafruit_ST7789(&SPI, cs, dc, rst) {}

  void selectMode(uint8_t mode) {
    hwspi.settings = SPISettings(spiHz, MSBFIRST, mode);
  }

 protected:
  void begin(uint32_t) override { Adafruit_ST77xx::begin(spiHz); }
};

InversionDisplay display;

char stage = 'B';

void showBaseline() {
  display.init(240, 240, SPI_MODE3);
  display.setSPISpeed(spiHz);
  display.setRotation(0);
  display.invertDisplay(true);
  const uint16_t colors[] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE};
  for (int i = 0; i < 4; ++i) display.fillRect(60 * i, 0, 60, 240, colors[i]);
  display.fillRect(0, 94, 240, 52, ST77XX_BLACK);
  display.setTextColor(ST77XX_WHITE);
  display.setTextSize(3);
  display.setCursor(12, 108);
  display.print("3  SLOW M3");
  stage = 'B';
  Serial.println("[inversion] baseline sent, holding");
}

void setup() {
  Serial.begin(115200);
  SPI.begin(sck, -1, mosi, cs);
  showBaseline();
}

void loop() {
  // Hold each image until the observer is ready. B restores the reference
  // between trials, ? reports progress without sending anything to the TFT.
  if (Serial.available()) {
    const int command = Serial.read();
    if (command == 'B') {
      showBaseline();
    } else if (command == '?') {
      Serial.printf("[inversion] stage=%c\n", stage);
    } else if (command == '0' || command == '3') {
      if (stage == 'B') {
        display.selectMode(command == '0' ? SPI_MODE0 : SPI_MODE3);
        display.invertDisplay(false);
        stage = command;
        Serial.printf("[inversion] M%c INVOFF sent, holding\n", stage);
      } else {
        Serial.println("[inversion] restore baseline with B first");
      }
    }
  }
  delay(5);
}
