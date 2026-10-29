#pragma once
#include <stdint.h>
#include "mac.h"

namespace ff {

constexpr size_t kNameLen = 20;  // 19 chars + terminator
constexpr int16_t kPosUnknown = INT16_MIN;

// Per-bug settings. Packed: sent as-is over ESP-NOW and stored in flash.
struct __attribute__((packed)) Settings {
  char name[kNameLen];
  uint8_t hopping;
  uint8_t randomColour;
  uint8_t stayLit;
  uint8_t holdServo;     // keep driving the servo between hops (vs. release it)
  uint8_t r, g, b;      // fixed colour
  uint8_t brightness;   // %
  uint16_t minWait;     // s
  uint16_t maxWait;     // s
  uint16_t swingMs;
  uint16_t fadeMs;
  uint16_t pulseA;      // µs
  uint16_t pulseB;      // µs
  int16_t posX;         // reserved for position mapping
  int16_t posY;
  uint8_t onboardLed;    // D1 mini's blue LED (GPIO2). Keep new fields last: old saves stay a prefix.
  uint16_t ledDelayMs;   // wait after the servo starts before the LED lights (servo lag)
  uint8_t fireflyGlow;   // blink like a firefly between hops
  uint8_t glowPulses;    // max pulses per burst (min: glowPulsesMin)
  uint16_t glowPulseMs;  // max length of one pulse, fade up + down (min: glowPulseMinMs)
  uint16_t glowEverySec; // max gap between bursts (min: glowEveryMinSec)
  uint8_t glowR, glowG, glowB;
  uint8_t rainbowHop;    // sweep the whole colour wheel during the jump
  uint8_t glowColourMode;  // GlowColourMode
  uint8_t glowPulsesMin;
  uint16_t glowPulseMinMs;
  uint16_t glowEveryMinSec;
  uint8_t glowBrightMin;   // % per pulse: random in [min, max]; min == max = fixed
  uint8_t glowBrightMax;
};

enum SettingField : uint32_t {
  kName = 1 << 0,
  kHopping = 1 << 1,
  kRandomColour = 1 << 2,
  kStayLit = 1 << 3,
  kColour = 1 << 4,
  kBrightness = 1 << 5,
  kMinWait = 1 << 6,
  kMaxWait = 1 << 7,
  kSwing = 1 << 8,
  kFade = 1 << 9,
  kPulseA = 1 << 10,
  kPulseB = 1 << 11,
  kPosition = 1 << 12,
  kHoldServo = 1 << 13,
  kOnboardLed = 1 << 14,
  kLedDelay = 1 << 15,
  kFireflyGlow = 1UL << 16,
  kGlowPulses = 1UL << 17,
  kGlowPulseMs = 1UL << 18,
  kGlowEvery = 1UL << 19,
  kGlowColour = 1UL << 20,
  kRainbowHop = 1UL << 21,
  kGlowColourMode = 1UL << 22,
  kGlowPulsesMin = 1UL << 23,
  kGlowPulseMin = 1UL << 24,
  kGlowEveryMin = 1UL << 25,
  kGlowBrightMin = 1UL << 26,
  kGlowBrightMax = 1UL << 27,
};
constexpr uint32_t kAllFields = (1UL << 28) - 1;

// Colour of the firefly glow: the fixed glowColour, a random one per burst, or the jump's colour.
enum GlowColourMode : uint8_t { kGlowFixed = 0, kGlowRandomColour = 1, kGlowMatchJump = 2 };

const char* glowModeName(uint8_t mode);  // "fixed" | "random" | "jump"
Settings defaultSettings(const Mac& mac);
Settings clampSettings(Settings s);
Settings applyMasked(const Settings& base, const Settings& patch, uint32_t mask);

// Parses one API query param (e.g. "minWait", "12") into patch and sets its bit in mask.
// Returns nullptr on success, otherwise an error message.
const char* parseSettingParam(const char* key, const char* value, Settings& patch, uint32_t& mask);

}  // namespace ff
