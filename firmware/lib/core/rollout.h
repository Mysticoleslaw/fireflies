#pragma once
#include <stddef.h>
#include <stdint.h>

#include "mac.h"
#include "roster.h"

// The master upgrading followers to its own firmware, one at a time. It only decides who
// goes next; the follower downloads from the master and refuses anything that isn't newer.
namespace ff {

class Rollout {
 public:
  static constexpr uint32_t kStartAfterMs = 60000;  // the master's build has to run a while first
  static constexpr uint32_t kTimeoutMs = 120000;    // join Wi-Fi + download + flash + reboot + rejoin
  static constexpr uint8_t kMaxTries = 2;           // per bug, per master boot

  // Call every loop with the master's uptime. Returns true, with the bug in `out`, when that
  // bug should be told to fetch the master's firmware now.
  bool next(const Roster& roster, uint32_t build, const Mac& self, uint32_t now, Mac& out);

 private:
  struct Tries {
    Mac mac;
    uint8_t count;
  };
  uint8_t* triesFor(const Mac& mac);
  bool due(const RosterEntry& e, uint32_t build, const Mac& self);

  bool active_ = false;
  Mac current_{};
  uint32_t startedAt_ = 0;
  Tries tries_[Roster::kCapacity] = {};
  size_t triedCount_ = 0;
};

}  // namespace ff
