#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_ST7789.h>
#include <driver/spi_master.h>
#include "doom_fire.h"

constexpr int sck = 5, mosi = 4, cs = 7, dc = 20, rst = 21, action = 2;
constexpr int side = doomfire::Animation::outputSize;
constexpr int stripRows = 60;
static_assert(side == 3 * doomfire::Animation::size && side % stripRows == 0 && stripRows % 3 == 0);
constexpr int speedsMHz[] = {20, 40, 80};
constexpr unsigned speedCount = sizeof(speedsMHz) / sizeof(speedsMHz[0]);
constexpr uint32_t intervalUs = 8000000;

class FirePanel : public Adafruit_ST7789 {
 public:
  FirePanel() : Adafruit_ST7789(&SPI, cs, dc, rst) {}
 protected:
  void begin(uint32_t) override { Adafruit_ST77xx::begin(1000000); }
};

FirePanel panel;
doomfire::Animation fire;
DMA_ATTR uint16_t strips[2][stripRows * side];
spi_transaction_t transfers[2] = {};
spi_device_handle_t device = nullptr;
GFXcanvas16 caption(136, 32);
unsigned speedIndex = speedCount - 1;  // Start at 80 MHz; ACTION still allows manual comparison.
int clockKHz = 0;
uint32_t sampleStart, sampleFrames;

void setCaption(float fps) {
  caption.fillScreen(ST77XX_BLACK);
  caption.setCursor(2, 2);
  caption.setTextColor(ST77XX_WHITE);
  caption.setTextSize(2);
  caption.println("DOOM FIRE");
  caption.setTextSize(1);
  caption.setCursor(2, 22);
  caption.print(device ? "DMA " : "SPI ");
  caption.print(clockKHz / 1000);
  caption.print(" MHz   ");
  if (fps > 0) caption.print(fps, 1);
  else caption.print("--");
  caption.print(" FPS");
  // Black and white use identical RGB565 byte order on the wire.
}

void selectSpeed() {
  if (device) ESP_ERROR_CHECK(spi_bus_remove_device(device));
  spi_device_interface_config_t config = {};
  config.mode = 3;
  config.clock_speed_hz = speedsMHz[speedIndex] * 1000000;
  config.spics_io_num = -1; // Hold CS manually across command and DMA strips.
  config.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;
  config.queue_size = 2;
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &config, &device));
  ESP_ERROR_CHECK(spi_device_get_actual_freq(device, &clockKHz));
  setCaption(0);
  sampleFrames = 0;
  sampleStart = micros();
}

void renderStrip(int yStart, uint16_t* pixels) {
  for (int y = 0; y < stripRows; y += 3) {
    uint16_t* row = pixels + y * side;
    for (int x = 0; x < side; x += 3) {
      // Transpose the logical flame for the prism and expand one cell to 3x3.
      const uint16_t color = __builtin_bswap16(fire.pixelAt((yStart + y) / 3, x / 3));
      row[x] = row[x + 1] = row[x + 2] = color;
    }
    memcpy(row + side, row, side * sizeof(uint16_t));
    memcpy(row + 2 * side, row, side * sizeof(uint16_t));
  }
  if (yStart == 0) {
    for (int y = 0; y < caption.height(); ++y) {
      memcpy(pixels + y * side, caption.getBuffer() + y * caption.width(),
             caption.width() * sizeof(uint16_t));
    }
  }
}

void waitForStrip() {
  spi_transaction_t* complete;
  ESP_ERROR_CHECK(spi_device_get_trans_result(device, &complete, portMAX_DELAY));
}

