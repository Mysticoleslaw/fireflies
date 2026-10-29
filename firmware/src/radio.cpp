#include "radio.h"

#include <ESP8266WiFi.h>
#include <espnow.h>

// ESP8266 quirk (measured on hardware): ESP-NOW broadcasts are only delivered to the
// receive callback while the softAP interface is up. In plain station mode the radio sits
// on the right channel but reception stays at zero. So every bug runs a hidden, private
// access point ("mesh AP") on the mesh channel. The master's visible hotspot replaces it.
namespace radio {
namespace {

constexpr size_t kQueueLen = 32;
constexpr uint32_t kSendTimeoutMs = 20;  // safety net if a send callback never arrives
constexpr uint16_t kMeshBeaconMs = 1000; // hidden AP beacons; slow, so 35 bugs stay quiet
RxPacket queue[kQueueLen];
volatile size_t head = 0, tail = 0;  // ESP-NOW callbacks run between loop() calls, never mid-loop
volatile bool busy = false;
uint32_t sentAt = 0;
uint8_t currentChannel = 1;
ff::Mac broadcast = ff::kBroadcast;

void onSent(uint8_t* mac, uint8_t status) { busy = false; }

void onReceive(uint8_t* mac, uint8_t* data, uint8_t len) {
  size_t next = (head + 1) % kQueueLen;
  if (next == tail || len > ff::kMaxPacket) return;  // full or oversized: drop
  RxPacket& p = queue[head];
  memcpy(p.from.b, mac, 6);
  memcpy(p.data, data, len);
  p.len = len;
  head = next;
}

void addBroadcastPeer() {
  esp_now_del_peer(broadcast.b);
  esp_now_add_peer(broadcast.b, ESP_NOW_ROLE_COMBO, currentChannel, nullptr, 0);
}

// Unique name and a random password nobody knows: phones joining "Fireflies" can't land here.
void startMeshAp(uint8_t channel) {
  if (WiFi.getMode() & WIFI_AP) return;  // already up (mesh AP or the master's hotspot)
  WiFi.mode(WIFI_AP_STA);
  char ssid[16], pass[17];
  snprintf(ssid, sizeof ssid, "ffmesh-%06x", ESP.getChipId());
  snprintf(pass, sizeof pass, "%08x%08x", (unsigned)RANDOM_REG32, (unsigned)RANDOM_REG32);
  if (!WiFi.softAP(ssid, pass, channel, 1 /*hidden*/, 1)) Serial.println(F("[radio] mesh AP failed to start"));
  softap_config c;
  wifi_softap_get_config(&c);
  c.beacon_interval = kMeshBeaconMs;
  wifi_softap_set_config_current(&c);
}

// Moves the AP (and with it the radio) to another channel. With a connected station the
// router decides the channel, so leave it alone.
void moveAp(uint8_t channel) {
  if (WiFi.status() == WL_CONNECTED) return;
  softap_config c;
  wifi_softap_get_config(&c);
  if (c.channel == channel) return;
  c.channel = channel;
  wifi_softap_set_config_current(&c);
}

}  // namespace

bool begin(uint8_t channel) {
  esp_now_deinit();  // safe if not running; also restarts cleanly after a Wi-Fi scan
  startMeshAp(channel);
  if (esp_now_init() != 0) return false;
  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onReceive);
  esp_now_register_send_cb(onSent);
  setChannel(channel);
  return true;
}

void setChannel(uint8_t channel) {
  currentChannel = channel;
  moveAp(channel);
  addBroadcastPeer();
}

uint8_t channel() { return currentChannel; }

bool ready() {
  if (busy && millis() - sentAt > kSendTimeoutMs) busy = false;
  return !busy;
}

bool send(const void* data, size_t len) {
  if (!ready() || esp_now_send(broadcast.b, (uint8_t*)data, len) != 0) return false;
  busy = true;
  sentAt = millis();
  return true;
}

bool receive(RxPacket& out) {
  if (tail == head) return false;
  out = queue[tail];
  tail = (tail + 1) % kQueueLen;
  return true;
}

ff::Mac selfMac() {
  ff::Mac mac;
  WiFi.softAPmacAddress(mac.b);
  return mac;
}

}  // namespace radio
