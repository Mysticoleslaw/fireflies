#pragma once
#include <stdint.h>

// Follower "update mode": briefly joins the home Wi-Fi (the network built into the
// firmware, so no password is ever broadcast) and opens ArduinoOTA, so the Mac can push
// new firmware to it. The mesh keeps working meanwhile (same channel, AP stays up).
namespace updater {

// Starts (or extends) update mode for `minutes`. False if no home network is known.
bool start(uint32_t now, uint16_t minutes);
// Runs OTA; returns true once the window has expired without an update (caller restarts).
bool loop(uint32_t now);
// IPv4 address while connected in update mode, else 0.
uint32_t ip();

}  // namespace updater
