#include "node.h"


#include <user_interface.h>  // rst_info

#include "battery.h"
#include "generated/build_info.h"
#include "config.h"
#include "gateway.h"
#include "network.h"
#include "radio.h"
#include "storage.h"
#include "updater.h"

namespace {

const char* roleName(ff::Role r) {
  switch (r) {
    case ff::Role::Searching: return "searching";
    case ff::Role::Follower: return "follower";
    case ff::Role::Master: return "master";
  }
  return "?";
}

bool forwardToNode(const ff::CommandPacket& c) { return node.queueCommand(c); }

}  // namespace

void Node::begin(uint32_t now) {
  self_ = radio::selfMac();
  settings_ = storage::load(self_);
  hopper_.begin(settings_, now);
  election_ = ff::Election(self_);
  seq_ = random(0x10000);
  if (!radio::begin(cfg::kDefaultChannel)) Serial.println(F("[mesh] ESP-NOW failed to start"));
  char id[13];
  ff::macToHex(self_, id);
  Serial.printf("[boot] %s (%s)\n", settings_.name, id);
  startSearch();
}

void Node::loop(uint32_t now) {
  hopper_.loop(now);
  battery::loop(now);
  if (scanning_) {
    pollScan(now);
  } else {
    drainRadio(now);
    cycleChannel(now);
    election_.tick(now);
    handleRoleChange(now);
  }
  if (election_.role() == ff::Role::Master) masterLoop(now);
  if (restartAt_) restart(now);
  if (updater::loop(now)) restart(now);  // update window over without an update
  drainInbox(now);
  flushOutgoing(now);
  statusLoop(now);
  saveLoop(now);
}

bool Node::queueCommand(const ff::CommandPacket& command) {
  if (inboxCount_ == kQueueLen) return false;
  inbox_[inboxCount_++] = command;
  return true;
}

// ---------- election ----------

void Node::startSearch() {
  scanning_ = true;
  net::beginScan();
}

// Listen long enough to visit every candidate channel twice before claiming master.
void Node::pollScan(uint32_t now) {
  if (!net::scanResult(scan_)) return;
  scanning_ = false;
  channelIdx_ = 0;
  channelHopAt_ = now + cfg::kChannelDwellMs;
  int32_t holdLeft = (int32_t)(holdUntil_ - now);
  if (holdLeft > 0) {  // master restarting on purpose: it may come back on any channel
    scan_.count = net::kMaxChannels;
    for (uint8_t i = 0; i < scan_.count; i++) scan_.channels[i] = i + 1;
  }
  if (!radio::begin(scan_.channels[0])) Serial.println(F("[mesh] ESP-NOW failed to restart after scan"));
  uint32_t listen = std::max<uint32_t>(cfg::kSearchMs, 2 * scan_.count * cfg::kChannelDwellMs);
  listen = std::max<int32_t>(listen, holdLeft);
  election_.begin(now, listen + random(cfg::kSearchJitterMs));
  Serial.printf("[mesh] home Wi-Fi %s, listening on %u channel(s) starting with %u\n",
                scan_.homeFound ? "found" : "not found", scan_.count, scan_.channels[0]);
}

void Node::cycleChannel(uint32_t now) {
  if (!election_.searching() || !election_.armed() || scan_.count < 2) return;
  if ((int32_t)(now - channelHopAt_) < 0) return;
  channelIdx_ = (channelIdx_ + 1) % scan_.count;
  channelHopAt_ = now + cfg::kChannelDwellMs;
  radio::setChannel(scan_.channels[channelIdx_]);
}

void Node::handleRoleChange(uint32_t now) {
  ff::Role role = election_.role();
  if (role == lastRole_) return;
  Serial.printf("[mesh] %s -> %s\n", roleName(lastRole_), roleName(role));
  if (lastRole_ == ff::Role::Master) {
    // Outranked by an older master. Restarting is the cleanest way to drop Wi-Fi and the server.
    Serial.println(F("[mesh] stepping down"));
    restart(now);
  }
  lastRole_ = role;
  if (role == ff::Role::Master) becomeMaster(now);
  if (role == ff::Role::Follower) statusDue_ = true;  // introduce ourselves to the new master
  if (role == ff::Role::Searching && !election_.armed()) startSearch();
}

