#include "fetch.h"

#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>

#include "config.h"
#include "generated/build_info.h"
#include "network.h"
#include "radio.h"

namespace fetch {
namespace {

bool active = false;
uint32_t deadline = 0;
uint32_t serverIp = 0;

void stop(const char* why) {
  Serial.printf("[fetch] %s\n", why);
  WiFi.disconnect();  // station only: the mesh AP stays up
  active = false;
}

void download() {
  String url = "http://" + IPAddress(serverIp).toString() + "/firmware.bin";
  Serial.printf("[fetch] downloading %s\n", url.c_str());
  WiFiClient client;
  ESPhttpUpdate.rebootOnUpdate(true);
  switch (ESPhttpUpdate.update(client, url, String(FF_BUILD_TIME))) {
    case HTTP_UPDATE_NO_UPDATES: return stop("the master's firmware isn't newer, keeping ours");
    case HTTP_UPDATE_FAILED: return stop(ESPhttpUpdate.getLastErrorString().c_str());
    case HTTP_UPDATE_OK: break;  // rebooting
  }
}

}  // namespace

bool start(uint32_t now, ff::NetMode masterMode, uint32_t masterIp) {
  if (active || !masterIp) return false;
  String ssid, password;
  if (masterMode == ff::NetMode::Hotspot) {
    ssid = cfg::kHotspotSsid;
    password = FF_HOTSPOT_PASSWORD;
  } else if (!net::homeCredentials(ssid, password)) {
    Serial.println(F("[fetch] no home Wi-Fi known, can't fetch firmware"));
    return false;
  }
  WiFi.begin(ssid.c_str(), password.c_str(), radio::channel());  // stay on the mesh channel
  active = true;
  deadline = now + cfg::kFetchJoinMs;
  serverIp = masterIp;
  Serial.printf("[fetch] master has newer firmware, joining \"%s\"\n", ssid.c_str());
  return true;
}

void loop(uint32_t now, bool armBusy) {
  if (!active) return;
  if (WiFi.status() == WL_CONNECTED && !armBusy) return download();
  if ((int32_t)(now - deadline) >= 0) stop("couldn't join the master's network");
}

}  // namespace fetch
