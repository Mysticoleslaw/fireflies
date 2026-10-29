#include "election.h"

namespace ff {

bool outranks(uint32_t aUptime, const Mac& a, uint32_t bUptime, const Mac& b) {
  if (aUptime != bUptime) return aUptime > bUptime;
  return a < b;
}

void Election::begin(uint32_t now, uint32_t waitMs) {
  role_ = Role::Searching;
  armed_ = true;
  deadline_ = now + waitMs;
}

void Election::onBeacon(uint32_t now, const Mac& from, uint32_t theirUptime) {
  if (from == self_) return;
  switch (role_) {
    case Role::Searching:
      follow(now, from, theirUptime);
      break;
    case Role::Follower:
      if (from == master_ || outranks(theirUptime, from, masterUptime(now), master_)) {
        follow(now, from, theirUptime);
      }
      break;
    case Role::Master:
      if (outranks(theirUptime, from, now, self_)) follow(now, from, theirUptime);
      break;
  }
}

void Election::tick(uint32_t now) {
  if (role_ == Role::Searching && armed_ && (int32_t)(now - deadline_) >= 0) {
    role_ = Role::Master;
    master_ = self_;
  } else if (role_ == Role::Follower && now - lastBeacon_ > kMasterTimeoutMs) {
    role_ = Role::Searching;
    armed_ = false;  // caller rescans the channel, then calls begin()
  }
}

void Election::follow(uint32_t now, const Mac& from, uint32_t theirUptime) {
  role_ = Role::Follower;
  master_ = from;
  lastBeacon_ = now;
  masterUptime_ = theirUptime;
}

}  // namespace ff
