# Fireflies firmware

Custom firmware for the D1 mini bugs. Every bug runs the same build:
- **Bugs talk over ESP-NOW**, radio to radio. They don't need a router, and any number of bugs works.
- **One bug becomes master automatically.** If a bug hears no master when it starts, it takes over. If two masters meet, the one that's been on longer wins.
- **Only the master uses Wi-Fi.** It joins home Wi-Fi if it's in range, otherwise it makes a `Fireflies` hotspot. Either way, it serves the web app and passes commands on to every bug.

Open the app at `http://fireflies.local/` on home Wi-Fi, or join the `Fireflies` hotspot and the app should pop up (otherwise go to `http://192.168.4.1/`).

> `../esphome/` is the earlier ESPHome version, kept for reference only. The two versions don't work together.

## Wiring (same as before)
- D1 (GPIO5) -> servo signal
- D2 (GPIO4) -> 330 Ω -> WS2812B DIN (2 LEDs folded back-to-back; set `kLedCount` in `src/config.h` for other counts)
- D4 (GPIO2) is left free: it drives the D1 mini's blue onboard LED (the "Onboard LED" setting, off by default)
- 5V -> servo + LED, GND common, 470 µF across 5V/GND
- Optional battery monitor: **180 kΩ from battery + (5V pin) to A0**. The app then shows the voltage, and turns it red below 3.6 V

## Setup
1. `cp include/secrets.example.h include/secrets.h`, then fill it in. Every bug uses the same file.
2. Install PlatformIO (VS Code extension, or `pip install platformio`).

## Flash a firefly (USB, first time only)
```sh
pio run -t upload       # firmware, with the web app built in
pio device monitor      # optional: watch it pick a channel and become master or follower
```
The web app is compiled into the firmware, so there's no separate filesystem upload, and saved settings (calibration, names) are never wiped by updates.

## Flash many bugs
```sh
python scripts/flash_station.py --count 13 --master fireflies.local
```
Plug boards in one after another (or several on a hub). Each one gets the firmware and the app, prints its name to write on it, and is checked in the mesh.

## Update every firefly over Wi-Fi
```sh
python scripts/update_all.py            # bugs whose build differs; --force for all, --only ee4624 for some
```
Run with the PlatformIO venv's Python, with the master on home Wi-Fi. Followers briefly join home Wi-Fi in update mode, get the new firmware over OTA, then the master is updated last and every build is checked. The app shows each firefly's build, red if it differs from the master's.

## Test
`pio test -e native` runs the election, protocol, settings and roster unit tests on your computer.

## Layout
- `lib/core/`: pure logic with no Arduino code: settings, packet format, master election, bug list
- `src/hopper.*`: servo hop and LED flash/fade
- `src/battery.*`: battery voltage on A0
- `src/radio.*`: ESP-NOW send/receive. Every bug runs a hidden access point, because the ESP8266 only receives ESP-NOW broadcasts while one is up
- `src/network.*`: channel scan, home Wi-Fi or hotspot, mDNS, OTA
- `src/gateway.*`: HTTP API and live updates (see `../API.md`)
- `src/node.*`: ties it together: election, beacons, status, relaying commands