void Node::becomeMaster(uint32_t now) {
  uint8_t channel = scan_.homeFound ? scan_.homeChannel : cfg::kDefaultChannel;
  radio::setChannel(channel);
  sendBeacon(now);  // claim it before the slow Wi-Fi setup, so rivals can stand down
  net::startMaster(channel, scan_.homeFound, now);
}

void Node::masterLoop(uint32_t now) {
  if (net::loop(now)) restart(now);
  if (!gatewayStarted_ && net::mode() != ff::NetMode::None) {
    gateway::begin(roster_, self_, forwardToNode);
    gatewayStarted_ = true;
  }
  if (now - lastBeacon_ >= cfg::kBeaconMs && radio::ready()) sendBeacon(now);
  roster_.sweep(now);
  roster_.removeGone(now, gateway::gone);
  if (gatewayStarted_) roster_.drainDirty(gateway::publish, cfg::kPublishPerLoop);
}

// A master restarting on purpose (Wi-Fi changes, recovery) first tells followers to hold,
// so none of them grabs master (without the new Wi-Fi settings) while it reboots.
void Node::restart(uint32_t now) {
  if (election_.role() == ff::Role::Master && !restartAt_) {
    ff::CommandPacket hold{};
    hold.target = ff::kBroadcast;
    hold.command = (uint8_t)ff::CommandType::Hold;
    hold.arg = cfg::kHoldSeconds;
    enqueueOutgoing(hold, now);
    restartAt_ = now + cfg::kRestartDelayMs;
    return;
  }
  if (restartAt_ && (int32_t)(now - restartAt_) < 0) return;
  if (settingsDirty_) storage::save(settings_);
  delay(50);
  ESP.restart();
}

// ---------- incoming ----------

void Node::drainRadio(uint32_t now) {
  RxPacket p;
  while (radio::receive(p)) {
    ff::PacketType type;
    if (p.from == self_ || !ff::parsePacket(p.data, p.len, FF_GROUP_ID, type)) continue;
    if (type == ff::PacketType::Beacon) {
      ff::BeaconPacket b;
      memcpy(&b, p.data, sizeof b);
      election_.onBeacon(now, p.from, b.uptimeMs);
    } else if (type == ff::PacketType::Status && election_.role() == ff::Role::Master) {
      ff::StatusPacket s;
      memcpy(&s, p.data, sizeof s);
      s.body.settings = ff::clampSettings(s.body.settings);  // also terminates the name
      if (!roster_.upsert(p.from, s.body, now)) Serial.println(F("[mesh] roster full, ignoring a bug"));
    } else if (type == ff::PacketType::Command) {
      ff::CommandPacket c;
      memcpy(&c, p.data, sizeof c);
      handleCommand(p.from, c, now);
    }
  }
}

void Node::handleCommand(const ff::Mac& from, const ff::CommandPacket& c, uint32_t now) {
  if (election_.role() != ff::Role::Follower || from != election_.master()) return;
  if (from != seenFrom_) {  // new master: forget old seqs
    seenFrom_ = from;
    seenCount_ = 0;
  }
  if (seenBefore(c.h.seq) || !ff::isTargeted(c, self_)) return;
  applyCommand(c, now);
}

// Commands are sent several times; act on each seq once.
bool Node::seenBefore(uint16_t seq) {
  for (size_t i = 0; i < seenCount_; i++) {
    if (seen_[i] == seq) return true;
  }
  seen_[seenNext_] = seq;
  seenNext_ = (seenNext_ + 1) % kSeenLen;
  if (seenCount_ < kSeenLen) seenCount_++;
  return false;
}

