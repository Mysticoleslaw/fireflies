#pragma once
#include <Arduino.h>
#include <stdint.h>

#include "protocol.h"

// Wi-Fi side: the boot scan that picks which channels to listen on, and the master's
// home Wi-Fi / hotspot, mDNS (fireflies.local), captive DNS, OTA and recovery.
namespace net {

constexpr uint8_t kMaxChannels = 13;

struct ScanResult {
  bool homeFound;
  uint8_t homeChannel;              // strongest home access point (if found)
  uint8_t channels[kMaxChannels];   // where a master might be, most likely first
  uint8_t count;
};

// Starts an async scan for the home Wi-Fi (finishes immediately if none is configured).
void beginScan();
// True once scanning is done (retries a few times before giving up on home Wi-Fi).
bool scanResult(ScanResult& out);

// Master only: join home Wi-Fi (falls back to the hotspot) or start the hotspot.
void startMaster(uint8_t channel, bool homeFound, uint32_t now);
// Returns true when the master should restart: home Wi-Fi came back while on the hotspot,
// home Wi-Fi was lost for a while, or a new network was saved from the app.
bool loop(uint32_t now);
ff::NetMode mode();

struct WifiInfo {
  ff::NetMode mode;
  String ssid;     // network the master is on right now (empty on the hotspot)
  String saved;    // network saved from the app (empty if none)
  String builtIn;  // network from secrets.h, unless deleted in the app (empty if none)
  bool offline;    // disconnected from Wi-Fi on purpose
};
WifiInfo wifiInfo();
// First known network (saved from the app, else secrets.h), for followers' update mode.
bool homeCredentials(String& ssid, String& password);
// Saves a network entered in the app and restarts the master to join it.
// Returns an error message, or nullptr on success.
const char* setWifi(const String& ssid, const String& password);
// Deletes a known network (empty ssid = the saved one) and restarts. Error or nullptr.
const char* forgetWifi(const String& ssid);
// Disconnect from Wi-Fi (stay on the hotspot) or reconnect, then restart. Error or nullptr.
const char* setOffline(bool offline);

}  // namespace net
