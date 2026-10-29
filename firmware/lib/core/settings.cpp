#include "settings.h"

#include <stdlib.h>
#include <string.h>

namespace ff {
namespace {

struct NumberField {
  const char* key;
  uint32_t field;
  long min;
  long max;
};

const NumberField kNumbers[] = {
    {"brightness", kBrightness, 5, 100}, {"minWait", kMinWait, 1, 600},
    {"maxWait", kMaxWait, 1, 600},       {"swingMs", kSwing, 100, 2000},
    {"fadeMs", kFade, 0, 5000},          {"pulseA", kPulseA, 400, 1500},
    {"pulseB", kPulseB, 1500, 2600},     {"ledDelayMs", kLedDelay, 0, 1000},
    {"glowPulsesMin", kGlowPulsesMin, 1, 10},   {"glowPulsesMax", kGlowPulses, 1, 10},
    {"glowPulseMinMs", kGlowPulseMin, 50, 3000}, {"glowPulseMaxMs", kGlowPulseMs, 50, 3000},
    {"glowEveryMinSec", kGlowEveryMin, 0, 600},  {"glowEveryMaxSec", kGlowEvery, 0, 600},
    {"glowBrightMin", kGlowBrightMin, 1, 100},   {"glowBrightMax", kGlowBrightMax, 1, 100},
};

struct BoolField {
  const char* key;
  uint32_t field;
};

const BoolField kBools[] = {
    {"hopping", kHopping}, {"randomColour", kRandomColour}, {"stayLit", kStayLit}, {"holdServo", kHoldServo}, {"onboardLed", kOnboardLed}, {"fireflyGlow", kFireflyGlow},
    {"rainbowHop", kRainbowHop}};

long clampLong(long v, long lo, long hi) { return v < lo ? lo : (v > hi ? hi : v); }

long getNumber(const Settings& s, uint32_t field) {
  switch (field) {
    case kBrightness: return s.brightness;
    case kMinWait: return s.minWait;
    case kMaxWait: return s.maxWait;
    case kSwing: return s.swingMs;
    case kFade: return s.fadeMs;
    case kPulseA: return s.pulseA;
    case kPulseB: return s.pulseB;
    case kLedDelay: return s.ledDelayMs;
    case kGlowPulses: return s.glowPulses;
    case kGlowPulseMs: return s.glowPulseMs;
    case kGlowEvery: return s.glowEverySec;
    case kGlowPulsesMin: return s.glowPulsesMin;
    case kGlowPulseMin: return s.glowPulseMinMs;
    case kGlowEveryMin: return s.glowEveryMinSec;
    case kGlowBrightMin: return s.glowBrightMin;
    case kGlowBrightMax: return s.glowBrightMax;
    default: return 0;
  }
}

void setNumber(Settings& s, uint32_t field, long v) {
  switch (field) {
    case kBrightness: s.brightness = (uint8_t)v; break;
    case kMinWait: s.minWait = (uint16_t)v; break;
    case kMaxWait: s.maxWait = (uint16_t)v; break;
    case kSwing: s.swingMs = (uint16_t)v; break;
    case kFade: s.fadeMs = (uint16_t)v; break;
    case kPulseA: s.pulseA = (uint16_t)v; break;
    case kPulseB: s.pulseB = (uint16_t)v; break;
    case kLedDelay: s.ledDelayMs = (uint16_t)v; break;
    case kGlowPulses: s.glowPulses = (uint8_t)v; break;
    case kGlowPulseMs: s.glowPulseMs = (uint16_t)v; break;
    case kGlowEvery: s.glowEverySec = (uint16_t)v; break;
    case kGlowPulsesMin: s.glowPulsesMin = (uint8_t)v; break;
    case kGlowPulseMin: s.glowPulseMinMs = (uint16_t)v; break;
    case kGlowEveryMin: s.glowEveryMinSec = (uint16_t)v; break;
    case kGlowBrightMin: s.glowBrightMin = (uint8_t)v; break;
    case kGlowBrightMax: s.glowBrightMax = (uint8_t)v; break;
  }
}

void setBool(Settings& s, uint32_t field, bool v) {
  if (field == kHopping) s.hopping = v;
  if (field == kRandomColour) s.randomColour = v;
  if (field == kStayLit) s.stayLit = v;
  if (field == kHoldServo) s.holdServo = v;
  if (field == kOnboardLed) s.onboardLed = v;
  if (field == kFireflyGlow) s.fireflyGlow = v;
  if (field == kRainbowHop) s.rainbowHop = v;
}

const char* const kGlowModeNames[] = {"fixed", "random", "jump"};

const char* parseGlowMode(const char* value, Settings& patch) {
  for (uint8_t i = 0; i < 3; i++) {
    if (strcmp(value, kGlowModeNames[i]) != 0) continue;
    patch.glowColourMode = i;
    return nullptr;
  }
  return "glowColourMode must be fixed, random or jump";
}

const char* parseName(const char* value, Settings& patch) {
  size_t len = strlen(value);
  if (len == 0 || len >= kNameLen) return "name must be 1-19 characters";
  for (size_t i = 0; i < len; i++) {
    if (value[i] < 0x20 || value[i] > 0x7e) return "name must be plain text";
  }
  memset(patch.name, 0, kNameLen);
  memcpy(patch.name, value, len);
  return nullptr;
}

const char* parseColour(const char* value, uint8_t& r, uint8_t& g, uint8_t& b) {
  int n[6];
  if (strlen(value) != 6) return "colour must be rrggbb";
  for (int i = 0; i < 6; i++) {
    if ((n[i] = hexNibble(value[i])) < 0) return "colour must be rrggbb";
  }
  r = (uint8_t)(n[0] << 4 | n[1]);
  g = (uint8_t)(n[2] << 4 | n[3]);
  b = (uint8_t)(n[4] << 4 | n[5]);
  return nullptr;
}

}  // namespace

const char* glowModeName(uint8_t mode) { return mode <= kGlowMatchJump ? kGlowModeNames[mode] : "fixed"; }

Settings defaultSettings(const Mac& mac) {
  Settings s{};
  snprintf(s.name, kNameLen, "Firefly %02x%02x%02x", mac.b[3], mac.b[4], mac.b[5]);
  s.hopping = 1;
  s.randomColour = 1;
  s.stayLit = 0;
  s.holdServo = 0;
  s.onboardLed = 0;
  s.ledDelayMs = 100;  // a micro servo takes ~100 ms to visibly start moving
  s.fireflyGlow = 1;
  s.glowPulsesMin = 1, s.glowPulses = 6;          // pulses per burst
  s.glowPulseMinMs = 100, s.glowPulseMs = 600;     // each pulse
  s.glowEveryMinSec = 2, s.glowEverySec = 30;      // between bursts
  s.glowBrightMin = 40, s.glowBrightMax = 100;     // % per pulse
  s.glowR = 0xb4, s.glowG = 0xff, s.glowB = 0x28;  // firefly yellow-green
  s.rainbowHop = 0;
  s.glowColourMode = kGlowFixed;
  s.r = 0xff, s.g = 0xc9, s.b = 0x40;
  s.brightness = 100;
  s.minWait = 10;
  s.maxWait = 30;
  s.swingMs = 500;
  s.fadeMs = 1200;
  s.pulseA = 500;
  s.pulseB = 2500;
  s.posX = s.posY = kPosUnknown;
  return s;
}

Settings clampSettings(Settings s) {
  for (const auto& f : kNumbers) setNumber(s, f.field, clampLong(getNumber(s, f.field), f.min, f.max));
  s.hopping = s.hopping ? 1 : 0;
  s.randomColour = s.randomColour ? 1 : 0;
  s.stayLit = s.stayLit ? 1 : 0;
  s.holdServo = s.holdServo ? 1 : 0;
  s.onboardLed = s.onboardLed ? 1 : 0;
  s.fireflyGlow = s.fireflyGlow ? 1 : 0;
  s.rainbowHop = s.rainbowHop ? 1 : 0;
  if (s.glowColourMode > kGlowMatchJump) s.glowColourMode = kGlowFixed;
  s.name[kNameLen - 1] = '\0';
  return s;
}

Settings applyMasked(const Settings& base, const Settings& patch, uint32_t mask) {
  Settings s = base;
  if (mask & kName) memcpy(s.name, patch.name, kNameLen);
  if (mask & kHopping) s.hopping = patch.hopping;
  if (mask & kRandomColour) s.randomColour = patch.randomColour;
  if (mask & kStayLit) s.stayLit = patch.stayLit;
  if (mask & kHoldServo) s.holdServo = patch.holdServo;
  if (mask & kOnboardLed) s.onboardLed = patch.onboardLed;
  if (mask & kFireflyGlow) s.fireflyGlow = patch.fireflyGlow;
  if (mask & kRainbowHop) s.rainbowHop = patch.rainbowHop;
  if (mask & kGlowColourMode) s.glowColourMode = patch.glowColourMode;
  if (mask & kGlowColour) s.glowR = patch.glowR, s.glowG = patch.glowG, s.glowB = patch.glowB;
  if (mask & kColour) s.r = patch.r, s.g = patch.g, s.b = patch.b;
  for (const auto& f : kNumbers) {
    if (mask & f.field) setNumber(s, f.field, getNumber(patch, f.field));
  }
  if (mask & kPosition) s.posX = patch.posX, s.posY = patch.posY;
  return clampSettings(s);
}

const char* parseSettingParam(const char* key, const char* value, Settings& patch, uint32_t& mask) {
  if (!key || !value) return "missing value";
  for (const auto& f : kNumbers) {
    if (strcmp(key, f.key) != 0) continue;
    char* end = nullptr;
    long v = strtol(value, &end, 10);
    if (end == value || *end != '\0') return "expected a whole number";
    setNumber(patch, f.field, clampLong(v, f.min, f.max));
    mask |= f.field;
    return nullptr;
  }
  for (const auto& f : kBools) {
    if (strcmp(key, f.key) != 0) continue;
    bool on = !strcmp(value, "1") || !strcmp(value, "true");
    if (!on && strcmp(value, "0") && strcmp(value, "false")) return "expected 1 or 0";
    setBool(patch, f.field, on);
    mask |= f.field;
    return nullptr;
  }
  const char* err = "unknown setting";
  if (!strcmp(key, "colour") && !(err = parseColour(value, patch.r, patch.g, patch.b))) mask |= kColour;
  if (!strcmp(key, "glowColour") && !(err = parseColour(value, patch.glowR, patch.glowG, patch.glowB))) mask |= kGlowColour;
  if (!strcmp(key, "name") && !(err = parseName(value, patch))) mask |= kName;
  if (!strcmp(key, "glowColourMode") && !(err = parseGlowMode(value, patch))) mask |= kGlowColourMode;
  return err;
}

}  // namespace ff