void Node::applyCommand(const ff::CommandPacket& c, uint32_t now) {
  switch ((ff::CommandType)c.command) {
    case ff::CommandType::Hop: hopper_.hopNow(now); break;
    case ff::CommandType::Ripple: hopper_.hopAfter(now, random((uint32_t)c.arg + 1)); break;
    case ff::CommandType::Identify: hopper_.identify(now); break;
    case ff::CommandType::Update:
      if (election_.role() == ff::Role::Follower) updater::start(now, c.arg);  // master updates directly
      break;
    case ff::CommandType::Hold:
      holdUntil_ = now + c.arg * 1000UL;
      Serial.printf("[mesh] master restarting, holding for %u s\n", c.arg);
      break;
    case ff::CommandType::Settings: {
      ff::Settings patch = c.settings;
      settings_ = ff::applyMasked(settings_, patch, c.mask);
      hopper_.setSettings(settings_, now);
      settingsDirty_ = true;
      settingsChangedAt_ = now;
      statusDue_ = true;
      break;
    }
  }
}

// ---------- outgoing ----------

// Commands from the API: apply to ourselves, then broadcast to everyone else.
void Node::drainInbox(uint32_t now) {
  size_t kept = 0;
  for (size_t i = 0; i < inboxCount_; i++) {
    ff::CommandPacket c = inbox_[i];
    bool onlySelf = c.target == self_;
    if (!onlySelf && !enqueueOutgoing(c, now)) {
      inbox_[kept++] = c;  // send queue full, retry next loop
      continue;
    }
    if (ff::isTargeted(c, self_)) applyCommand(c, now);
  }
  inboxCount_ = kept;
}

bool Node::enqueueOutgoing(const ff::CommandPacket& c, uint32_t now) {
  for (Outgoing& o : outgoing_) {
    if (o.remaining) continue;
    o.packet = c;
    o.packet.h = ff::makeHeader(ff::PacketType::Command, FF_GROUP_ID, seq_++);
    o.remaining = cfg::kCommandRepeats;
    o.nextAt = now;
    return true;
  }
  return false;
}

// One packet per loop pass; a repeat only counts once it actually went out.
void Node::flushOutgoing(uint32_t now) {
  if (!radio::ready()) return;
  for (Outgoing& o : outgoing_) {
    if (!o.remaining || (int32_t)(now - o.nextAt) < 0) continue;
    if (!radio::send(&o.packet, sizeof o.packet)) return;
    o.remaining--;
    o.nextAt = now + cfg::kCommandGapMs;
    return;
  }
}

void Node::sendBeacon(uint32_t now) {
  ff::BeaconPacket b{ff::makeHeader(ff::PacketType::Beacon, FF_GROUP_ID, seq_++), now, (uint8_t)net::mode()};
  if (radio::send(&b, sizeof b)) lastBeacon_ = now;
}

// Sends status every kStatusMs, or soon after a change, with a random delay so bugs
// reacting to the same "hop all" don't all transmit in the same millisecond.
void Node::statusLoop(uint32_t now) {
  if (statusAt_ == 0) {
    if (hopper_.takeChanged() || statusDue_ || now - lastStatus_ >= cfg::kStatusMs) {
      statusAt_ = now + 1 + random(cfg::kStatusJitterMs);
      statusDue_ = false;
    }
    return;
  }
  if ((int32_t)(now - statusAt_) < 0) return;
  ff::StatusBody body = makeStatus(now);
  if (election_.role() == ff::Role::Master) {
    roster_.upsert(self_, body, now);
  } else if (election_.role() == ff::Role::Follower) {
    ff::StatusPacket s{ff::makeHeader(ff::PacketType::Status, FF_GROUP_ID, seq_++), body};
    if (!radio::send(&s, sizeof s)) return;  // radio busy: try again next pass
  }
  statusAt_ = 0;
  lastStatus_ = now;
}

void Node::saveLoop(uint32_t now) {
  if (!settingsDirty_ || now - settingsChangedAt_ < cfg::kSaveDelayMs) return;
  settingsDirty_ = false;
  storage::save(settings_);
}

ff::StatusBody Node::makeStatus(uint32_t now) const {
  ff::StatusBody body{};
  body.uptimeMs = now;
  body.lit = hopper_.lit();
  uint32_t c = hopper_.colour();
  body.r = c >> 16, body.g = c >> 8 & 0xff, body.b = c & 0xff;
  body.batteryMv = battery::millivolts();
  body.resetReason = (uint8_t)ESP.getResetInfoPtr()->reason;
  body.build = FF_BUILD_ID;
  body.ip = updater::ip();
  body.settings = settings_;
  return body;
}
