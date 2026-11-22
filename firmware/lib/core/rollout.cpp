#include "rollout.h"

namespace ff {

uint8_t* Rollout::triesFor(const Mac& mac) {
  for (size_t i = 0; i < triedCount_; i++) {
    if (tries_[i].mac == mac) return &tries_[i].count;
  }
  if (triedCount_ == Roster::kCapacity) return nullptr;
  tries_[triedCount_] = Tries{mac, 0};
  return &tries_[triedCount_++].count;
}

bool Rollout::due(const RosterEntry& e, uint32_t build, const Mac& self) {
  if (e.mac == self || !e.online || e.status.build == build) return false;
  uint8_t* tries = triesFor(e.mac);
  return tries && *tries < kMaxTries;
}

bool Rollout::next(const Roster& roster, uint32_t build, const Mac& self, uint32_t now, Mac& out) {
  if (now < kStartAfterMs) return false;
  for (size_t i = 0; i < roster.size(); i++) {
    if (roster.at(i).online && roster.at(i).status.ip) return false;  // the Mac is updating bugs
  }
  if (active_) {
    const RosterEntry* e = roster.find(current_);
    bool done = e && e->online && e->status.build == build;
    if (!done && now - startedAt_ < kTimeoutMs) return false;
    active_ = false;
  }
  for (size_t i = 0; i < roster.size(); i++) {
    const RosterEntry& e = roster.at(i);
    if (!due(e, build, self)) continue;
    (*triesFor(e.mac))++;
    active_ = true;
    current_ = e.mac;
    startedAt_ = now;
    out = e.mac;
    return true;
  }
  return false;
}

}  // namespace ff
