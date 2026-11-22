#pragma once
#include <stdint.h>

#include "protocol.h"

// Follower: joins the master's network (home Wi-Fi, or its Fireflies hotspot) and downloads
// the master's firmware, which the master only hands over if it's newer than ours. On success
// the bug reboots onto it; otherwise it drops the connection and carries on. The mesh keeps
// working meanwhile (same channel, AP stays up).
namespace fetch {

// False if already fetching or the network's password isn't known.
bool start(uint32_t now, ff::NetMode masterMode, uint32_t masterIp);
// Call every loop. Waits while `armBusy`, so the reboot can't land mid-swing.
void loop(uint32_t now, bool armBusy);

}  // namespace fetch
