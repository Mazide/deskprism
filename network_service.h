#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>

namespace network {
enum class State : uint8_t { Connecting, NoCredentials, Syncing, Ready, Failed, MissingKey, QuoteFailed, SetupPortal };
struct Snapshot {
  State state=State::Connecting;
  bool wifi=false, quoteValid=false;
  float price=0, change=0;
  uint32_t quoteTime=0, receivedAt=0;
  bool portal=false;
  char portalPassword[9]={};
};
class Service {
 public:
  bool begin();
  bool configure(const char* ssid,const char* password);
  void refresh() { refreshRequested=true; }
  void openPortal() { portalRequested=true; }
  bool poll(Snapshot& value);
 private:
  struct Credentials { char ssid[33], password[65]; };
  QueueHandle_t updates=nullptr, credentials=nullptr;
  std::atomic<bool> refreshRequested{false};
  std::atomic<bool> portalRequested{false};
  static void task(void* self);
  void run();
};
}
