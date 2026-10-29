#include "hopper.h"

namespace {

constexpr uint32_t kWhite = 0xffffff;
constexpr uint32_t kServoPeriodUs = 20000;  // standard 50 Hz servo frame
constexpr uint32_t kLedRefreshMs = 1000;
constexpr uint16_t kHueSteps = 1536;  // hueToRgb's full colour wheel
constexpr uint32_t kFirstGlowMs = 2000;         // after boot / switching the glow on
constexpr uint32_t kGlowAfterLandingMs = 300;   // 0.3..1.2 s after the hop flash fades

uint32_t hueToRgb(uint16_t hue) {  // 0..1535, full saturation
  uint8_t pos = hue & 0xff;
  switch (hue >> 8) {
    case 0: return 0xff0000 | pos << 8;
    case 1: return (uint32_t)(255 - pos) << 16 | 0x00ff00;
    case 2: return 0x00ff00 | pos;
    case 3: return (uint32_t)(255 - pos) << 8 | 0x0000ff;
    case 4: return (uint32_t)pos << 16 | 0x0000ff;
    default: return 0xff0000 | (255 - pos);
  }
}

uint8_t scale(uint8_t channel, uint8_t level) { return (uint16_t)channel * level / 255; }

}  // namespace

void Hopper::begin(const ff::Settings& settings, uint32_t now) {
  pixel_.begin();
  show(0, 0);
  pinMode(cfg::kOnboardLedPin, OUTPUT);
  digitalWrite(cfg::kOnboardLedPin, settings.onboardLed ? LOW : HIGH);
  pinMode(cfg::kServoPin, OUTPUT);
  digitalWrite(cfg::kServoPin, LOW);
  settings_ = settings;
  scheduleNext(now);
  servoReady_ = true;
  driveServo();  // start from a known end (A)
  if (glowing()) glow_.startAt(now, kFirstGlowMs);
}

void Hopper::setSettings(const ff::Settings& settings, uint32_t now) {
  bool resumed = settings.hopping && !settings_.hopping;
  bool timingChanged = settings.minWait != settings_.minWait || settings.maxWait != settings_.maxWait;
  bool aChanged = settings.pulseA != settings_.pulseA, bChanged = settings.pulseB != settings_.pulseB;
  bool holdChanged = settings.holdServo != settings_.holdServo;
  bool wasGlowing = glowing();
  settings_ = settings;
  if (resumed || timingChanged) scheduleNext(now);
  if (!settings_.hopping) hopped_ = false;  // paused bugs go dark
  digitalWrite(cfg::kOnboardLedPin, settings_.onboardLed ? LOW : HIGH);  // active low
  // Calibration: show the end being adjusted, so you can watch the angle as you tune it.
  if (aChanged) atB_ = false;
  else if (bChanged) atB_ = true;
  if (servoReady_ && (aChanged || bChanged || holdChanged)) driveServo();
  if (!glowing()) glow_.stop();
  else if (!wasGlowing) glow_.startAt(now, kFirstGlowMs);
}

void Hopper::loop(uint32_t now) {
  if (pending_ && (int32_t)(now - pendingAt_) >= 0) {
    pending_ = false;
    hop(now);
  } else if (settings_.hopping && (int32_t)(now - nextHop_) >= 0) {
    hop(now);
  }
  if (now - lastFrame_ >= cfg::kFrameMs) {
    lastFrame_ = now;
    updateLed(now);
  }
}

void Hopper::hopNow(uint32_t now) { hop(now); }

void Hopper::hopAfter(uint32_t now, uint32_t delayMs) {
  pending_ = true;
  pendingAt_ = now + delayMs;
}

void Hopper::identify(uint32_t now) {
  identifying_ = true;
  identifyAt_ = now;
}

bool Hopper::takeChanged() {
  bool c = changed_;
  changed_ = false;
  return c;
}

void Hopper::hop(uint32_t now) {
  atB_ = !atB_;
  driveServo();
  hopHue_ = random(kHueSteps);
  nextColour_ = settings_.rainbowHop ? hueToRgb(hopHue_) : pickColour();  // shown after the LED delay
  colourPending_ = true;
  lastHop_ = now;
  hopped_ = true;
  scheduleNext(now);
  // Once it has landed and the hop flash has faded, start glowing like a firefly.
  uint32_t landed = settings_.ledDelayMs + settings_.swingMs + settings_.fadeMs;
  if (glowing()) glow_.startAt(now, landed + kGlowAfterLandingMs + random(kGlowAfterLandingMs * 3));
}

// Firefly glow runs while hopping is on, the glow is enabled, and the LED isn't held on.
bool Hopper::glowing() const { return settings_.fireflyGlow && settings_.hopping && !settings_.stayLit; }

uint8_t Hopper::glowLevel(uint32_t now) {
  if (!glowing()) return 0;
  ff::GlowParams p{settings_.glowPulsesMin,   settings_.glowPulses,   settings_.glowPulseMinMs,
                   settings_.glowPulseMs,     settings_.glowEveryMinSec, settings_.glowEverySec,
                   settings_.glowBrightMin,   settings_.glowBrightMax};
  // The glow has its own per-pulse brightness range (separate from the hop flash's brightness).
  return glow_.level(now, p, [](uint32_t n) -> uint32_t { return n ? random(n) : 0; });
}

