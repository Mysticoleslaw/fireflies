#pragma once
#include <stdint.h>

// Firefly glow between hops: bursts of soft pulses (fade up, fade down) at random times.
// Everything is a range picked fresh each time (pulses per burst, each pulse's length,
// the wait between bursts), so a swarm never falls into a pattern.
// Pure logic so it can be unit-tested; the caller supplies the clock and randomness.
namespace ff {

struct GlowParams {
  uint8_t minPulses, maxPulses;     // pulses per burst
  uint16_t minPulseMs, maxPulseMs;  // each pulse, fade up + fade down
  uint16_t minEverySec, maxEverySec;  // wait between bursts
  uint8_t minBright = 100, maxBright = 100;  // % per pulse
};

class FireflyGlow {
 public:
  using Rng = uint32_t (*)(uint32_t maxExclusive);  // returns 0..maxExclusive-1

  // Stop glowing (e.g. a new hop started, or glow switched off).
  void stop() { active_ = false; }
  // The bug has landed: first burst after `delayMs`, then at random intervals.
  void startAt(uint32_t now, uint32_t delayMs);
  // Brightness 0..255 for this moment (pulse shape x this pulse's brightness); advances the schedule.
  uint8_t level(uint32_t now, const GlowParams& p, Rng rng);
  // Increments when a burst starts (lets the caller pick a new colour per burst).
  uint32_t bursts() const { return bursts_; }

 private:
  void nextPulse(uint32_t now, const GlowParams& p, Rng rng);

  bool active_ = false;
  bool inBurst_ = false;
  uint32_t nextBurstAt_ = 0;
  uint32_t pulseStart_ = 0;
  uint32_t pulseMs_ = 0;
  uint32_t pulseBright_ = 100;  // %
  uint32_t gapMs_ = 0;
  uint8_t pulsesLeft_ = 0;
  uint32_t bursts_ = 0;
};

// Random value in [lo, hi] (the bounds may be given in either order).
uint32_t pickInRange(uint32_t lo, uint32_t hi, FireflyGlow::Rng rng);
// Smooth pulse shape: 0 at the ends, 255 in the middle (sin^2 curve).
uint8_t pulseShape(uint32_t t, uint32_t lengthMs);

}  // namespace ff
