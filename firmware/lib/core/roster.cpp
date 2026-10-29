#include "roster.h"

#include <stddef.h>
#include <string.h>

namespace ff {
namespace {

// Compare everything after uptimeMs, which changes on every status.
bool sameState(const StatusBody& a, const StatusBody& b) {
  const size_t from = offsetof(StatusBody, lit);
  return memcmp((const uint8_t*)&a + from, (const uint8_t*)&b + from, sizeof(StatusBody) - from) == 0;
}

}  // namespace

bool Roster::upsert(const Mac& mac, const StatusBody& status, uint32_t now) {
  for (size_t i = 0; i < count_; i++) {
    RosterEntry& e = entries_[i];
    if (e.mac != mac) continue;
    bool restarted = status.uptimeMs < e.status.uptimeMs;
    bool changed = !e.online || restarted || !sameState(e.status, status);
    e = RosterEntry{mac, status, now, true, e.dirty || changed,
                    (uint16_t)(e.restarts + restarted), e.dropouts};
    return true;
  }
  if (count_ == kCapacity) return false;
  entries_[count_++] = RosterEntry{mac, status, now, true, true, 0, 0};
  return true;
}

const RosterEntry* Roster::find(const Mac& mac) const {
  for (size_t i = 0; i < count_; i++) {
    if (entries_[i].mac == mac) return &entries_[i];
  }
  return nullptr;
}

void Roster::sweep(uint32_t now) {
  for (size_t i = 0; i < count_; i++) {
    RosterEntry& e = entries_[i];
    if (e.online && now - e.lastSeen > kOfflineMs) {
      e.online = false;
      e.dirty = true;
      e.dropouts++;
    }
  }
}

}  // namespace ff
