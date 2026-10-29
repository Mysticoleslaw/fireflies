#include "battery.h"

#include <Arduino.h>

#include "config.h"

namespace battery {
namespace {

constexpr uint32_t kSampleMs = 2000;    // reading A0 too often disturbs ESP8266 Wi-Fi
constexpr uint16_t kNotWiredMv = 1000;  // below this, nothing is connected
constexpr uint16_t kStepMv = 50;

float smoothed = 0;
uint32_t lastSample = 0;
bool sampled = false;

}  // namespace

void loop(uint32_t now) {
  if (sampled && now - lastSample < kSampleMs) return;
  lastSample = now;
  float mv = analogRead(A0) * (float)cfg::kBatteryFullScaleMv / 1023.0f;
  smoothed = sampled ? smoothed * 0.8f + mv * 0.2f : mv;
  sampled = true;
}

uint16_t millivolts() {
  if (smoothed < kNotWiredMv) return 0;
  return (uint16_t)((smoothed + kStepMv / 2) / kStepMv) * kStepMv;
}

}  // namespace battery
