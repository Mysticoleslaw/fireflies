#pragma once
#include <stddef.h>
#include <stdint.h>
#include "mac.h"
#include "protocol.h"

// The master's list of every bug it has heard from.
namespace ff {

struct RosterEntry {
  Mac mac;
  StatusBody status;
  uint32_t lastSeen;
  bool online;
  bool dirty;          // changed since last published
  uint16_t restarts;   // uptime went backwards while we watched
  uint16_t dropouts;   // went silent for kOfflineMs
};

class Roster {
 public:
  static constexpr size_t kCapacity = 40;
  static constexpr uint32_t kOfflineMs = 10000;
  static constexpr uint32_t kGoneMs = 10UL * 60 * 1000;  // offline this long -> dropped

  // Adds or updates a bug. Uptime-only changes don't mark it dirty. Returns false if full.
  bool upsert(const Mac& mac, const StatusBody& status, uint32_t now);
  // Marks bugs offline after kOfflineMs of silence.
  void sweep(uint32_t now);
  const RosterEntry* find(const Mac& mac) const;

  size_t size() const { return count_; }
  const RosterEntry& at(size_t i) const { return entries_[i]; }

  // Offers up to `limit` dirty entries to fn(entry); fn returns false when it can't take
  // more right now. Accepted entries are cleared. Rotates so no bug is starved.
  template <typename Fn>
  void drainDirty(Fn fn, size_t limit) {
    for (size_t n = 0; n < count_ && limit > 0; n++) {
      RosterEntry& e = entries_[(cursor_ + n) % count_];
      if (!e.dirty) continue;
      if (!fn(e)) return;
      e.dirty = false;
      limit--;
    }
    cursor_ = count_ ? (cursor_ + 1) % count_ : 0;
  }

  // Drops bugs offline for kGoneMs and calls fn(mac) for each.
  template <typename Fn>
  void removeGone(uint32_t now, Fn fn) {
    size_t kept = 0;
    for (size_t i = 0; i < count_; i++) {
      const RosterEntry& e = entries_[i];
      if (!e.online && now - e.lastSeen > kGoneMs) {
        fn(e.mac);
        continue;
      }
      entries_[kept++] = e;
    }
    count_ = kept;
    if (cursor_ >= count_) cursor_ = 0;
  }

 private:
  RosterEntry entries_[kCapacity];
  size_t count_ = 0;
  size_t cursor_ = 0;
};

}  // namespace ff
