#pragma once
#include "protocol.h"
#include "roster.h"

// The master's HTTP API and web app (see ../API.md).
namespace gateway {

// Hands a validated command to the mesh; returns false if the queue is full.
using CommandSink = bool (*)(const ff::CommandPacket& command);

void begin(const ff::Roster& roster, const ff::Mac& self, CommandSink sink);
// Pushes one bug to connected apps as an SSE "bug" event.
// Returns false if the apps' queues are too full right now (try again later).
bool publish(const ff::RosterEntry& entry);
// Tells connected apps a bug was dropped from the roster (SSE "gone" event).
void gone(const ff::Mac& mac);

}  // namespace gateway
