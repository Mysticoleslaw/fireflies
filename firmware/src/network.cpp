#include "network.h"

#include <ArduinoOTA.h>
#include <DNSServer.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>

#include "config.h"
#include "storage.h"

namespace net {
namespace {

enum class State { Idle, Scanning, Scanned, Connecting, HomeWifi, Hotspot };

constexpr uint8_t kScanAttempts = 3;  // one scan often misses a weak access point
constexpr uint32_t kHomeRecheckMs = 5UL * 60 * 1000;
constexpr uint32_t kHomeLostMs = 2UL * 60 * 1000;

State state = State::Idle;
uint8_t scansLeft = 0;
ScanResult result{};
uint8_t homeBssid[6];
int32_t homeRssi = INT32_MIN;
uint32_t restartAt = 0;  // set after the app saves a new network

// Networks we may join: the one saved from the app first, then the one in secrets.h.
struct Known {
  String ssid;
  String password;
};
Known known[2];
uint8_t knownCount = 0;
int8_t homeIdx = -1;  // which known network the scan picked
uint8_t meshChannel = cfg::kDefaultChannel;
uint32_t connectStarted = 0;
uint32_t lastHomeCheck = 0;
uint32_t homeLostSince = 0;
bool recheckRunning = false;
DNSServer dns;

// FF_NO_HOME_WIFI (env:hotspot_test) ignores the network in secrets.h, to try hotspot mode
// (and Wi-Fi setup from the app) at home.
// Built-in network from secrets.h, unless the test build or the app removed it.
bool builtInAvailable(const storage::NetPrefs& prefs) {
#ifdef FF_NO_HOME_WIFI
  return false;
#else
  return strlen(FF_WIFI_SSID) > 0 && !prefs.ignoreBuiltIn;
#endif
}

void loadKnown() {
  knownCount = 0;
  storage::NetPrefs prefs = storage::loadNetPrefs();
  if (prefs.offline) return;  // "Disconnect from Wi-Fi": behave as if we know no networks
  storage::WifiCreds saved;
  if (storage::loadWifi(saved)) known[knownCount++] = Known{saved.ssid, saved.password};
  if (builtInAvailable(prefs)) known[knownCount++] = Known{FF_WIFI_SSID, FF_WIFI_PASSWORD};
}

void restartSoon(const __FlashStringHelper* why) {
  Serial.println(why);
  restartAt = millis() + 1500;  // let the HTTP response get out first
}

int knownIndex(const String& ssid) {
  for (uint8_t i = 0; i < knownCount; i++) {
    if (known[i].ssid == ssid) return i;
  }
  return -1;
}

bool homeConfigured() { return knownCount > 0; }

void addChannel(uint8_t ch) {
  for (uint8_t i = 0; i < result.count; i++) {
    if (result.channels[i] == ch) return;
  }
  if (result.count < kMaxChannels) result.channels[result.count++] = ch;
}

// Records every channel a known network is on (mesh routers use several). Picks the
// saved-from-app network over the built-in one, then the strongest access point.
bool collectHome(int count) {
  bool seen = false;
  for (int i = 0; i < count; i++) {
    int k = knownIndex(WiFi.SSID(i));
    if (k < 0) continue;
    seen = true;
    addChannel(WiFi.channel(i));
    bool better = homeIdx < 0 || k < homeIdx || (k == homeIdx && WiFi.RSSI(i) > homeRssi);
    if (!better) continue;
    homeIdx = k;
    homeRssi = WiFi.RSSI(i);
    result.homeFound = true;
    result.homeChannel = WiFi.channel(i);
    memcpy(homeBssid, WiFi.BSSID(i), 6);
  }
  return seen;
}

// What to listen on: home channel(s) first, then the hotspot default. If home Wi-Fi is
// configured but we can't see it (edge of range), the others may still be on it: try all.
void finishScan() {
  for (uint8_t i = 1; result.homeFound && i < result.count; i++) {
    if (result.channels[i] != result.homeChannel) continue;
    std::swap(result.channels[0], result.channels[i]);  // strongest first
    break;
  }
  addChannel(cfg::kDefaultChannel);
  if (homeConfigured() && !result.homeFound) {
    for (uint8_t ch = 1; ch <= kMaxChannels; ch++) addChannel(ch);
  }
  state = State::Scanned;
}

void startServices() {
  ArduinoOTA.setHostname(cfg::kHostname);
  ArduinoOTA.setPassword(FF_OTA_PASSWORD);
  ArduinoOTA.begin();  // also starts mDNS with our hostname
  MDNS.addService("http", "tcp", 80);
}

// The hotspot stays on the mesh channel so followers never have to move.
void startHotspot(uint32_t now) {
  WiFi.disconnect();
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(cfg::kHotspotSsid, FF_HOTSPOT_PASSWORD, meshChannel, 0, cfg::kHotspotMaxClients)) {
    Serial.println(F("[net] hotspot failed to start (password must be 8+ chars)"));
  }
  dns.start(53, "*", WiFi.softAPIP());  // any name -> us, so phones pop up the app
  state = State::Hotspot;
  lastHomeCheck = now;
  startServices();
  Serial.printf("[net] hotspot \"%s\" on channel %u, http://%s/\n", cfg::kHotspotSsid, meshChannel,
                WiFi.softAPIP().toString().c_str());
}

// On the hotspot: every few minutes, look for home Wi-Fi (e.g. the router booted after us).
bool homeCameBack(uint32_t now) {
  if (!homeConfigured()) return false;
  if (!recheckRunning) {
    if (now - lastHomeCheck < kHomeRecheckMs) return false;
    WiFi.scanNetworks(true, true);
    recheckRunning = true;
    return false;
  }
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return false;
  bool found = false;
  for (int i = 0; i < n; i++) found |= knownIndex(WiFi.SSID(i)) >= 0;
  WiFi.scanDelete();
  recheckRunning = false;
  lastHomeCheck = now;
  if (found) Serial.println(F("[net] home Wi-Fi is back, restarting to move the mesh onto it"));
  return found;
}

bool homeLost(uint32_t now) {
  if (WiFi.status() == WL_CONNECTED) {
    homeLostSince = 0;
    return false;
  }
  if (!homeLostSince) homeLostSince = now;
  if (now - homeLostSince < kHomeLostMs) return false;
  Serial.println(F("[net] home Wi-Fi lost, restarting"));
  return true;
}

}  // namespace

