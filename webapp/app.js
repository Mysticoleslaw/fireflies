// Fireflies control panel: talks to the master firefly, which relays to all the others (see ../API.md).
import {
  DEFAULT_HOST, NAME_MAX, NUMBER_FIELDS, batteryInfo, isOutdated, normalizeHost, servedByBug, apiBase, apiUrl,
  settingsParams, sharedSettings, sortBugs, bugStatus, summarize,
} from "./bugs.js";

const HOST_KEY = "fireflies.host";
const RIPPLE_MS = 3000;
const TOAST_MS = 5000;
const STATUS_LABEL = { hopping: "Hopping", resting: "Resting", offline: "Offline" };
const CONN_LABEL = { connecting: "Connecting…", connected: "Connected", error: "Can't reach the fireflies" };

const bugs = new Map();  // id -> bug (as sent by the master)
const cards = new Map(); // id -> card element
const sameOrigin = servedByBug(location);
let base = "";
let source = null;

const $ = (sel, root = document) => root.querySelector(sel);
const grid = $("#grid");

// ---------- storage (per-browser convenience; failures are non-fatal) ----------

function loadHost() {
  try {
    return normalizeHost(localStorage.getItem(HOST_KEY)) ?? DEFAULT_HOST;
  } catch {
    return DEFAULT_HOST;
  }
}

function saveHost(host) {
  try {
    localStorage.setItem(HOST_KEY, host);
  } catch {
    showError("Couldn't save the address in this browser.");
  }
}

// ---------- connection ----------

function connect(host) {
  base = apiBase(sameOrigin, host);
  source?.close();
  bugs.clear();
  cards.forEach((card) => card.remove());
  cards.clear();
  renderSummary();
  setConn("connecting");
  source = new EventSource(apiUrl(base, "/api/events"));
  let opened = false; // EventSource retries forever; before the first open, an error means unreachable
  source.addEventListener("open", () => { opened = true; setConn("connected"); fetchBugs(); });
  source.addEventListener("error", () => setConn(opened ? "connecting" : "error"));
  source.addEventListener("bug", (e) => withJson(e.data, upsertBug));
  source.addEventListener("gone", (e) => withJson(e.data, ({ id }) => removeBug(id)));
}

async function fetchBugs() {
  try {
    const res = await fetch(apiUrl(base, "/api/bugs"));
    if (!res.ok) throw new Error(`${res.status} ${res.statusText}`);
    const { bugs: list } = await res.json();
    const ids = new Set(list.map((b) => b.id));
    [...bugs.keys()].filter((id) => !ids.has(id)).forEach(removeBug);
    list.forEach(upsertBug);
    loadWifi();
  } catch (err) {
    setConn("error");
    showError(`Couldn't load fireflies: ${err.message}`);
  }
}

function withJson(raw, fn) {
  try {
    fn(JSON.parse(raw));
  } catch (err) {
    console.warn("Bad event from master", err);
  }
}

function upsertBug(bug) {
  if (!bug?.id) return;
  const prev = bugs.get(bug.id);
  bugs.set(bug.id, bug);
  renderCard(bug);
  if (!prev || prev.name !== bug.name || prev.master !== bug.master || prev.online !== bug.online) reorder();
  renderSummary();
}

function removeBug(id) {
  bugs.delete(id);
  cards.get(id)?.remove();
  cards.delete(id);
  renderSummary();
}

// ---------- actions ----------

async function post(path, params) {
  try {
    const res = await fetch(apiUrl(base, path, params), { method: "POST" });
    if (!res.ok) {
      const body = await res.json().catch(() => ({}));
      throw new Error(body.error ?? `${res.status} ${res.statusText}`);
    }
  } catch (err) {
    showError(err.message);
  }
}

const setSettings = (id, settings) => post("/api/settings", { id, ...settingsParams(settings) });
const hopAll = () => post("/api/hop", { id: "all" });
const ripple = () => post("/api/ripple", { ms: RIPPLE_MS });

function toggleAll() {
  const anyHopping = summarize([...bugs.values()]).hopping > 0;
  setSettings("all", { hopping: !anyHopping });
}

