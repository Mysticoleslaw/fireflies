#include "gateway.h"

#include <ArduinoJson.h>
#include <user_interface.h>  // REASON_* reset codes
#include <ESPAsyncWebServer.h>

#include "generated/webapp_files.h"

#include "network.h"

namespace gateway {
namespace {

constexpr long kRippleMaxMs = 10000;
constexpr uint32_t kMinFreeBlock = 6000;  // refuse the bug list below this much contiguous heap
constexpr size_t kMaxQueuedEvents = 4;    // ESP8266 SSE queue holds 8 per app

AsyncWebServer server(80);
AsyncEventSource events("/api/events");
const ff::Roster* roster = nullptr;
ff::Mac self{};
CommandSink sink = nullptr;

// ESP8266 rst_info.reason. A brownout from weak batteries usually shows up as "power-on".
const char* resetReasonName(uint8_t reason) {
  switch (reason) {
    case REASON_DEFAULT_RST: return "power-on";
    case REASON_WDT_RST:
    case REASON_SOFT_WDT_RST: return "watchdog";
    case REASON_EXCEPTION_RST: return "crash";
    case REASON_SOFT_RESTART: return "restart";
    case REASON_DEEP_SLEEP_AWAKE: return "wake";
    case REASON_EXT_SYS_RST: return "reset button";
    default: return "unknown";
  }
}

String hexColour(uint8_t r, uint8_t g, uint8_t b) {
  char buf[8];
  snprintf(buf, sizeof buf, "#%02x%02x%02x", r, g, b);
  return buf;
}

void writeSettings(JsonObject o, const ff::Settings& s) {
  o["hopping"] = s.hopping != 0;
  o["randomColour"] = s.randomColour != 0;
  o["stayLit"] = s.stayLit != 0;
  o["holdServo"] = s.holdServo != 0;
  o["onboardLed"] = s.onboardLed != 0;
  o["colour"] = hexColour(s.r, s.g, s.b);
  o["brightness"] = s.brightness;
  o["minWait"] = s.minWait;
  o["maxWait"] = s.maxWait;
  o["swingMs"] = s.swingMs;
  o["fadeMs"] = s.fadeMs;
  o["pulseA"] = s.pulseA;
  o["pulseB"] = s.pulseB;
  o["ledDelayMs"] = s.ledDelayMs;
  o["fireflyGlow"] = s.fireflyGlow != 0;
  o["glowColourMode"] = ff::glowModeName(s.glowColourMode);
  o["rainbowHop"] = s.rainbowHop != 0;
  o["glowColour"] = hexColour(s.glowR, s.glowG, s.glowB);
  o["glowPulsesMin"] = s.glowPulsesMin;
  o["glowPulsesMax"] = s.glowPulses;
  o["glowPulseMinMs"] = s.glowPulseMinMs;
  o["glowPulseMaxMs"] = s.glowPulseMs;
  o["glowEveryMinSec"] = s.glowEveryMinSec;
  o["glowEveryMaxSec"] = s.glowEverySec;
  o["glowBrightMin"] = s.glowBrightMin;
  o["glowBrightMax"] = s.glowBrightMax;
}

String bugJson(const ff::RosterEntry& e) {
  JsonDocument doc;
  char id[13];
  ff::macToHex(e.mac, id);
  doc["id"] = String(id);
  doc["name"] = String(e.status.settings.name);
  doc["master"] = e.mac == self;
  doc["online"] = e.online;
  doc["lit"] = e.status.lit != 0;
  doc["colour"] = hexColour(e.status.r, e.status.g, e.status.b);
  doc["uptime"] = e.status.uptimeMs / 1000;
  if (e.status.batteryMv) doc["battery"] = e.status.batteryMv / 1000.0;
  else doc["battery"] = nullptr;
  doc["lastStart"] = resetReasonName(e.status.resetReason);
  char build[9];
  snprintf(build, sizeof build, "%08x", (unsigned)e.status.build);
  doc["build"] = String(build);
  if (e.status.ip) doc["ip"] = IPAddress(e.status.ip).toString();
  else doc["ip"] = nullptr;
  doc["restarts"] = e.restarts;
  doc["dropouts"] = e.dropouts;
  writeSettings(doc["settings"].to<JsonObject>(), e.status.settings);
  String out;
  serializeJson(doc, out);
  return out;
}

void sendError(AsyncWebServerRequest* req, const char* message, int code = 400) {
  JsonDocument doc;
  doc["error"] = message;
  String body;
  serializeJson(doc, body);
  req->send(code, "application/json", body);
}

void sendCommand(AsyncWebServerRequest* req, const ff::CommandPacket& c) {
  if (!sink(c)) return sendError(req, "bugs are busy, try again", 503);
  req->send(200, "application/json", "{\"ok\":true}");
}

bool parseTarget(AsyncWebServerRequest* req, ff::Mac& out, bool allowAll) {
  if (!req->hasParam("id")) return false;
  const String& id = req->getParam("id")->value();
  if (allowAll && id == "all") {
    out = ff::kBroadcast;
    return true;
  }
  return ff::macFromHex(id.c_str(), out);
}

ff::CommandPacket makeCommand(ff::CommandType type, const ff::Mac& target) {
  ff::CommandPacket c{};
  c.target = target;
  c.command = (uint8_t)type;
  return c;
}

// Master first, then by name.
bool listsBefore(const ff::RosterEntry& a, const ff::RosterEntry& b) {
  if ((a.mac == self) != (b.mac == self)) return a.mac == self;
  return strcasecmp(a.status.settings.name, b.status.settings.name) < 0;
}

// Streams /api/bugs one bug at a time, so 35 bugs never sit in RAM as one big string.
// Holds MACs rather than indices: the roster can change between chunks.
struct BugListWriter {
  ff::Mac order[ff::Roster::kCapacity];
  size_t count = 0, next = 0, pos = 0;
  bool opened = false, closed = false;
  String pending;

