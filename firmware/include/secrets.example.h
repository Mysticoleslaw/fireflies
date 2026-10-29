#pragma once
// Copy to secrets.h (git-ignored) and fill in. Every bug gets the same file.

// Home Wi-Fi the master joins when it's in range. Leave empty to always use the hotspot.
#define FF_WIFI_SSID ""
#define FF_WIFI_PASSWORD ""

// Password for the "Fireflies" hotspot the master makes when there's no home Wi-Fi (8+ chars).
#define FF_HOTSPOT_PASSWORD "change-me-please"

// Password for over-the-air updates.
#define FF_OTA_PASSWORD "change-me-too"

// Bugs only listen to bugs with the same group id. Change it if a neighbour runs Fireflies too.
#define FF_GROUP_ID 0x4742