function applyToAll(id) {
  const bug = bugs.get(id);
  if (bug) setSettings("all", sharedSettings(bug.settings));
}

// ---------- rendering ----------

function createNumberField({ key, label, unit, min, max, step, nudge }) {
  const field = document.createElement("label");
  field.className = nudge ? "field wide" : "field";
  const name = document.createElement("span");
  name.textContent = label;
  const wrap = document.createElement("span");
  wrap.className = "input-unit";
  const input = Object.assign(document.createElement("input"), { type: "number", min, max, step });
  input.dataset.setting = key;
  const unitEl = document.createElement("span");
  unitEl.textContent = unit;
  wrap.append(input, unitEl);
  if (nudge) wrap.append(createNudge(key, -step, `Decrease ${label}`), createNudge(key, step, `Increase ${label}`));
  field.append(name, wrap);
  return field;
}

function createNudge(key, delta, ariaLabel) {
  const btn = Object.assign(document.createElement("button"), {
    type: "button", className: "btn nudge", textContent: delta < 0 ? "−" : "+",
  });
  btn.setAttribute("aria-label", ariaLabel);
  Object.assign(btn.dataset, { action: "nudge", key, delta });
  return btn;
}

// Calibration: step a servo pulse; the bug moves the arm to that end so you can watch it.
function nudge(id, card, key, delta) {
  const input = $(`[data-setting="${key}"]`, card);
  const next = Math.min(Number(input.max), Math.max(Number(input.min), Number(input.value) + delta));
  input.value = next;
  setSettings(id, { [key]: next });
}

function createCard(id) {
  const card = $("#bug-card").content.firstElementChild.cloneNode(true);
  card.dataset.id = id;
  $("[data-numbers]", card).append(...NUMBER_FIELDS.map(createNumberField));
  cards.set(id, card);
  grid.append(card);
  return card;
}

function setValue(input, value) {
  if (document.activeElement === input || value === undefined) return;
  if (input.type === "checkbox") input.checked = Boolean(value);
  else input.value = value;
}

function renderCard(bug) {
  const card = cards.get(bug.id) ?? createCard(bug.id);
  const status = bugStatus(bug);
  const settings = bug.settings ?? {};
  $("[data-name]", card).textContent = bug.name;
  $("[data-master]", card).hidden = !bug.master;
  $("[data-id]", card).textContent = bug.id;
  const pill = $("[data-status]", card);
  pill.textContent = STATUS_LABEL[status];
  pill.dataset.status = status;
  const lit = bug.online && bug.lit;
  $("[data-swatch]", card).style.setProperty("--led", lit ? bug.colour : "transparent");
  card.classList.toggle("lit", Boolean(lit));
  $("[data-controls]", card).disabled = !bug.online;
  setValue($("[data-name-input]", card), bug.name);
  card.querySelectorAll("[data-setting]").forEach((input) => setValue(input, settings[input.dataset.setting]));
  $("[data-colour-field]", card).hidden = Boolean(settings.randomColour);
  $("[data-glow-colour-field]", card).hidden = (settings.glowColourMode ?? "fixed") !== "fixed";
  renderHealth($("[data-health]", card), bug);
}

const masterBuild = () => [...bugs.values()].find((b) => b.master)?.build;

// "Battery 4.52 V · Restarts 0 · Dropouts 1 · Last start: power-on · Firmware f04967d4" (problems in red)
function renderHealth(el, bug) {
  const battery = batteryInfo(bug.battery);
  const parts = [
    ["Battery", battery.text, battery.low],
    ["Restarts", bug.restarts ?? 0, bug.restarts > 0],
    ["Dropouts", bug.dropouts ?? 0, bug.dropouts > 0],
    ["Last start", bug.lastStart ?? "—", ["crash", "watchdog"].includes(bug.lastStart)],
    ["Firmware", bug.build ?? "—", isOutdated(bug, masterBuild())],
  ];
  el.replaceChildren(...parts.flatMap(([label, value, bad], i) => {
    const strong = Object.assign(document.createElement("span"), { textContent: String(value) });
    if (bad) strong.className = "bad";
    return [`${i ? " · " : ""}${label} `, strong];
  }));
}

