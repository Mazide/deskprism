#pragma once
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include "command_frame.h"

namespace ble {
constexpr const char* serviceUuid="6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* rxUuid="6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr const char* txUuid="6e400003-b5a3-f393-e0a9-e50e24dcca9e";
class Transport {
 public:
  Transport():serverCallbacks(*this),rxCallbacks(*this) {}
  bool begin(QueueHandle_t queue);
  bool open(uint32_t now);
  void update(uint32_t now);
  bool connected() const {return authenticated.load();}
  bool windowOpen(uint32_t now) const {return advertisingWindow && uint32_t(now-openedAt)<120000;}
  uint32_t passkey() const {return pairingCode.load();}
  void reply(const char* json) {if(tx) tx->setValue(json);}
  bool takeError() {return rxError.exchange(false);}
 private:
  struct ServerCallbacks: public NimBLEServerCallbacks {
    Transport& owner;explicit ServerCallbacks(Transport& t):owner(t) {}
    void onConnect(NimBLEServer* server,NimBLEConnInfo& info) override;
    void onDisconnect(NimBLEServer*,NimBLEConnInfo&,int) override;
    uint32_t onPassKeyDisplay() override {return owner.pairingCode.load();}
    void onAuthenticationComplete(NimBLEConnInfo& info) override;
  } serverCallbacks;
  struct RxCallbacks: public NimBLECharacteristicCallbacks {
    Transport& owner;explicit RxCallbacks(Transport& t):owner(t) {}
    void onWrite(NimBLECharacteristic* characteristic,NimBLEConnInfo& info) override;
  } rxCallbacks;
  QueueHandle_t commands=nullptr;
  NimBLEServer* server=nullptr;
  NimBLECharacteristic* tx=nullptr;
  protocol::LineBuffer frame;
  std::atomic<bool> link{false},authenticated{false},rxError{false};
  std::atomic<uint32_t> pairingCode{0};
  std::atomic<uint16_t> activeHandle{BLE_HS_CONN_HANDLE_NONE};
  bool advertisingWindow=false;
  uint32_t openedAt=0;
};
}
