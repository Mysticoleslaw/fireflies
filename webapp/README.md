# Fireflies web app

A static control panel for the bug network. It talks to the master bug only, and the master passes commands on to every other bug over ESP-NOW (see `../API.md`). There's no build step and no dependencies.

## Where it runs
- **From the master bug:** the bugs serve this app themselves. Open `http://fireflies.local/` on home Wi-Fi, or `http://192.168.4.1/` after joining the `Fireflies` hotspot.
- **From your computer while developing:**
  ```sh
  cd webapp
  python3 -m http.server 8080   # then open http://localhost:8080
  ```
  Enter the bug network address (default `fireflies.local`). It's saved in this browser.

## Controls
- **Hop all** `H` · **Ripple** `R` (every bug hops within 3 s) · **Pause / Resume all** `P`
- Per bug: hopping on/off, **Hop**, and **Identify** (flashes white so you can find it in the lawn)
- **Settings:** name, random or fixed colour, stay lit, brightness, timing and servo pulses. **Apply to all bugs** copies colour, timing and hopping to every bug, but not the name or servo pulses.

## Develop without hardware
```sh
node mock-master.mjs 8081 15   # a fake master with 15 bugs
```
Then set the bug network to `localhost:8081`.

## Test
`node --test`