function reorder() {
  const order = sortBugs([...bugs.values()]).map((b) => cards.get(b.id));
  const same = order.length === grid.children.length && order.every((card, i) => grid.children[i] === card);
  if (!same) grid.replaceChildren(...order);
}

function renderSummary() {
  const { total, hopping, offline } = summarize([...bugs.values()]);
  const parts = [`${total} ${total === 1 ? "firefly" : "fireflies"}`, `${hopping} hopping`];
  if (offline) parts.push(`${offline} offline`);
  $("#summary").textContent = total ? parts.join(" · ") : "";
  $("#empty").hidden = total > 0 || $("#conn").dataset.state !== "connected";
  const anyOnline = total - offline > 0;
  document.querySelectorAll("[data-global]").forEach((btn) => { btn.disabled = !anyOnline; });
  $("[data-global=toggle]").firstChild.textContent = hopping > 0 ? "Pause all " : "Resume all ";
}

function setConn(state) {
  const conn = $("#conn");
  conn.dataset.state = state;
  conn.textContent = CONN_LABEL[state];
  renderSummary();
}

let toastTimer;
function showError(message) {
  console.error(message);
  const toast = $("#toast");
  toast.textContent = message;
  toast.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { toast.hidden = true; }, TOAST_MS);
}

// ---------- events ----------

$("#net-form").addEventListener("submit", (e) => {
  e.preventDefault();
  const host = normalizeHost($("#net-host").value);
  if (!host) return showError("Enter a hostname or IP, like fireflies.local or 192.168.4.1");
  $("#net-host").value = host;
  saveHost(host);
  connect(host);
});

grid.addEventListener("click", (e) => {
  const btn = e.target.closest("[data-action]");
  const id = e.target.closest(".card")?.dataset.id;
  if (!btn || !id) return;
  if (btn.dataset.action === "hop") post("/api/hop", { id });
  if (btn.dataset.action === "identify") post("/api/identify", { id });
  if (btn.dataset.action === "apply-all") applyToAll(id);
  if (btn.dataset.action === "nudge") {
    e.preventDefault();  // the button sits inside a <label>
    nudge(id, btn.closest(".card"), btn.dataset.key, Number(btn.dataset.delta));
  }
});

grid.addEventListener("change", (e) => {
  const input = e.target;
  const id = input.closest(".card")?.dataset.id;
  if (!id) return;
  if (input.matches("[data-name-input]")) {
    const name = input.value.trim().slice(0, NAME_MAX);
    if (name) post("/api/settings", { id, name });
    return;
  }
  const key = input.dataset.setting;
  if (!key || (input.type === "number" && input.value === "")) return;
  const value = input.type === "checkbox" ? input.checked : input.type === "number" ? Number(input.value) : input.value;
  setSettings(id, { [key]: value });
});

// ---------- Wi-Fi setup ----------

const CONFIRM_MS = 3000;

function wifiStatus({ mode, ssid, offline }) {
  if (mode === "home") return `on "${ssid}"`;
  if (mode === "hotspot") return offline ? "Hotspot (disconnected from Wi-Fi)" : "Hotspot (no Wi-Fi network)";
  return "starting…";
}

// Destructive buttons need a second tap within CONFIRM_MS (no browser dialogs).
function confirmTap(btn, action) {
  if (btn.dataset.armed) {
    delete btn.dataset.armed;
    btn.textContent = btn.dataset.label;
    return action();
  }
  btn.dataset.label = btn.textContent;
  btn.dataset.armed = "1";
  btn.textContent = "Tap again to confirm";
  setTimeout(() => {
    if (!btn.dataset.armed) return;
    delete btn.dataset.armed;
    btn.textContent = btn.dataset.label;
  }, CONFIRM_MS);
}

