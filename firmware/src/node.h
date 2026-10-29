#pragma once
#include "election.h"
#include "hopper.h"
#include "network.h"
#include "protocol.h"
#include "roster.h"
#include "settings.h"

// Ties one bug together: hop behaviour, master election, mesh messages and (as master) the gateway.
class Node {
 public:
  void begin(uint32_t now);
  void loop(uint32_t now);
  // Commands from the web API. Returns false if the queue is full.
  bool queueCommand(const ff::CommandPacket& command);

 private:
  struct Outgoing {
    ff::CommandPacket packet;
    uint8_t remaining;
    uint32_t nextAt;
  };
  static constexpr size_t kQueueLen = 8;
  static constexpr size_t kSeenLen = 16;

  void startSearch();
  void pollScan(uint32_t now);
  void cycleChannel(uint32_t now);
  void handleRoleChange(uint32_t now);
  void becomeMaster(uint32_t now);
  void masterLoop(uint32_t now);
  void restart(uint32_t now);
  void drainRadio(uint32_t now);
  void handleCommand(const ff::Mac& from, const ff::CommandPacket& c, uint32_t now);
  bool seenBefore(uint16_t seq);
  void applyCommand(const ff::CommandPacket& c, uint32_t now);
  void drainInbox(uint32_t now);
  bool enqueueOutgoing(const ff::CommandPacket& c, uint32_t now);
  void flushOutgoing(uint32_t now);
  void sendBeacon(uint32_t now);
  void statusLoop(uint32_t now);
  void saveLoop(uint32_t now);
  ff::StatusBody makeStatus(uint32_t now) const;

  ff::Mac self_{};
  ff::Settings settings_{};
  Hopper hopper_;
  ff::Election election_{ff::Mac{}};
  ff::Roster roster_;
  ff::Role lastRole_ = ff::Role::Searching;

  bool scanning_ = false;
  net::ScanResult scan_{};
  uint8_t channelIdx_ = 0;
  uint32_t channelHopAt_ = 0;
  bool gatewayStarted_ = false;
  bool statusDue_ = true;
  bool settingsDirty_ = false;
  uint16_t seq_ = 0;
  uint32_t lastBeacon_ = 0;
  uint32_t lastStatus_ = 0;
  uint32_t settingsChangedAt_ = 0;

  ff::CommandPacket inbox_[kQueueLen];
  size_t inboxCount_ = 0;
  Outgoing outgoing_[kQueueLen] = {};
  uint16_t seen_[kSeenLen] = {};  // recent command seqs from the current master
  size_t seenNext_ = 0;
  size_t seenCount_ = 0;
  uint32_t statusAt_ = 0;    // jittered send time for a pending status
  uint32_t holdUntil_ = 0;   // don't claim master before this (master restarting on purpose)
  uint32_t restartAt_ = 0;   // planned restart, after the Hold broadcast goes out
  ff::Mac seenFrom_{};
};

extern Node node;
