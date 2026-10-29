# GlowBug (ESPHome)

Same behaviour as the old WLED usermod: the servo hops 180° at random intervals, and the LED flashes and fades each time it lands. One YAML file runs every bug. Each device names itself from its MAC address (`glowbug-a1b2c3`).

## Wiring (D1 mini)
- D1 (GPIO5) -> servo signal
- D4 (GPIO2) -> 330 Ω -> WS2812B DIN
- 5V -> servo + LED, GND common, 470 µF across 5V/GND

## Flash
1. `cp secrets.yaml.example secrets.yaml` and fill it in (`openssl rand -base64 32` for `api_key`)
2. First flash over USB: `esphome run glowbug.yaml` (or the ESPHome add-on in Home Assistant / web.esphome.io)
3. After that, updates go over Wi-Fi (OTA)

## Entities (Home Assistant + `http://glowbug-xxxxxx.local/`)
| Entity | Default | Notes |
|---|---|---|
| Hopping (switch) | on | off = bug rests, LED off |
| Hop now (button) | | |
| LED (light) | | sets the colour used when Random colour is off |
| Random colour | on | |
| Stay lit | off | LED stays on after a hop instead of fading |
| Min / Max wait | 10 / 30 s | |
| Flash brightness | 100 % | |
| Swing / Fade time | 500 / 1200 ms | |
| Servo pulse A / B | 500 / 2500 µs | tune for a clean 180° with no buzzing |

All settings survive a reboot.

## REST API (per bug, for the web app)
- `POST /button/hop_now/press`
- `POST /switch/hopping/turn_on` / `turn_off`
- `POST /number/min_wait/set?value=5`
- `GET /events` streams live state (SSE)
