#pragma once
#include <stdint.h>

// Battery voltage on A0. The D1 mini has a 220k/100k divider on A0 (3.2 V full scale);
// a 180k resistor from battery + to A0 stretches that to 5.0 V, enough for 3xAA / 4xNiMH.
namespace battery {

void loop(uint32_t now);
// Smoothed millivolts, rounded to 50 mV so tiny wobbles don't spam the app. 0 = not wired.
uint16_t millivolts();

}  // namespace battery