function renderNetworks(networks = []) {
  const list = $("#wifi-networks");
  list.replaceChildren(...networks.map((ssid) => {
    const li = document.createElement("li");
    const name = Object.assign(document.createElement("span"), { textContent: ssid });
    const del = Object.assign(document.createElement("button"), { type: "button", className: "btn", textContent: "Delete" });
    del.setAttribute("aria-label", `Delete ${ssid}`);
    del.addEventListener("click", () => confirmTap(del, () => deleteNetwork(ssid)));
    li.append(name, del);
    return li;
  }));
  if (!networks.length) list.append(Object.assign(document.createElement("li"), { textContent: "None yet", className: "hint" }));
}

async function deleteNetwork(ssid) {
  if (await sendWifi("/api/wifi/forget", new URLSearchParams({ ssid }))) {
    showWifiNote(`Deleted "${ssid}". The master is restarting.`);
  }
}

async function loadWifi() {
  try {
    const res = await fetch(apiUrl(base, "/api/wifi"));
    if (!res.ok) throw new Error(`${res.status}`);
    renderWifi(await res.json());
  } catch {
    $("#wifi").hidden = true;  // older firmware without Wi-Fi setup
  }
}

function renderWifi(info) {
  const { mode, offline, networks } = info;
  const panel = $("#wifi");
  panel.hidden = false;
  $("#wifi-status").textContent = wifiStatus(info);
  renderNetworks(networks);
  const toggle = $("#wifi-toggle");
  if (!toggle.dataset.armed) toggle.textContent = offline ? "Reconnect to Wi-Fi" : "Disconnect from Wi-Fi";
  toggle.dataset.offline = offline ? "1" : "";
  toggle.hidden = !(offline || mode === "home");
  if (mode === "hotspot" && !panel.dataset.seen) panel.open = true;  // that's when you need it
  panel.dataset.seen = "1";
}

async function sendWifi(path, body) {
  try {
    const res = await fetch(apiUrl(base, path), { method: "POST", body });
    const data = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(data.error ?? `${res.status} ${res.statusText}`);
    return true;
  } catch (err) {
    showError(err.message);
    return false;
  }
}

function showWifiNote(text) {
  Object.assign($("#wifi-note"), { textContent: text, hidden: false });
}

$("#wifi-form").addEventListener("submit", async (e) => {
  e.preventDefault();
  const ssid = $("#wifi-ssid").value.trim();
  const password = $("#wifi-password").value;
  if (!(await sendWifi("/api/wifi", new URLSearchParams({ ssid, password })))) return;
  $("#wifi-password").value = "";
  loadWifi();
  showWifiNote(`Saved. The master is restarting to join "${ssid}". Switch your phone to that network, ` +
    "then open http://fireflies.local/ (or the master's address from your router).");
});

$("#wifi-toggle").addEventListener("click", (e) => {
  const btn = e.currentTarget;
  if (btn.dataset.offline) return reconnectWifi();
  confirmTap(btn, disconnectWifi);
});

async function disconnectWifi() {
  if (!(await sendWifi("/api/wifi/disconnect"))) return;
  showWifiNote("Disconnecting. The master restarts on its Fireflies hotspot: join that network on your phone, " +
    "then open http://192.168.4.1/.");
}

async function reconnectWifi() {
  if (!(await sendWifi("/api/wifi/reconnect"))) return;
  showWifiNote("Reconnecting. The master restarts and joins your Wi-Fi: switch your phone back to it, " +
    "then open http://fireflies.local/.");
}

const GLOBAL_ACTIONS = { hop: hopAll, ripple, toggle: toggleAll };
document.querySelectorAll("[data-global]").forEach((btn) =>
  btn.addEventListener("click", () => GLOBAL_ACTIONS[btn.dataset.global]()));

const KEYS = { h: hopAll, r: ripple, p: toggleAll };
document.addEventListener("keydown", (e) => {
  if (e.metaKey || e.ctrlKey || e.altKey || e.target.closest("input, textarea, select")) return;
  const action = KEYS[e.key.toLowerCase()];
  if (!action || !summarize([...bugs.values()]).total) return;
  e.preventDefault();
  action();
});

const startHost = loadHost();
if (!sameOrigin) {
  $("#net-form").hidden = false;
  $("#net-host").value = startHost;
}
connect(startHost);
