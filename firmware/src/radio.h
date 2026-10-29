#pragma once
#include <stddef.h>
#include <stdint.h>

#include "mac.h"
#include "protocol.h"

// ESP-NOW broadcast send/receive. Received packets are queued and read from loop().
struct RxPacket {
  ff::Mac from;
  uint8_t len;
  uint8_t data[ff::kMaxPacket];
};

namespace radio {

bool begin(uint8_t channel);
// Moves ESP-NOW to another channel (only while the Wi-Fi station isn't connected).
void setChannel(uint8_t channel);
uint8_t channel();
// False while the previous packet is still going out. Send at most one packet per ready().
bool ready();
// Returns false if busy or the send failed (callers retry later).
bool send(const void* data, size_t len);
bool receive(RxPacket& out);
// The MAC this bug transmits from (its softAP interface): its id everywhere.
ff::Mac selfMac();

}  // namespace radio
