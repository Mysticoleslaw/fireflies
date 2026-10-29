#pragma once
#include <Adafruit_NeoPixel.h>
#include <core_esp8266_waveform.h>

#include "config.h"
#include "firefly.h"
#include "settings.h"

// One firefly's body: the servo arm hops 180° at random intervals, the LED flashes and fades,
// then it glows like a firefly (bursts of soft pulses) until the next hop.
class Hopper {
 public:
  void begin(const ff::Settings& settings, uint32_t now);
  void setSettings(const ff::Settings& settings, uint32_t now);
  void loop(uint32_t now);

  void hopNow(uint32_t now);
  void hopAfter(uint32_t now, uint32_t delayMs);
  void identify(uint32_t now);

  bool lit() const { return lit_; }
  uint32_t colour() const { return colour_; }  // 0xRRGGBB of the last hop
  // True once after the LED turned on/off or changed colour.
  bool takeChanged();

 private:
  void hop(uint32_t now);
  void scheduleNext(uint32_t now);
  void driveServo();
  uint16_t targetUs() const;
  void advanceSwing(uint32_t now);
  void driveServoUs(uint32_t us, uint32_t runUs);
  bool servoRunning(uint32_t nowUs) const;
  void waitForQuietServo();
  uint8_t envelope(uint32_t now) const;
  void updateLed(uint32_t now);
  void show(uint32_t rgb, uint8_t level);
  uint32_t pickColour() const;
  bool glowing() const;
  uint32_t hopColour(uint32_t now) const;
  uint32_t glowColour();
  uint8_t glowLevel(uint32_t now);

  ff::Settings settings_{};
  Adafruit_NeoPixel pixel_{cfg::kLedCount, cfg::kLedPin, NEO_GRB + NEO_KHZ800};

  bool atB_ = false;
  bool servoReady_ = false;
  bool swinging_ = false;
  uint32_t swingStart_ = 0;
  uint16_t fromUs_ = 0, toUs_ = 0;
  uint16_t servoUs_ = 0;        // pulse width currently being sent
  bool servoActive_ = false;
  uint32_t servoT0Us_ = 0;      // when the current waveform started (its pulses are phase-locked to this)
  uint32_t servoUntilUs_ = 0;   // when it stops (0 = runs until told otherwise)
  bool hopped_ = false;      // LED envelope running since lastHop_
  bool pending_ = false;     // a delayed hop (ripple) is queued
  bool identifying_ = false;
  uint32_t pendingAt_ = 0;
  uint32_t lastHop_ = 0;
  uint32_t nextHop_ = 0;
  uint32_t identifyAt_ = 0;
  uint32_t lastFrame_ = 0;

  ff::FireflyGlow glow_;
  uint32_t lastBurst_ = 0;
  uint32_t burstColour_ = 0;
  uint16_t hopHue_ = 0;
  uint32_t colour_ = 0;
  uint32_t nextColour_ = 0;
  bool colourPending_ = false;
  bool lit_ = false;
  bool changed_ = false;
  uint32_t shownRgb_ = 0;
  uint8_t shownLevel_ = 0;
  uint32_t shownAt_ = 0;
};
