#include "storage.h"

#include <LittleFS.h>

namespace storage {
namespace {

constexpr const char* kPath = "/settings.bin";
constexpr const char* kTempPath = "/settings.tmp";
constexpr uint8_t kVersion = 6;  // bump when Settings changes shape (append fields only)

// Older layouts are prefixes of the current one; fields they lack keep their defaults.
struct OldLayout {
  uint8_t version;
  size_t size;
};
constexpr size_t kV6Bytes = 9;    // rainbowHop, glowColourMode, glow range minimums, glow brightness range
constexpr size_t kGlowBytes = 9;  // fireflyGlow .. glowB
constexpr size_t kV5 = sizeof(ff::Settings) - kV6Bytes;
constexpr OldLayout kOldLayouts[] = {
    {5, kV5},                                                     // before rainbowHop / glow colour mode / glow ranges
    {4, kV5 - kGlowBytes},                                        // before firefly glow
    {3, kV5 - kGlowBytes - sizeof(uint16_t)},                     // before ledDelayMs
    {2, kV5 - kGlowBytes - sizeof(uint16_t) - sizeof(uint8_t)},  // before onboardLed
};

// Bugs still called by the old default ("Bug a1b2c3") become "Firefly a1b2c3"; custom names stay.
ff::Settings renameOldDefault(ff::Settings s, const ff::Mac& self) {
  char oldDefault[ff::kNameLen];
  snprintf(oldDefault, sizeof oldDefault, "Bug %02x%02x%02x", self.b[3], self.b[4], self.b[5]);
  if (strcmp(s.name, oldDefault) == 0) memcpy(s.name, ff::defaultSettings(self).name, ff::kNameLen);
  return s;
}
constexpr const char* kWifiPath = "/wifi.bin";
constexpr const char* kWifiTempPath = "/wifi.tmp";
constexpr uint8_t kWifiVersion = 1;
constexpr const char* kPrefsPath = "/netprefs.bin";
constexpr const char* kPrefsTempPath = "/netprefs.tmp";
constexpr uint8_t kPrefsVersion = 1;

// Reads [version byte][size bytes]; false if missing, short or a different version.
bool readVersioned(const char* path, uint8_t version, void* data, size_t size) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  bool ok = f.read() == version && f.read((uint8_t*)data, size) == (int)size;
  f.close();
  return ok;
}

// Writes a temp file, then renames it over the old one, so a power cut mid-save can't wipe it.
bool writeVersioned(const char* path, const char* tempPath, uint8_t version, const void* data, size_t size) {
  File f = LittleFS.open(tempPath, "w");
  if (!f) return false;
  bool ok = f.write(version) == 1 && f.write((const uint8_t*)data, size) == size;
  f.close();
  return ok && LittleFS.rename(tempPath, path);
}

}  // namespace

bool begin() { return LittleFS.begin(); }

ff::Settings load(const ff::Mac& self) {
  ff::Settings s;
  if (readVersioned(kPath, kVersion, &s, sizeof s)) return ff::clampSettings(s);
  for (const OldLayout& old : kOldLayouts) {
    s = ff::defaultSettings(self);
    if (readVersioned(kPath, old.version, &s, old.size)) return ff::clampSettings(renameOldDefault(s, self));
  }
  if (LittleFS.exists(kPath)) Serial.println(F("[storage] settings unreadable, using defaults"));
  return ff::defaultSettings(self);
}

bool save(const ff::Settings& settings) {
  bool ok = writeVersioned(kPath, kTempPath, kVersion, &settings, sizeof settings);
  if (!ok) Serial.println(F("[storage] settings write failed"));
  return ok;
}

bool loadWifi(WifiCreds& out) {
  WifiCreds c;
  if (!readVersioned(kWifiPath, kWifiVersion, &c, sizeof c)) return false;
  c.ssid[sizeof c.ssid - 1] = '\0';
  c.password[sizeof c.password - 1] = '\0';
  if (!c.ssid[0]) return false;
  out = c;
  return true;
}

bool saveWifi(const WifiCreds& creds) {
  bool ok = writeVersioned(kWifiPath, kWifiTempPath, kWifiVersion, &creds, sizeof creds);
  if (!ok) Serial.println(F("[storage] Wi-Fi write failed"));
  return ok;
}

void forgetWifi() { LittleFS.remove(kWifiPath); }

NetPrefs loadNetPrefs() {
  NetPrefs p{};
  if (!readVersioned(kPrefsPath, kPrefsVersion, &p, sizeof p)) return NetPrefs{};
  return p;
}

bool saveNetPrefs(const NetPrefs& prefs) {
  bool ok = writeVersioned(kPrefsPath, kPrefsTempPath, kPrefsVersion, &prefs, sizeof prefs);
  if (!ok) Serial.println(F("[storage] network preferences write failed"));
  return ok;
}

}  // namespace storage
