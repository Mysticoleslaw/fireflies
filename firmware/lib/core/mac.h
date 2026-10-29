#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace ff {

struct Mac {
  uint8_t b[6];
};

inline bool operator==(const Mac& x, const Mac& y) { return memcmp(x.b, y.b, 6) == 0; }
inline bool operator!=(const Mac& x, const Mac& y) { return !(x == y); }
inline bool operator<(const Mac& x, const Mac& y) { return memcmp(x.b, y.b, 6) < 0; }

constexpr Mac kBroadcast = {{0xff, 0xff, 0xff, 0xff, 0xff, 0xff}};

// "a1b2c3d4e5f6" (12 chars + terminator)
inline void macToHex(const Mac& mac, char out[13]) {
  snprintf(out, 13, "%02x%02x%02x%02x%02x%02x",
           mac.b[0], mac.b[1], mac.b[2], mac.b[3], mac.b[4], mac.b[5]);
}

inline int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

inline bool macFromHex(const char* s, Mac& out) {
  if (!s || strlen(s) != 12) return false;
  Mac mac{};
  for (int i = 0; i < 6; i++) {
    int hi = hexNibble(s[2 * i]), lo = hexNibble(s[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    mac.b[i] = (uint8_t)(hi << 4 | lo);
  }
  out = mac;
  return true;
}

}  // namespace ff
