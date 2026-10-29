// Dev-only fake master bug implementing ../API.md for N simulated bugs. No dependencies.
// Usage: node mock-master.mjs 8081 15   then set the app's bug network to localhost:8081.
import http from "node:http";
import { randomBytes } from "node:crypto";

const port = Number(process.argv[2] ?? 8081);
const count = Math.max(1, Number(process.argv[3] ?? 15));
const clients = new Set();
const started = Date.now();

const NUMBERS = {
  brightness: [5, 100], minWait: [1, 600], maxWait: [1, 600], swingMs: [100, 2000],
  fadeMs: [0, 5000], pulseA: [400, 1500], pulseB: [1500, 2600], ledDelayMs: [0, 1000],
  glowPulsesMin: [1, 10], glowPulsesMax: [1, 10], glowPulseMinMs: [50, 3000], glowPulseMaxMs: [50, 3000],
  glowEveryMinSec: [0, 600], glowEveryMaxSec: [0, 600], glowBrightMin: [1, 100], glowBrightMax: [1, 100],
};
const BOOLS = ["hopping", "randomColour", "stayLit", "holdServo", "onboardLed", "fireflyGlow", "rainbowHop"];

const rand = (n) => Math.floor(Math.random() * n);
const randomColour = () => "#" + randomBytes(3).toString("hex");

function makeBug(i) {
  const id = randomBytes(6).toString("hex");
  return {
    id, name: `Firefly ${id.slice(-6)}`, master: i === 0, online: true, lit: false, colour: "#ffa200", uptime: 0,
    battery: i === 2 ? 3.45 : i === 3 ? null : Math.round((4.2 + Math.random() * 0.5) * 100) / 100,
    lastStart: "power-on", build: i === 3 ? "0ld0ld00" : "f04967d4", ip: null, restarts: i === 1 ? 2 : 0, dropouts: i === 1 ? 1 : 0,
    settings: {
      hopping: true, randomColour: false, stayLit: false, holdServo: false, onboardLed: false, colour: "#ffc940", brightness: 100,
      minWait: 30, maxWait: 120, swingMs: 550, ledDelayMs: 100, fireflyGlow: true, rainbowHop: false, glowColourMode: "fixed", glowColour: "#00ff00", glowPulsesMin: 1, glowPulsesMax: 6, glowPulseMinMs: 100, glowPulseMaxMs: 700, glowEveryMinSec: 3, glowEveryMaxSec: 92, glowBrightMin: 30, glowBrightMax: 100, fadeMs: 500, pulseA: 500, pulseB: 2500,
    },
  };
}

const bugs = new Map(Array.from({ length: count }, (_, i) => makeBug(i)).map((b) => [b.id, b]));

function view(bug) {
  return { ...bug, uptime: Math.round((Date.now() - started) / 1000) };
}

function emit(bug) {
  const msg = `event: bug\ndata: ${JSON.stringify(view(bug))}\n\n`;
  clients.forEach((res) => res.write(msg));
}

function update(id, patch) {
  const next = { ...bugs.get(id), ...patch };
  bugs.set(id, next);
  emit(next);
}

function hop(id) {
  const { settings } = bugs.get(id);
  update(id, { lit: true, colour: settings.randomColour ? randomColour() : settings.colour });
  if (!settings.stayLit) setTimeout(() => update(id, { lit: false }), settings.swingMs + settings.fadeMs);
}

function identify(id) {
  const prev = bugs.get(id).colour;
  [0, 300, 600, 900, 1200, 1500].forEach((t, i) =>
    setTimeout(() => update(id, i % 2 ? { lit: false } : { lit: true, colour: "#ffffff" }), t));
  setTimeout(() => update(id, { colour: prev }), 1600);
}

function scheduleHops(id) {
  const { minWait, maxWait } = bugs.get(id).settings;
  const lo = Math.min(minWait, maxWait), hi = Math.max(minWait, maxWait);
  setTimeout(() => {
    if (bugs.get(id).settings.hopping) hop(id);
    scheduleHops(id);
  }, 1000 * (lo + rand(hi - lo + 1)));
}

