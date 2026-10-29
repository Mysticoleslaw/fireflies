#pragma once
#include <stdint.h>
#include "mac.h"

// Picks one master among the bugs. Rules:
//  - Listen; if no master beacon arrives before the deadline, become master.
//  - When two masters meet, the longer uptime wins (it was switched on first); ties go to the lower MAC.
//  - Followers go back to searching after kMasterTimeoutMs without a beacon.
namespace ff {

enum class Role : uint8_t { Searching, Follower, Master };

constexpr uint32_t kMasterTimeoutMs = 5000;

bool outranks(uint32_t aUptime, const Mac& a, uint32_t bUptime, const Mac& b);

class Election {
 public:
  explicit Election(const Mac& self) : self_(self) {}

  // Start (or restart) searching; become master if nothing is heard within waitMs.
  void begin(uint32_t now, uint32_t waitMs);
  // A master beacon. `now` doubles as our own uptime.
  void onBeacon(uint32_t now, const Mac& from, uint32_t theirUptime);
  void tick(uint32_t now);

  Role role() const { return role_; }
  bool searching() const { return role_ == Role::Searching; }
  // True once begin() has been called for the current search.
  bool armed() const { return armed_; }
  const Mac& master() const { return master_; }

 private:
  void follow(uint32_t now, const Mac& from, uint32_t theirUptime);
  uint32_t masterUptime(uint32_t now) const { return masterUptime_ + (now - lastBeacon_); }

  Mac self_;
  Role role_ = Role::Searching;
  bool armed_ = false;
  Mac master_{};
  uint32_t deadline_ = 0;
  uint32_t lastBeacon_ = 0;
  uint32_t masterUptime_ = 0;
};

}  // namespace ff