  BugListWriter() {
    const ff::RosterEntry* sorted[ff::Roster::kCapacity];
    for (size_t i = 0; i < roster->size(); i++) {
      size_t j = count++;
      for (; j > 0 && listsBefore(roster->at(i), *sorted[j - 1]); j--) sorted[j] = sorted[j - 1];
      sorted[j] = &roster->at(i);
    }
    for (size_t i = 0; i < count; i++) order[i] = sorted[i]->mac;
  }

  // Loads the next piece into `pending`. Returns false (leaving pos at the end) when done.
  bool refill() {
    if (!opened) {
      opened = true;
      return load("{\"bugs\":[");
    }
    while (next < count) {
      const ff::RosterEntry* e = roster->find(order[next]);
      bool comma = next++ > 0;
      if (e) return load(comma ? "," + bugJson(*e) : bugJson(*e));
    }
    if (!closed) {
      closed = true;
      return load("]}");
    }
    return false;
  }

  bool load(const String& piece) {
    pending = piece;
    pos = 0;
    return true;
  }

  size_t fill(uint8_t* buf, size_t maxLen) {
    size_t written = 0;
    while (written < maxLen && (pos < pending.length() || refill())) {
      size_t n = std::min(maxLen - written, pending.length() - pos);
      memcpy(buf + written, pending.c_str() + pos, n);
      pos += n;
      written += n;
    }
    return written;
  }
};

void handleBugs(AsyncWebServerRequest* req) {
  if (ESP.getMaxFreeBlockSize() < kMinFreeBlock) return sendError(req, "master is low on memory, try again", 503);
  auto writer = std::make_shared<BugListWriter>();
  req->sendChunked("application/json",
                   [writer](uint8_t* buf, size_t maxLen, size_t) { return writer->fill(buf, maxLen); });
}

void handleHop(AsyncWebServerRequest* req) {
  ff::Mac target;
  if (!parseTarget(req, target, true)) return sendError(req, "id must be a bug id or all");
  sendCommand(req, makeCommand(ff::CommandType::Hop, target));
}

void handleRipple(AsyncWebServerRequest* req) {
  long ms = req->hasParam("ms") ? req->getParam("ms")->value().toInt() : 3000;
  ff::CommandPacket c = makeCommand(ff::CommandType::Ripple, ff::kBroadcast);
  c.arg = (uint16_t)constrain(ms, 0L, kRippleMaxMs);
  sendCommand(req, c);
}

void handleIdentify(AsyncWebServerRequest* req) {
  ff::Mac target;
  if (!parseTarget(req, target, false)) return sendError(req, "id must be a single bug id");
  sendCommand(req, makeCommand(ff::CommandType::Identify, target));
}

// Puts followers into update mode so new firmware can be pushed to them (scripts/update_all.py).
void handleUpdate(AsyncWebServerRequest* req) {
  if (net::mode() != ff::NetMode::HomeWifi) return sendError(req, "the master must be on home Wi-Fi for updates");
  ff::Mac target;
  if (!parseTarget(req, target, true)) return sendError(req, "id must be a bug id or all");
  if (target == self) return sendError(req, "update the master directly over OTA");
  long minutes = req->hasParam("minutes") ? req->getParam("minutes")->value().toInt() : 10;
  ff::CommandPacket c = makeCommand(ff::CommandType::Update, target);
  c.arg = (uint16_t)constrain(minutes, 1L, 30L);
  sendCommand(req, c);
}

void handleSettings(AsyncWebServerRequest* req) {
  ff::Mac target;
  if (!parseTarget(req, target, true)) return sendError(req, "id must be a bug id or all");
  ff::Settings patch{};
  uint32_t mask = 0;
  for (size_t i = 0; i < req->params(); i++) {
    const AsyncWebParameter* p = req->getParam(i);
    if (p->isPost() || p->isFile() || p->name() == "id") continue;
    const char* err = ff::parseSettingParam(p->name().c_str(), p->value().c_str(), patch, mask);
    if (err) return sendError(req, err);
  }
  if (!mask) return sendError(req, "no settings given");
  if ((mask & ff::kName) && target == ff::kBroadcast) return sendError(req, "name needs a single bug id");
  ff::CommandPacket c = makeCommand(ff::CommandType::Settings, target);
  c.mask = mask;
  c.settings = patch;
  sendCommand(req, c);
}

const char* netModeName(ff::NetMode mode) {
  if (mode == ff::NetMode::HomeWifi) return "home";
  if (mode == ff::NetMode::Hotspot) return "hotspot";
  return "none";
}

// Form-body field first (how the app sends it), then the query string.
bool formValue(AsyncWebServerRequest* req, const char* name, String& out) {
  for (bool post : {true, false}) {
    if (!req->hasParam(name, post)) continue;
    out = req->getParam(name, post)->value();
    return true;
  }
  return false;
}

void handleWifiGet(AsyncWebServerRequest* req) {
  net::WifiInfo info = net::wifiInfo();
  JsonDocument doc;
  doc["mode"] = netModeName(info.mode);
  doc["offline"] = info.offline;
  if (info.ssid.length()) doc["ssid"] = info.ssid; else doc["ssid"] = nullptr;
  if (info.saved.length()) doc["saved"] = info.saved; else doc["saved"] = nullptr;
  JsonArray networks = doc["networks"].to<JsonArray>();
  if (info.saved.length()) networks.add(info.saved);
  if (info.builtIn.length() && info.builtIn != info.saved) networks.add(info.builtIn);
  String body;
  serializeJson(doc, body);
  req->send(200, "application/json", body);
}

void handleWifiSet(AsyncWebServerRequest* req) {
  String ssid, password;
  if (!formValue(req, "ssid", ssid)) return sendError(req, "ssid is required");
  formValue(req, "password", password);
  if (const char* err = net::setWifi(ssid, password)) return sendError(req, err);
  req->send(200, "application/json", "{\"ok\":true,\"restarting\":true}");
}

void sendRestarting(AsyncWebServerRequest* req, const char* err) {
  if (err) return sendError(req, err);
  req->send(200, "application/json", "{\"ok\":true,\"restarting\":true}");
}

void handleWifiForget(AsyncWebServerRequest* req) {
  String ssid;
  formValue(req, "ssid", ssid);
  sendRestarting(req, net::forgetWifi(ssid));
}

void handleWifiDisconnect(AsyncWebServerRequest* req) { sendRestarting(req, net::setOffline(true)); }
void handleWifiReconnect(AsyncWebServerRequest* req) { sendRestarting(req, net::setOffline(false)); }

// The web app is compiled into the firmware (gzipped), so updating a bug updates its app
// too, and saved settings in LittleFS are never touched.
void sendWebFile(AsyncWebServerRequest* req, const WebFile& file) {
  AsyncWebServerResponse* res = req->beginResponse(200, file.mime, file.gz, file.size);
  res->addHeader("Content-Encoding", "gzip");
  res->addHeader("Cache-Control", "no-cache");
  req->send(res);
}

// Unknown GETs go to the app (this is also what makes the hotspot's captive portal work).
// OPTIONS answers CORS preflights for apps running on another origin.
void handleNotFound(AsyncWebServerRequest* req) {
  if (req->method() == HTTP_OPTIONS) {
    AsyncWebServerResponse* res = req->beginResponse(204);
    res->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    res->addHeader("Access-Control-Allow-Headers", "Content-Type");
    return req->send(res);
  }
  if (req->method() == HTTP_GET && !req->url().startsWith("/api/")) return req->redirect("/");
  sendError(req, "not found", 404);
}

}  // namespace

void begin(const ff::Roster& r, const ff::Mac& selfMac, CommandSink commandSink) {
  roster = &r;
  self = selfMac;
  sink = commandSink;
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  server.on("/api/bugs", HTTP_GET, handleBugs);
  server.on("/api/hop", HTTP_POST, handleHop);
  server.on("/api/ripple", HTTP_POST, handleRipple);
  server.on("/api/identify", HTTP_POST, handleIdentify);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/update", HTTP_POST, handleUpdate);
  // "/api/wifi" would also match "/api/wifi/forget", so register the longer path first.
  server.on("/api/wifi/forget", HTTP_POST, handleWifiForget);
  server.on("/api/wifi/disconnect", HTTP_POST, handleWifiDisconnect);
  server.on("/api/wifi/reconnect", HTTP_POST, handleWifiReconnect);
  server.on("/api/wifi", HTTP_GET, handleWifiGet);
  server.on("/api/wifi", HTTP_POST, handleWifiSet);
  server.addHandler(&events);
  for (const WebFile& file : kWebFiles) {
    server.on(file.path, HTTP_GET, [&file](AsyncWebServerRequest* req) { sendWebFile(req, file); });
  }
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) { sendWebFile(req, kWebFiles[0]); });
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println(F("[gateway] web app and API up"));
}

bool publish(const ff::RosterEntry& entry) {
  if (events.count() == 0) return true;  // nobody watching: nothing to catch up on
  if (events.avgPacketsWaiting() >= kMaxQueuedEvents) return false;
  events.send(bugJson(entry).c_str(), "bug", millis());
  return true;
}

void gone(const ff::Mac& mac) {
  if (events.count() == 0) return;
  char id[13];
  ff::macToHex(mac, id);
  String body = String("{\"id\":\"") + id + "\"}";
  events.send(body.c_str(), "gone", millis());
}

}  // namespace gateway
