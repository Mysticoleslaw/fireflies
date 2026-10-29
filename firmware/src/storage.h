#pragma once
#include "settings.h"

// Settings (and a Wi-Fi network entered in the app) persisted in LittleFS.
// The web app lives there too, under /www.
namespace storage {

// A network the user added from the app; tried before the one compiled into secrets.h.
struct WifiCreds {
  char ssid[33];
  char password[65];
};

bool begin();
// Returns saved settings, or defaults for this bug if nothing valid is stored.
ff::Settings load(const ff::Mac& self);
bool save(const ff::Settings& settings);

// Network choices made in the app, kept on the master.
struct NetPrefs {
  uint8_t offline;        // "Disconnect from Wi-Fi": stay on the hotspot until reconnected
  uint8_t ignoreBuiltIn;  // the network compiled into secrets.h was deleted in the app
};

bool loadWifi(WifiCreds& out);
bool saveWifi(const WifiCreds& creds);
void forgetWifi();
NetPrefs loadNetPrefs();
bool saveNetPrefs(const NetPrefs& prefs);

}  // namespace storage
