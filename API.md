# Fireflies API

The master bug serves this API, and the web app, on port 80. You can reach it at:
- `http://fireflies.local/` when the master is on home Wi-Fi (only the master advertises this name)
- `http://192.168.4.1/` when the master runs its own `Fireflies` hotspot

All responses send `Access-Control-Allow-Origin: *`, so the app can also run from `localhost` while you develop.

## Bug object
```json
{
  "id": "a1b2c3d4e5f6",        // softAP MAC (what the bug transmits from), lowercase hex
  "name": "Porch",             // up to 19 chars; defaults to "Bug " + last 6 hex digits
  "master": true,              // is this bug the current master
  "online": true,              // false if no status for 10 s
  "lit": false,                // LED currently on
  "colour": "#ff8800",         // last shown colour
  "uptime": 1234,              // seconds
  "battery": 4.52,             // volts, or null if the A0 resistor isn't fitted
  "lastStart": "power-on",     // why it last booted: power-on | watchdog | crash | restart | reset button | ...
  "restarts": 0,               // reboots the master has noticed (resets when the master restarts)
  "dropouts": 0,               // times it went silent for 10 s
  "build": "933f8782",         // firmware fingerprint (differs from the master = needs updating)
  "ip": null,                  // set while the firefly is in update mode
  "settings": {
    "hopping": true,
    "randomColour": false,
    "stayLit": false,
    "holdServo": false,        // keep driving the servo between hops (default: release it)
    "onboardLed": false,       // the D1 mini's blue LED (default: off)
    "colour": "#ffa200",       // fixed colour used when randomColour is false
    "brightness": 100,         // %, 5..100
    "minWait": 30,             // s, 1..600
    "maxWait": 120,             // s, 1..600
    "swingMs": 550,            // 100..2000: how long the swing takes (eased; can't beat the servo's top speed)
    "ledDelayMs": 100,         // 0..1000: LED lights this long after the servo starts (servo lag)
    "fireflyGlow": true,       // blink like a firefly between hops (bursts of soft pulses)
    "glowColour": "#00ff00",   // green
    "glowColourMode": "fixed", // "fixed" (glowColour) | "random" (new colour per burst) | "jump" (the hop's colour)
    "rainbowHop": false,       // during the jump, sweep the whole colour wheel from a random start
    "glowPulsesMin": 1, "glowPulsesMax": 6,         // 1..10: pulses per burst, random in range
    "glowPulseMinMs": 100, "glowPulseMaxMs": 700,   // 50..3000: each pulse's length, re-rolled per pulse
    "glowEveryMinSec": 3, "glowEveryMaxSec": 92,    // 0..600: wait between bursts, random in range
    "glowBrightMin": 30, "glowBrightMax": 100,     // 1..100 %: each pulse's brightness (min = max for fixed)
    "fadeMs": 500,            // 0..5000
    "pulseA": 500,             // µs, 400..1500; changing it moves the arm to end A
    "pulseB": 2500             // µs, 1500..2600; changing it moves the arm to end B
  }
}
```

## Endpoints
| Method | Path | Notes |
|---|---|---|
| GET | `/api/bugs` | `{ "bugs": [Bug, ...] }`, master first, then sorted by name |
| GET | `/api/events` | SSE (requires `Accept: text/event-stream`, which browsers' EventSource sends; for curl, add `-H "Accept: text/event-stream"`). `bug` event = one Bug JSON whenever it changes; `gone` event = `{"id": "..."}` when a bug has been offline for 10 minutes and is dropped from the list |
| POST | `/api/hop?id=<id or all>` | hop now |
| POST | `/api/ripple?ms=3000` | every bug hops after a random 0..ms delay (ms clamped 0..10000) |
| POST | `/api/update?id=<id or all>&minutes=10` | followers join home Wi-Fi with OTA open for N (1-30) minutes; master must be on home Wi-Fi. Used by `scripts/update_all.py` |
| POST | `/api/identify?id=<id>` | bug flashes white 3 times |
| POST | `/api/settings?id=<id or all>&<key>=<value>...` | sets only the keys you send. Keys match `settings`, plus `name` (single bug only). Booleans are `1`/`0`, colours are `rrggbb` with no `#`. Out-of-range values are clamped |

| GET | `/api/wifi` | `{ "mode": "home" \| "hotspot" \| "none", "offline": bool, "ssid": current network or null, "saved": network saved from the app or null, "networks": [known network names] }` |
| POST | `/api/wifi` | form body `ssid=…&password=…` (password empty for open networks, else 8-63 chars). Saved on the master only (never broadcast), which then restarts to join it. Tried before the network in `secrets.h` |
| POST | `/api/wifi/forget` | form body `ssid=…`: deletes that known network (the built-in one from `secrets.h` is hidden, not erased; saving it again brings it back), then restarts |
| POST | `/api/wifi/disconnect` | stay off Wi-Fi and run the `Fireflies` hotspot until reconnected (kept across reboots), then restart |
| POST | `/api/wifi/reconnect` | undo disconnect, then restart |

Before any planned restart, the master broadcasts a `Hold` command (45 s), so followers don't take over during the reboot. While holding, they listen on all 13 channels.

Errors return 400 with `{"error": "message"}`, or 503 when the master is momentarily busy (command queue full, or low on memory); retry after a moment. Success returns 200 `{"ok": true}`. `OPTIONS` requests get a 204 CORS preflight response.

## Bug-to-bug (ESP-NOW, broadcast on the master's channel)
These packets are internal to the firmware and shown here for reference. Every packet starts with `magic "FF"`, `version`, `type`, `groupId` (from `secrets.h`) and `seq`.
- `BEACON` (master, every 500 ms): master uptime and Wi-Fi mode
- `STATUS` (every bug, every 3 s and when its state changes): name, settings, lit, colour, uptime
- `COMMAND` (master → target id or all, sent 3 times, receivers de-duplicate by seq): hop, ripple, identify, or settings with a field mask

Choosing the master: at boot a bug scans for the home Wi-Fi (up to 3 times). It then listens on the home channel(s) and channel 1, or on all 13 channels if home Wi-Fi is configured but out of reach, visiting each channel twice. If it hears no beacon in that time (plus a random delay of up to 7 s), it becomes master. When two masters hear each other, the one with the longer uptime wins, and if uptimes tie, the lower MAC wins. Followers start a new search after 5 s without a beacon.
