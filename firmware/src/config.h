#pragma once
#include <stddef.h>
#include <stdint.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.example.h to include/secrets.h and fill it in"
#endif

namespace cfg {

// Wiring (D1 mini)
constexpr uint8_t kServoPin = 5;  // D1
constexpr uint8_t kLedPin = 4;    // D2 (moved off D4, which the blue onboard LED shares)
constexpr uint8_t kOnboardLedPin = 2;  // D4: blue LED on the D1 mini, lit when LOW
constexpr uint8_t kLedCount = 2;  // folded back-to-back, both show the same colour
constexpr uint16_t kServoMinUs = 400;   // matches the pulseA/pulseB setting limits
constexpr uint16_t kServoMaxUs = 2600;
// A0 reading at 1023: 1.0 V x (180k + 220k + 100k) / 100k. Tweak if your multimeter disagrees.
constexpr uint16_t kBatteryFullScaleMv = 5000;

// Network
constexpr const char* kHostname = "fireflies";  // fireflies.local, master only
constexpr const char* kHotspotSsid = "Fireflies";
constexpr uint8_t kDefaultChannel = 1;        // used when home Wi-Fi isn't around
constexpr uint8_t kHotspotMaxClients = 4;
constexpr uint32_t kWifiConnectMs = 15000;

// Mesh timing
constexpr uint32_t kBeaconMs = 500;
constexpr uint32_t kStatusMs = 3000;
constexpr uint32_t kSearchMs = 3000;
constexpr uint32_t kSearchJitterMs = 7000;  // so bugs switched on together don't all claim master
constexpr uint32_t kStatusJitterMs = 400;   // spread status reports so bugs don't all transmit at once
constexpr uint32_t kChannelDwellMs = 700;   // per channel while searching (beacons every 500 ms)
constexpr size_t kPublishPerLoop = 3;
constexpr uint16_t kHoldSeconds = 45;         // followers wait this long for a restarting master
constexpr uint32_t kRestartDelayMs = 400;     // time to get the Hold broadcast out first       // live updates per loop; the ESP8266 queues only 8 per app
constexpr uint8_t kCommandRepeats = 3;      // broadcasts aren't acknowledged, so send a few
constexpr uint32_t kCommandGapMs = 30;

// Behaviour
constexpr uint32_t kSaveDelayMs = 2000;  // batch flash writes
constexpr uint32_t kIdentifyMs = 1200;   // 3 white flashes
constexpr uint32_t kFrameMs = 20;        // LED update rate while fading

}  // namespace cfg
