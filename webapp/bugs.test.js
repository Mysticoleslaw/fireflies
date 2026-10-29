import { test } from "node:test";
import assert from "node:assert/strict";
import {
  normalizeHost, servedByBug, apiBase, apiUrl, settingsParams, sharedSettings,
  sortBugs, bugStatus, summarize,
} from "./bugs.js";

test("normalizeHost accepts hosts, IPs and ports; strips scheme and slash", () => {
  assert.equal(normalizeHost(" http://Fireflies.local/ "), "fireflies.local");
  assert.equal(normalizeHost("192.168.4.1"), "192.168.4.1");
  assert.equal(normalizeHost("localhost:8081"), "localhost:8081");
  assert.equal(normalizeHost("fireflies"), "fireflies.local");
});

test("normalizeHost rejects paths, injection and empty input", () => {
  for (const bad of ["", null, "bug.local/x", "bug.local?x=1", "<script>", "a b", "-bad.local"]) {
    assert.equal(normalizeHost(bad), null, String(bad));
  }
});

test("servedByBug is false for local dev and file pages", () => {
  assert.equal(servedByBug({ protocol: "http:", hostname: "localhost" }), false);
  assert.equal(servedByBug({ protocol: "http:", hostname: "127.0.0.1" }), false);
  assert.equal(servedByBug({ protocol: "file:", hostname: "" }), false);
  assert.equal(servedByBug({ protocol: "http:", hostname: "fireflies.local" }), true);
  assert.equal(servedByBug({ protocol: "http:", hostname: "192.168.4.1" }), true);
});

test("apiUrl builds same-origin and cross-origin URLs", () => {
  assert.equal(apiUrl(apiBase(true, "x"), "/api/bugs"), "/api/bugs");
  assert.equal(apiUrl(apiBase(false, "fireflies.local"), "/api/hop", { id: "all" }),
    "http://fireflies.local/api/hop?id=all");
});

test("settingsParams encodes booleans, colours and numbers", () => {
  assert.deepEqual(
    settingsParams({ hopping: true, stayLit: false, colour: "#FFC940", glowColour: "#B4FF28", minWait: 5, name: "Porch" }),
    { hopping: "1", stayLit: "0", colour: "ffc940", glowColour: "b4ff28", minWait: "5", name: "Porch" },
  );
  assert.equal(apiUrl("", "/api/settings", { id: "all", ...settingsParams({ hopping: false }) }),
    "/api/settings?id=all&hopping=0");
});

test("sharedSettings drops name and servo pulses", () => {
  const s = { hopping: true, colour: "#ffffff", minWait: 3, pulseA: 500, pulseB: 2500 };
  assert.deepEqual(sharedSettings(s), { hopping: true, colour: "#ffffff", minWait: 3 });
});

test("sortBugs: master, then online, then offline; natural name order within; no mutation", () => {
  const bugs = [
    { id: "c", name: "Bug 10", online: true }, { id: "a", name: "bug 2", online: true },
    { id: "m", name: "Zed", master: true, online: true }, { id: "o", name: "Aardvark", online: false },
  ];
  assert.deepEqual(sortBugs(bugs).map((b) => b.id), ["m", "a", "c", "o"]);
  assert.equal(bugs[0].id, "c");
});

test("bugStatus and summarize", () => {
  const hopping = { online: true, settings: { hopping: true } };
  const resting = { online: true, settings: { hopping: false } };
  const offline = { online: false, settings: { hopping: true } };
  assert.equal(bugStatus(hopping), "hopping");
  assert.equal(bugStatus(resting), "resting");
  assert.equal(bugStatus(offline), "offline");
  assert.deepEqual(summarize([hopping, resting, offline]), { total: 3, hopping: 1, offline: 1 });
});

test("isOutdated compares against the master's build", async () => {
  const { isOutdated } = await import("./bugs.js");
  assert.equal(isOutdated({ build: "aaaa" }, "aaaa"), false);
  assert.equal(isOutdated({ build: "aaaa" }, "bbbb"), true);
  assert.equal(isOutdated({}, "bbbb"), false);
});

test("batteryInfo formats volts and flags low batteries", async () => {
  const { batteryInfo } = await import("./bugs.js");
  assert.deepEqual(batteryInfo(4.52), { text: "4.52 V", low: false });
  assert.deepEqual(batteryInfo(3.5), { text: "3.50 V", low: true });
  assert.deepEqual(batteryInfo(null), { text: "—", low: false });
});
