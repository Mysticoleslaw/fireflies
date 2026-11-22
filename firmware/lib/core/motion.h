#pragma once
#include <stdint.h>

// Servo swing: instead of jumping straight to the target (full servo speed), the pulse
// width is moved from `fromUs` to `toUs` over `lengthMs` with an ease-in/ease-out curve,
// so "Swing time" really sets how fast the arm swings, and it starts and lands gently.
namespace ff {

// Smoothstep easing, 0..1000 in -> 0..1000 out (slow start, fast middle, slow landing).
inline uint32_t easeInOut(uint32_t permille) {
  if (permille >= 1000) return 1000;
  uint64_t x = permille;
  return (uint32_t)((3 * x * x * 1000 - 2 * x * x * x) / 1000000);
}

// Pulse width `t` ms into a swing of `lengthMs` from `fromUs` to `toUs`.
inline uint16_t swingPulse(uint16_t fromUs, uint16_t toUs, uint32_t t, uint32_t lengthMs) {
  if (lengthMs == 0 || t >= lengthMs) return toUs;
  int32_t span = (int32_t)toUs - (int32_t)fromUs;
  return (uint16_t)(fromUs + span * (int32_t)easeInOut(t * 1000 / lengthMs) / 1000);
}

// Even a tiny calibration nudge eases rather than jumps.
constexpr uint32_t kMinMoveMs = 150;

// How long a move from `fromUs` to `toUs` should take so the arm never goes faster than a
// hop: moving the full A..B range takes `swingMs`, shorter moves a proportional share.
inline uint32_t moveLengthMs(uint16_t fromUs, uint16_t toUs, uint16_t pulseA, uint16_t pulseB, uint32_t swingMs) {
  uint32_t dist = fromUs > toUs ? fromUs - toUs : toUs - fromUs;
  if (dist == 0) return 0;
  uint32_t range = pulseA > pulseB ? pulseA - pulseB : pulseB - pulseA;
  if (range == 0 || dist >= range) return swingMs;
  uint32_t ms = swingMs * dist / range;
  return ms < kMinMoveMs ? kMinMoveMs : ms;
}

}  // namespace ff