void beginScan() {
#ifdef FF_NO_HOME_WIFI
  Serial.println(F("[net] TEST BUILD: ignoring home Wi-Fi, hotspot mode only"));
#endif
  result = ScanResult{};
  homeRssi = INT32_MIN;
  homeIdx = -1;
  loadKnown();
  if (!homeConfigured()) return finishScan();
  scansLeft = kScanAttempts;
  WiFi.scanNetworks(true, true);  // async, include hidden networks
  state = State::Scanning;
}

bool scanResult(ScanResult& out) {
  if (state == State::Scanning) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return false;
    bool seen = collectHome(n);  // n < 0 (failed) collects nothing
    WiFi.scanDelete();
    if (!seen && --scansLeft > 0) {
      WiFi.scanNetworks(true, true);
      return false;
    }
    finishScan();
  }
  if (state != State::Scanned) return false;
  out = result;
  return true;
}

// On home Wi-Fi the radio's hidden mesh AP stays up next to the station connection (see
// radio.cpp); on the hotspot, the visible "Fireflies" AP takes its place.
void startMaster(uint8_t channel, bool home, uint32_t now) {
  meshChannel = channel;
  if (!home) return startHotspot(now);
  const Known& k = known[homeIdx];
  WiFi.begin(k.ssid.c_str(), k.password.c_str(), result.homeChannel, homeBssid);
  connectStarted = now;
  state = State::Connecting;
  Serial.printf("[net] joining \"%s\" on channel %u\n", k.ssid.c_str(), result.homeChannel);
}

