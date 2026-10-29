// Pure helpers for the Fireflies master API (see ../API.md).
// No DOM or network here, so it can be unit-tested with `node --test`.

const HOST_PATTERN = /^[a-z0-9]([a-z0-9-]*[a-z0-9])?(\.[a-z0-9]([a-z0-9-]*[a-z0-9])?)*(:\d{1,5})?$/;
const LOCAL_HOSTNAMES = new Set(["localhost", "127.0.0.1", "[::1]", ""]);

export const DEFAULT_HOST = "fireflies.local";
export const NAME_MAX = 19;

export const NUMBER_FIELDS = [
  { key: "brightness", label: "Brightness", unit: "%", min: 5, max: 100, step: 5 },
  { key: "minWait", label: "Min wait", unit: "s", min: 1, max: 600, step: 1 },
  { key: "maxWait", label: "Max wait", unit: "s", min: 1, max: 600, step: 1 },
  { key: "swingMs", label: "Swing time", unit: "ms", min: 100, max: 2000, step: 50 },
  { key: "ledDelayMs", label: "LED delay", unit: "ms", min: 0, max: 1000, step: 10 },
  { key: "fadeMs", label: "Fade time", unit: "ms", min: 0, max: 5000, step: 100 },
  // Firefly glow: each is a min-max range, picked fresh every burst / pulse for a natural feel.
  { key: "glowPulsesMin", label: "Glow pulses min", unit: "", min: 1, max: 10, step: 1 },
  { key: "glowPulsesMax", label: "Glow pulses max", unit: "", min: 1, max: 10, step: 1 },
  { key: "glowPulseMinMs", label: "Pulse length min", unit: "ms", min: 50, max: 3000, step: 50 },
  { key: "glowPulseMaxMs", label: "Pulse length max", unit: "ms", min: 50, max: 3000, step: 50 },
  { key: "glowEveryMinSec", label: "Glow every min", unit: "s", min: 0, max: 600, step: 1 },
  { key: "glowEveryMaxSec", label: "Glow every max", unit: "s", min: 0, max: 600, step: 1 },
  { key: "glowBrightMin", label: "Glow brightness min", unit: "%", min: 1, max: 100, step: 5 },
  { key: "glowBrightMax", label: "Glow brightness max", unit: "%", min: 1, max: 100, step: 5 },
  // nudge: − / + buttons for calibrating; the arm moves to that end as you adjust it.
  { key: "pulseA", label: "Servo pulse A", unit: "µs", min: 400, max: 1500, step: 10, nudge: true },
  { key: "pulseB", label: "Servo pulse B", unit: "µs", min: 1500, max: 2600, step: 10, nudge: true },
];

// Settings shared by "Apply to all bugs"; name and servo pulses are per-bug calibration.
export const SHARED_KEYS = [
  "hopping", "randomColour", "stayLit", "holdServo", "onboardLed", "colour", "brightness", "minWait", "maxWait", "swingMs", "ledDelayMs", "fadeMs",
  "fireflyGlow", "glowColour", "glowColourMode", "rainbowHop",
  "glowPulsesMin", "glowPulsesMax", "glowPulseMinMs", "glowPulseMaxMs", "glowEveryMinSec", "glowEveryMaxSec", "glowBrightMin", "glowBrightMax",
];

/** "http://Fireflies.local/" or "fireflies" -> "fireflies.local"; null if not a plain host[:port]. */
export function normalizeHost(input) {
  const host = String(input ?? "").trim().toLowerCase()
    .replace(/^https?:\/\//, "").replace(/\/+$/, "");
  if (!HOST_PATTERN.test(host)) return null;
  const bare = !/[.:]/.test(host) && host !== "localhost";
  return bare ? `${host}.local` : host;
}

/** True when the page itself is being served by a bug (so the API is same-origin). */
export function servedByBug({ protocol, hostname }) {
  return protocol !== "file:" && !LOCAL_HOSTNAMES.has(hostname);
}

/** "" for same-origin, otherwise "http://host". */
export function apiBase(sameOrigin, host) {
  return sameOrigin ? "" : `http://${host}`;
}

export function apiUrl(base, path, params = {}) {
  const query = new URLSearchParams(params).toString();
  return `${base}${path}${query ? `?${query}` : ""}`;
}

/** Settings object -> query params: booleans as 1/0, colours without '#', numbers as strings. */
export function settingsParams(settings) {
  return Object.fromEntries(Object.entries(settings).map(([key, value]) => {
    if (typeof value === "boolean") return [key, value ? "1" : "0"];
    if (/colour$/i.test(key)) return [key, String(value).replace(/^#/, "").toLowerCase()];
    return [key, String(value)];
  }));
}

export function sharedSettings(settings) {
  return Object.fromEntries(SHARED_KEYS.filter((k) => k in settings).map((k) => [k, settings[k]]));
}

/** Master first, then online bugs, then offline ones; by name (then id) within each. New array. */
export function sortBugs(bugs) {
  return [...bugs].sort((a, b) =>
    Number(Boolean(b.master)) - Number(Boolean(a.master))
    || Number(Boolean(b.online)) - Number(Boolean(a.online))
    || a.name.localeCompare(b.name, undefined, { numeric: true, sensitivity: "base" })
    || a.id.localeCompare(b.id));
}

export const BATTERY_LOW_VOLTS = 3.6;  // 1.2 V per AA cell: the servo starts to struggle

/** Battery reading for display: { text, low }. battery is volts or null (not wired). */
export function batteryInfo(battery) {
  if (typeof battery !== "number") return { text: "—", low: false };
  return { text: `${battery.toFixed(2)} V`, low: battery < BATTERY_LOW_VOLTS };
}

/** True when a bug runs a different firmware build than the master (it needs updating). */
export function isOutdated(bug, masterBuild) {
  return Boolean(bug.build && masterBuild && bug.build !== masterBuild);
}

export function bugStatus(bug) {
  if (!bug.online) return "offline";
  return bug.settings?.hopping ? "hopping" : "resting";
}

export function summarize(bugs) {
  const statuses = bugs.map(bugStatus);
  return {
    total: bugs.length,
    hopping: statuses.filter((s) => s === "hopping").length,
    offline: statuses.filter((s) => s === "offline").length,
  };
}
