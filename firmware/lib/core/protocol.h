#pragma once
#include <stddef.h>
#include <stdint.h>
#include "mac.h"
#include "settings.h"

// Bug-to-bug ESP-NOW packets. Everything is broadcast; commands carry a target id.
namespace ff {

constexpr uint8_t kProtocolVersion = 8;  // 8: rainbowHop, glow colour mode, glow ranges

enum class PacketType : uint8_t { Beacon = 1, Status = 2, Command = 3 };
// Hold: the master is about to restart on purpose; don't claim master for `arg` seconds.
// Update: followers join home Wi-Fi with OTA open for `arg` minutes (wireless updates).
enum class CommandType : uint8_t { Hop = 1, Ripple = 2, Identify = 3, Settings = 4, Hold = 5, Update = 6 };
enum class NetMode : uint8_t { None = 0, HomeWifi = 1, Hotspot = 2 };

struct __attribute__((packed)) Header {
  char magic[2];  // "FF"
  uint8_t version;
  uint8_t type;
  uint16_t group;
  uint16_t seq;
};

struct __attribute__((packed)) BeaconPacket {
  Header h;
  uint32_t uptimeMs;
  uint8_t net;
};

struct __attribute__((packed)) StatusBody {
  uint32_t uptimeMs;
  uint8_t lit;
  uint8_t r, g, b;
  uint16_t batteryMv;   // 0 = not wired
  uint8_t resetReason;  // ESP8266 rst_info.reason of the last start
  uint32_t build;       // FF_BUILD_ID: fingerprint of the firmware source
  uint32_t ip;          // IPv4 while in update mode, else 0
  Settings settings;
};

struct __attribute__((packed)) StatusPacket {
  Header h;
  StatusBody body;
};

struct __attribute__((packed)) CommandPacket {
  Header h;
  Mac target;  // kBroadcast = every bug
  uint8_t command;
  uint16_t arg;  // ripple: max delay ms
  uint32_t mask;  // settings: which fields to apply
  Settings settings;
};

constexpr size_t kMaxPacket = 96;  // receive buffer size; ESP-NOW allows up to 250
static_assert(sizeof(CommandPacket) <= kMaxPacket, "packet too big");
static_assert(sizeof(StatusPacket) <= kMaxPacket, "packet too big");

inline Header makeHeader(PacketType type, uint16_t group, uint16_t seq) {
  return Header{{'F', 'F'}, kProtocolVersion, (uint8_t)type, group, seq};
}

inline size_t packetSize(PacketType type) {
  switch (type) {
    case PacketType::Beacon: return sizeof(BeaconPacket);
    case PacketType::Status: return sizeof(StatusPacket);
    case PacketType::Command: return sizeof(CommandPacket);
  }
  return 0;
}

// Checks magic, version, group and exact size. Sets type on success.
inline bool parsePacket(const uint8_t* data, size_t len, uint16_t group, PacketType& type) {
  if (!data || len < sizeof(Header)) return false;
  Header h;
  memcpy(&h, data, sizeof h);
  if (h.magic[0] != 'F' || h.magic[1] != 'F' || h.version != kProtocolVersion || h.group != group) return false;
  PacketType t = (PacketType)h.type;
  if (packetSize(t) == 0 || packetSize(t) != len) return false;
  type = t;
  return true;
}

inline bool isTargeted(const CommandPacket& c, const Mac& self) {
  return c.target == kBroadcast || c.target == self;
}

}  // namespace ff