bool loop(uint32_t now) {
  if (restartAt && (int32_t)(now - restartAt) >= 0) return true;
  if (state == State::Connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      state = State::HomeWifi;
      startServices();
      Serial.printf("[net] on home Wi-Fi: http://%s.local/ (%s)\n", cfg::kHostname,
                    WiFi.localIP().toString().c_str());
    } else if (now - connectStarted > cfg::kWifiConnectMs) {
      Serial.println(F("[net] couldn't join home Wi-Fi, starting hotspot"));
      startHotspot(now);
    }
    return false;
  }
  if (state != State::HomeWifi && state != State::Hotspot) return false;
  if (state == State::Hotspot) dns.processNextRequest();
  MDNS.update();
  ArduinoOTA.handle();
  return state == State::Hotspot ? homeCameBack(now) : homeLost(now);
}

bool homeCredentials(String& ssid, String& password) {
  loadKnown();
  if (knownCount == 0) return false;
  ssid = known[0].ssid;  // the network saved from the app wins, as for the master
  password = known[0].password;
  return true;
}

WifiInfo wifiInfo() {
  storage::NetPrefs prefs = storage::loadNetPrefs();
  WifiInfo info{mode(), "", "", "", prefs.offline != 0};
  if (state == State::HomeWifi) info.ssid = WiFi.SSID();
  storage::WifiCreds saved;
  if (storage::loadWifi(saved)) info.saved = saved.ssid;
  if (builtInAvailable(prefs)) info.builtIn = FF_WIFI_SSID;
  return info;
}

const char* setWifi(const String& ssid, const String& password) {
  if (ssid.isEmpty() || ssid.length() > 32) return "network name must be 1-32 characters";
  if (!password.isEmpty() && (password.length() < 8 || password.length() > 63)) {
    return "password must be 8-63 characters (or empty for an open network)";
  }
  storage::WifiCreds c{};
  ssid.toCharArray(c.ssid, sizeof c.ssid);
  password.toCharArray(c.password, sizeof c.password);
  if (!storage::saveWifi(c)) return "couldn't save on the bug";
  storage::saveNetPrefs(storage::NetPrefs{0, storage::loadNetPrefs().ignoreBuiltIn});  // joining implies online
  restartSoon(F("[net] saved Wi-Fi, restarting to join it"));
  return nullptr;
}

const char* forgetWifi(const String& ssid) {
  storage::WifiCreds saved;
  storage::NetPrefs prefs = storage::loadNetPrefs();
  if (storage::loadWifi(saved) && (ssid.isEmpty() || ssid == saved.ssid)) {
    storage::forgetWifi();
  } else if (builtInAvailable(prefs) && ssid == FF_WIFI_SSID) {
    prefs.ignoreBuiltIn = 1;
    if (!storage::saveNetPrefs(prefs)) return "couldn't save on the bug";
  } else {
    return "that network isn't saved";
  }
  restartSoon(F("[net] deleted a network, restarting"));
  return nullptr;
}

const char* setOffline(bool offline) {
  storage::NetPrefs prefs = storage::loadNetPrefs();
  prefs.offline = offline;
  if (!storage::saveNetPrefs(prefs)) return "couldn't save on the bug";
  restartSoon(offline ? F("[net] disconnecting from Wi-Fi, restarting on the hotspot")
                      : F("[net] reconnecting to Wi-Fi, restarting"));
  return nullptr;
}

ff::NetMode mode() {
  if (state == State::HomeWifi) return ff::NetMode::HomeWifi;
  if (state == State::Hotspot) return ff::NetMode::Hotspot;
  return ff::NetMode::None;
}

}  // namespace net
