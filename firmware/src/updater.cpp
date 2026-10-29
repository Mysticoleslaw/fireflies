#include "updater.h"

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>

#include "config.h"
#include "network.h"
#include "radio.h"

namespace updater {
namespace {

bool active = false;
bool otaStarted = false;
uint32_t deadline = 0;

void startOta() {
  char host[20];
  snprintf(host, sizeof host, "firefly-%06x", ESP.getChipId());
  ArduinoOTA.setHostname(host);
  ArduinoOTA.setPassword(FF_OTA_PASSWORD);
  ArduinoOTA.onStart([] { Serial.println(F("[update] receiving firmware")); });
  ArduinoOTA.begin();
  otaStarted = true;
  Serial.printf("[update] ready for OTA at %s (%s)\n", WiFi.localIP().toString().c_str(), host);
}

}  // namespace

bool start(uint32_t now, uint16_t minutes) {
  uint32_t until = now + minutes * 60000UL;
  if (active) {
    deadline = until;
    return true;
  }
  String ssid, password;
  if (!net::homeCredentials(ssid, password)) {
    Serial.println(F("[update] no home Wi-Fi known, can't enter update mode"));
    return false;
  }
  WiFi.begin(ssid.c_str(), password.c_str(), radio::channel());  // stay on the mesh channel
  active = true;
  deadline = until;
  Serial.printf("[update] update mode for %u min, joining \"%s\"\n", minutes, ssid.c_str());
  return true;
}

bool loop(uint32_t now) {
  if (!active) return false;
  if (!otaStarted && WiFi.status() == WL_CONNECTED) startOta();
  if (otaStarted) ArduinoOTA.handle();  // a finished update reboots by itself
  return (int32_t)(now - deadline) >= 0;
}

uint32_t ip() { return active && WiFi.status() == WL_CONNECTED ? (uint32_t)WiFi.localIP() : 0; }

}  // namespace updater
