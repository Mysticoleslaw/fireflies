# Fireflies

A swarm of little lawn "fireflies" that hop and glow. Each one is a Wemos D1 mini (ESP8266) with a micro servo that flips an arm 180° at random intervals, and two WS2812B LEDs that flash on each jump. Between jumps they blink softly, like real fireflies.

- **No hub needed:** the bugs talk to each other directly over ESP-NOW. The first one switched on becomes the master, and if it goes away, another takes over.
- **Works anywhere:** at home the master joins your Wi-Fi as `fireflies.local`. Without a network it starts a `Fireflies` hotspot, and the control app pops up on your phone.
- **Phone app built in:** hop all, ripple, pause, identify a bug, rename it, calibrate its servo, and tune every light effect. Each bug shows its battery, restarts, dropouts and firmware version.
- **Light effects:** a random or fixed colour per jump, a rainbow sweep during the jump, and firefly glow between jumps. The glow runs in bursts of soft pulses, with random counts, lengths, brightness and timing, in a fixed or random colour, or the jump's colour.
- **Wireless updates:** `update_all.py` updates every bug over Wi-Fi. Settings and calibration are never lost.

## Hardware (per bug)
| Part | D1 mini pin |
|---|---|
| Micro servo signal | D1 (GPIO5) |
| 2× WS2812B data, through a 330 Ω resistor | D2 (GPIO4) |
| Servo + LED power, battery (3×AA or 4×NiMH) | 5V / G |
| 470 µF capacitor (recommended) | across 5V / G |
| Battery monitor (optional): 180 kΩ | 5V to A0 |

## Layout
- [`firmware/`](firmware/): PlatformIO firmware: mesh, master election, hop and LED behaviour, web API. [README](firmware/README.md)
- [`webapp/`](webapp/): the control app, as plain HTML and JS. It's compiled into the firmware. [README](webapp/README.md)
- [`API.md`](API.md): the HTTP/SSE API the master serves.
- `esphome/`, `glowbug_wled.zip`: earlier prototypes (ESPHome, WLED), kept for reference.

## Quick start
```sh
cd firmware
cp include/secrets.example.h include/secrets.h   # Wi-Fi, hotspot and OTA passwords
pip install platformio
pio test -e native                 # unit tests
pio run -t upload                  # flash one bug over USB
python scripts/flash_station.py    # flash many bugs, one after another
python scripts/update_all.py       # later: update every bug over Wi-Fi
```
Then open `http://fireflies.local/`, or join the `Fireflies` hotspot.