void drawFrame() {
  ESP_ERROR_CHECK(spi_device_acquire_bus(device, portMAX_DELAY));
  digitalWrite(dc, LOW);
  digitalWrite(cs, LOW);
  spi_transaction_t command = {};
  command.flags = SPI_TRANS_USE_TXDATA;
  command.length = 8;
  command.tx_data[0] = ST77XX_RAMWR;
  ESP_ERROR_CHECK(spi_device_polling_transmit(device, &command));
  digitalWrite(dc, HIGH);
  for (int i = 0; i < side / stripRows; ++i) {
    if (i >= 2) waitForStrip();
    const int slot = i % 2;
    // The first strip is prepared while the preceding frame is on the wire.
    if (i != 0) renderStrip(i * stripRows, strips[slot]);
    ESP_ERROR_CHECK(spi_device_queue_trans(device, &transfers[slot], portMAX_DELAY));
  }
  // All outgoing pixels now live in the DMA buffers. It is safe to advance
  // the heat field, then refill slot 0 once its penultimate transfer finishes.
  fire.advanceFrame();
  waitForStrip();
  renderStrip(0, strips[0]);
  waitForStrip();
  digitalWrite(cs, HIGH);
  spi_device_release_bus(device);
}

void startDma() {
  panel.startWrite();
  panel.setAddrWindow(0, 0, side, side);
  panel.endWrite();
  SPI.end();

  spi_bus_config_t bus = {};
  bus.mosi_io_num = mosi;
  bus.sclk_io_num = sck;
  bus.miso_io_num = bus.quadwp_io_num = bus.quadhd_io_num = -1;
  bus.max_transfer_sz = sizeof(strips[0]);
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
  for (int i = 0; i < 2; ++i) {
    transfers[i].length = sizeof(strips[i]) * 8;
    transfers[i].tx_buffer = strips[i];
  }
  selectSpeed();
  fire.advanceFrame();
  renderStrip(0, strips[0]);
}

void setup() {
  Serial.begin(115200);
  pinMode(action, INPUT_PULLUP);
  SPI.begin(sck, -1, mosi, cs);
  panel.init(side, side, SPI_MODE3);
  panel.setRotation(0);
  panel.invertDisplay(true);
  panel.setSPISpeed(20000000);
  clockKHz = 20000;
  setCaption(0);
  fire.begin(1);
  drawReferenceFrame();
  selectArduinoSpeed();
}

void selectArduinoSpeed() {
  clockKHz = speedsMHz[speedIndex] * 1000;
  panel.setSPISpeed(clockKHz * 1000);
  setCaption(0);
  sampleStart = micros();
  sampleFrames = 0;
}

void drawReferenceFrame() {
  fire.advanceFrame();
  panel.startWrite();
  panel.setAddrWindow(0, 0, side, side);
  for (int y = 0; y < side; y += stripRows) {
    renderStrip(y, strips[0]);
    panel.writePixels(strips[0], stripRows * side, true, true);
  }
  panel.endWrite();
}

void loop() {
  if (device) drawFrame();
  else drawReferenceFrame();
  ++sampleFrames;
  const uint32_t now = micros();
  const uint32_t elapsed = now - sampleStart;
  if (elapsed >= intervalUs) {
    const float fps = sampleFrames * 1000000.0f / elapsed;
    Serial.printf("[doom] path=%s spi_khz=%d frames=%lu elapsed_us=%lu fps=%.2f\n",
                  device ? "dma" : "spi", clockKHz, (unsigned long)sampleFrames, (unsigned long)elapsed, fps);
    setCaption(fps);
    sampleFrames = 0;
    sampleStart = now;
  } else if (sampleFrames % 16 == 0) {
    setCaption(sampleFrames * 1000000.0f / elapsed);
  }
  static bool wasPressed = false;
  static uint32_t lastPress = 0;
  const bool pressed = digitalRead(action) == LOW;
  const int command = Serial.available() ? Serial.read() : -1;
  if (command == 'D' && !device) {
    startDma();
  } else if ((pressed && !wasPressed && millis() - lastPress >= 200) ||
             command == '2' || command == '4' || command == '8') {
    lastPress = millis();
    if (command == '2') speedIndex = 0;
    else if (command == '4') speedIndex = 1;
    else if (command == '8') speedIndex = 2;
    else speedIndex = (speedIndex + 1) % speedCount;
    if (device) selectSpeed();
    else selectArduinoSpeed();
  }
  wasPressed = pressed;
}
