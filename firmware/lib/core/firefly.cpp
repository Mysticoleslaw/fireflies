#include "firefly.h"

#include <math.h>

namespace ff {

uint32_t pickInRange(uint32_t lo, uint32_t hi, FireflyGlow::Rng rng) {
  if (lo > hi) {
    uint32_t t = lo;
    lo = hi;
    hi = t;
  }
  return lo + rng(hi - lo + 1);
}

uint8_t pulseShape(uint32_t t, uint32_t lengthMs) {
  if (lengthMs == 0 || t >= lengthMs) return 0;
  float s = sinf(3.14159265f * (float)t / (float)lengthMs);
  return (uint8_t)(s * s * 255.0f + 0.5f);
}

void FireflyGlow::startAt(uint32_t now, uint32_t delayMs) {
  active_ = true;
  inBurst_ = false;
  nextBurstAt_ = now + delayMs;
}

// Each pulse gets its own length and brightness, then a pause of 0.6x..1.2x its length.
void FireflyGlow::nextPulse(uint32_t now, const GlowParams& p, Rng rng) {
  pulseStart_ = now;
  pulseMs_ = pickInRange(p.minPulseMs, p.maxPulseMs, rng);
  pulseBright_ = pickInRange(p.minBright, p.maxBright, rng);
  gapMs_ = pulseMs_ * (6 + rng(7)) / 10;
}

uint8_t FireflyGlow::level(uint32_t now, const GlowParams& p, Rng rng) {
  if (!active_) return 0;
  if (!inBurst_) {
    if ((int32_t)(now - nextBurstAt_) < 0) return 0;
    inBurst_ = true;
    bursts_++;
    pulsesLeft_ = (uint8_t)pickInRange(p.minPulses ? p.minPulses : 1, p.maxPulses ? p.maxPulses : 1, rng);
    nextPulse(now, p, rng);
  }
  uint32_t t = now - pulseStart_;
  if (t < pulseMs_) return (uint8_t)(pulseShape(t, pulseMs_) * pulseBright_ / 100);
  if (t < pulseMs_ + gapMs_) return 0;
  if (--pulsesLeft_ > 0) {
    nextPulse(now, p, rng);
    return 0;
  }
  inBurst_ = false;
  nextBurstAt_ = now + 1000UL * pickInRange(p.minEverySec, p.maxEverySec, rng);
  return 0;
}

}  // namespace ff