void Hopper::scheduleNext(uint32_t now) {
  uint16_t lo = min(settings_.minWait, settings_.maxWait), hi = max(settings_.minWait, settings_.maxWait);
  nextHop_ = now + 1000UL * random(lo, hi + 1);
}

// Drives the servo signal straight from the core's waveform generator rather than the Servo
// library: Servo::detach() cut a pulse short, and that runt pulse made the arm snap back to
// end A. With a run time, the waveform stops at the end of a full period, so the servo
// gets clean pulses for swingMs and then silence (released: no hum, less power).
// holdServo keeps pulsing instead, for servos that drift or arms that get pushed by wind.
void Hopper::driveServo() {
  uint32_t us = constrain(atB_ ? settings_.pulseB : settings_.pulseA, cfg::kServoMinUs, cfg::kServoMaxUs);
  uint32_t runUs = settings_.holdServo ? 0 : (uint32_t)settings_.swingMs * 1000;
  startWaveform(cfg::kServoPin, us, kServoPeriodUs - us, runUs);
}

// LED level after a hop: dark for ledDelayMs (servo lag; Stay lit keeps the old colour up),
// then full for swingMs, then a linear fade (or stay on with stayLit).
uint8_t Hopper::envelope(uint32_t now) const {
  if (!hopped_) return 0;
  uint8_t full = (uint16_t)settings_.brightness * 255 / 100;
  uint32_t t = now - lastHop_;
  if (t < settings_.ledDelayMs) return settings_.stayLit && lit_ ? full : 0;
  t -= settings_.ledDelayMs;
  if (t < settings_.swingMs || settings_.stayLit) return full;
  uint32_t fadeT = t - settings_.swingMs;
  if (fadeT >= settings_.fadeMs) return 0;
  return full - (uint32_t)full * fadeT / max<uint16_t>(1, settings_.fadeMs);
}

void Hopper::updateLed(uint32_t now) {
  if (identifying_ && now - identifyAt_ < cfg::kIdentifyMs) {
    bool on = ((now - identifyAt_) / 200) % 2 == 0;
    show(kWhite, on ? 255 : 0);
    return;
  }
  identifying_ = false;
  bool delayOver = now - lastHop_ >= settings_.ledDelayMs;
  if (colourPending_ && delayOver) {
    colour_ = nextColour_;
    colourPending_ = false;
    changed_ = true;
  }
  uint8_t level = envelope(now);
  if (level == 0 && delayOver) hopped_ = false;
  if ((level > 0) != lit_) {
    lit_ = level > 0;
    changed_ = true;
  }
  if (level > 0) return show(hopColour(now), level);
  // Between hops: firefly glow. Not reported as "lit": 35 blinking bugs would flood the radio.
  uint8_t glow = glowLevel(now);
  show(glow ? glowColour() : colour_, glow);
}

// Rainbow jump: from a random hue, sweep the whole wheel during the swing, landing back on it.
uint32_t Hopper::hopColour(uint32_t now) const {
  if (!settings_.rainbowHop) return colour_;
  uint32_t t = now - lastHop_;
  if (t < settings_.ledDelayMs || t - settings_.ledDelayMs >= settings_.swingMs) return colour_;
  uint32_t step = (uint32_t)kHueSteps * (t - settings_.ledDelayMs) / settings_.swingMs;
  return hueToRgb((hopHue_ + step) % kHueSteps);
}

// Glow colour: fixed, a new random one for each burst, or the colour of the last jump.
uint32_t Hopper::glowColour() {
  if (settings_.glowColourMode == ff::kGlowMatchJump) return colour_;
  if (settings_.glowColourMode != ff::kGlowRandomColour) {
    return (uint32_t)settings_.glowR << 16 | (uint32_t)settings_.glowG << 8 | settings_.glowB;
  }
  if (glow_.bursts() != lastBurst_) {
    lastBurst_ = glow_.bursts();
    burstColour_ = hueToRgb(random(kHueSteps));
  }
  return burstColour_;
}

// Wi-Fi and servo-timer interrupts can occasionally garble a WS2812 frame (e.g. a stuck-on
// LED after a fade), so resend the current state every kLedRefreshMs even if unchanged.
void Hopper::show(uint32_t rgb, uint8_t level) {
  uint32_t now = millis();
  bool same = rgb == shownRgb_ && level == shownLevel_;
  if (same && now - shownAt_ < kLedRefreshMs) return;
  shownRgb_ = rgb;
  shownLevel_ = level;
  shownAt_ = now;
  pixel_.fill(pixel_.Color(scale(rgb >> 16, level), scale(rgb >> 8 & 0xff, level), scale(rgb & 0xff, level)));
  pixel_.show();
}

uint32_t Hopper::pickColour() const {
  if (settings_.randomColour) return hueToRgb(random(kHueSteps));
  return (uint32_t)settings_.r << 16 | (uint32_t)settings_.g << 8 | settings_.b;
}