/** Parse settings query keys per API.md; returns { settings, name } or { error }. */
function parseSettings(params) {
  const settings = {};
  for (const [key, raw] of params) {
    if (key === "id" || key === "name") continue;
    if (BOOLS.includes(key)) settings[key] = raw === "1";
    else if (key === "glowColourMode") {
      if (!["fixed", "random", "jump"].includes(raw)) return { error: "glowColourMode must be fixed, random or jump" };
      settings.glowColourMode = raw;
    } else if (key === "colour" || key === "glowColour") {
      if (!/^[0-9a-f]{6}$/i.test(raw)) return { error: "colour must be rrggbb" };
      settings[key] = `#${raw.toLowerCase()}`;
    } else if (NUMBERS[key]) {
      const n = Number(raw);
      if (!Number.isFinite(n)) return { error: `${key} must be a number` };
      settings[key] = Math.min(NUMBERS[key][1], Math.max(NUMBERS[key][0], Math.round(n)));
    } else return { error: `unknown setting ${key}` };
  }
  const name = params.get("name")?.trim().slice(0, 19);
  return { settings, name };
}

function targets(id) {
  if (id === "all") return [...bugs.keys()];
  return bugs.has(id) ? [id] : null;
}

// Wi-Fi setup: starts on the hotspot so the panel opens; the form body is url-encoded.
const wifi = { mode: "home", ssid: "HomeNet", saved: null, builtIn: "HomeNet", offline: false };
const wifiView = () => ({
  mode: wifi.offline ? "hotspot" : wifi.mode, ssid: wifi.offline ? null : wifi.ssid, saved: wifi.saved,
  offline: wifi.offline, networks: [wifi.saved, wifi.builtIn].filter((n, i, a) => n && a.indexOf(n) === i),
});
function handleWifi(req, res, path) {
  if (req.method === "GET") return send(res, 200, wifiView());
  const done = () => send(res, 200, { ok: true, restarting: true });
  if (path === "/api/wifi/disconnect") return (wifi.offline = true, done());
  if (path === "/api/wifi/reconnect") return (wifi.offline = false, done());
  let body = "";
  req.on("data", (chunk) => { body += chunk; });
  req.on("end", () => {
    const form = new URLSearchParams(body);
    const ssid = form.get("ssid") ?? "", password = form.get("password") ?? "";
    if (path === "/api/wifi/forget") {
      if (ssid === wifi.saved) wifi.saved = null;
      else if (ssid === wifi.builtIn) wifi.builtIn = null;
      else return send(res, 400, { error: "that network isn't saved" });
      return done();
    }
    if (!ssid || ssid.length > 32) return send(res, 400, { error: "network name must be 1-32 characters" });
    if (password && (password.length < 8 || password.length > 63)) {
      return send(res, 400, { error: "password must be 8-63 characters (or empty for an open network)" });
    }
    Object.assign(wifi, { saved: ssid, offline: false });
    send(res, 200, { ok: true, restarting: true });
  });
}

function handlePost(path, params) {
  if (path === "/api/ripple") {
    const ms = Math.min(10000, Math.max(0, Number(params.get("ms")) || 0));
    bugs.forEach((_, id) => setTimeout(() => hop(id), rand(ms + 1)));
    return null;
  }
  const ids = targets(params.get("id"));
  if (!ids) return "unknown bug id";
  if (path === "/api/hop") ids.forEach(hop);
  else if (path === "/api/identify") {
    if (ids.length !== 1) return "identify needs a single bug id";
    identify(ids[0]);
  } else if (path === "/api/settings") {
    const { settings, name, error } = parseSettings(params);
    if (error) return error;
    if (name !== undefined && ids.length !== 1) return "name can only be set on a single bug";
    ids.forEach((id) => update(id, {
      ...(name ? { name } : {}), settings: { ...bugs.get(id).settings, ...settings },
    }));
  } else return "unknown endpoint";
  return null;
}

function send(res, status, body) {
  res.writeHead(status, { "Content-Type": "application/json" }).end(JSON.stringify(body));
}

http.createServer((req, res) => {
  res.setHeader("Access-Control-Allow-Origin", "*");
  const url = new URL(req.url, `http://localhost:${port}`);
  if (req.method === "GET" && url.pathname === "/api/events") {
    res.writeHead(200, { "Content-Type": "text/event-stream", "Cache-Control": "no-cache" });
    res.write(": hello\n\n");
    clients.add(res);
    req.on("close", () => clients.delete(res));
    return;
  }
  if (req.method === "GET" && url.pathname === "/api/bugs") return send(res, 200, { bugs: [...bugs.values()].map(view) });
  if (url.pathname.startsWith("/api/wifi")) return handleWifi(req, res, url.pathname);
  if (req.method !== "POST") return send(res, 404, { error: "not found" });
  const error = handlePost(url.pathname, url.searchParams);
  send(res, error ? 400 : 200, error ? { error } : { ok: true });
}).listen(port, () => console.log(`Mock master with ${count} bugs on http://localhost:${port}`));

bugs.forEach((_, id) => scheduleHops(id));
