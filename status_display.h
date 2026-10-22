#pragma once

#include "scramble.h"

namespace status {
enum class State : uint8_t { Loading, WifiConnecting, WifiConnected, WifiFailed, TimeSync, Ready, WifiSetup, QuoteWaiting, QuoteFailed, MissingKey, BleReady, BleConnected, StatsSaved, StorageError };

// A display for actual state changes. No network operations or simulated events.
class Display {
 public:
  void begin(uint32_t now) {
    current = State::Loading;
    animation.begin(label(current), now, true);
  }
  void show(State next, uint32_t now) {
    if (next == current) return;  // Repeated polls must not restart decoding.
    current = next;
    animation.setText(label(next), now, next == State::Loading);
  }
  void line(uint32_t now, char (&text)[scramble::Animation::cols+1]) const {
    animation.line(now, text);
  }
  int textOffset(unsigned textSize=2) const { return strlen(label(current)) % 2 ? 3*textSize : 0; }

 private:
  State current = State::Loading;
  scramble::Animation animation;
  static const char* label(State state) {
    switch (state) {
      case State::Loading: return "LOADING";
      case State::WifiConnecting: return "WIFI CONNECTING";
      case State::WifiConnected: return "WIFI CONNECTED";
      case State::WifiFailed: return "WIFI FAILED";
      case State::TimeSync: return "SYNCING TIME";
      case State::Ready: return "READY";
      case State::WifiSetup: return "WIFI SETUP NEEDED";
      case State::QuoteWaiting: return "FETCHING SNAP";
      case State::QuoteFailed: return "SNAP UNAVAILABLE";
      case State::MissingKey: return "SNAP KEY NEEDED";
      case State::BleReady: return "BLE READY";
      case State::BleConnected: return "BLE CONNECTED";
      case State::StatsSaved: return "SESSION SAVED";
      case State::StorageError: return "STORAGE ERROR";
    }
    return "LOADING";
  }
};
}
