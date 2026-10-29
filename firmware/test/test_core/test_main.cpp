#include <stddef.h>
#include <unity.h>

#include "election.h"
#include "firefly.h"
#include "motion.h"
#include "protocol.h"
#include "roster.h"
#include "settings.h"

using namespace ff;

static const Mac A = {{0, 0, 0, 0, 0, 1}};
static const Mac B = {{0, 0, 0, 0, 0, 2}};
static const Mac C = {{0, 0, 0, 0, 0, 3}};

void setUp() {}
void tearDown() {}

// ---------- mac ----------

void test_mac_hex_round_trip() {
  Mac m = {{0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6}}, back{};
  char hex[13];
  macToHex(m, hex);
  TEST_ASSERT_EQUAL_STRING("a1b2c3d4e5f6", hex);
  TEST_ASSERT_TRUE(macFromHex(hex, back));
  TEST_ASSERT_TRUE(m == back);
  TEST_ASSERT_FALSE(macFromHex("a1b2c3", back));
  TEST_ASSERT_FALSE(macFromHex("zzb2c3d4e5f6", back));
}

// ---------- settings ----------

void test_default_name_from_mac() {
  Mac m = {{0, 0, 0, 0x3f, 0x2a, 0x1c}};
  TEST_ASSERT_EQUAL_STRING("Firefly 3f2a1c", defaultSettings(m).name);
  TEST_ASSERT_EQUAL(0, defaultSettings(m).onboardLed);  // blue LED off by default
}

// Storage migration relies on new fields being appended: the old layout must be a prefix.
void test_new_fields_are_appended() {
  TEST_ASSERT_EQUAL(sizeof(Settings) - 2, offsetof(Settings, glowBrightMin));
  TEST_ASSERT_EQUAL(sizeof(Settings) - 4, offsetof(Settings, glowEveryMinSec));
  TEST_ASSERT_EQUAL(sizeof(Settings) - 9, offsetof(Settings, rainbowHop));
  TEST_ASSERT_EQUAL(sizeof(Settings) - 18, offsetof(Settings, fireflyGlow));
  TEST_ASSERT_EQUAL(sizeof(Settings) - 20, offsetof(Settings, ledDelayMs));
  TEST_ASSERT_EQUAL(sizeof(Settings) - 21, offsetof(Settings, onboardLed));
}

void test_glow_settings_parse_and_default() {
  Settings d = defaultSettings(A);
  TEST_ASSERT_EQUAL(1, d.fireflyGlow);
  TEST_ASSERT_EQUAL(1, d.glowPulsesMin);
  TEST_ASSERT_EQUAL(6, d.glowPulses);
  TEST_ASSERT_EQUAL(100, d.glowPulseMinMs);
  TEST_ASSERT_EQUAL(30, d.glowEverySec);
  Settings p{};
  uint32_t mask = 0;
  TEST_ASSERT_NULL(parseSettingParam("glowColour", "00ff80", p, mask));
  TEST_ASSERT_NULL(parseSettingParam("glowPulsesMax", "99", p, mask));
  TEST_ASSERT_NULL(parseSettingParam("fireflyGlow", "0", p, mask));
  TEST_ASSERT_EQUAL_HEX8(0x80, p.glowB);
  TEST_ASSERT_EQUAL(10, p.glowPulses);
  TEST_ASSERT_EQUAL(kGlowColour | kGlowPulses | kFireflyGlow, mask);
  Settings out = applyMasked(d, p, mask);
  TEST_ASSERT_EQUAL(0, out.fireflyGlow);
  TEST_ASSERT_EQUAL_HEX8(0xff, out.r);  // hop colour untouched
}

void test_led_delay_default_and_clamp() {
  TEST_ASSERT_EQUAL(100, defaultSettings(A).ledDelayMs);
  Settings p{};
  uint32_t mask = 0;
  TEST_ASSERT_NULL(parseSettingParam("ledDelayMs", "5000", p, mask));
  TEST_ASSERT_EQUAL(1000, p.ledDelayMs);
  TEST_ASSERT_EQUAL(kLedDelay, mask);
}

void test_parse_numbers_clamp_and_set_mask() {
  Settings p{};
  uint32_t mask = 0;
  TEST_ASSERT_NULL(parseSettingParam("minWait", "5", p, mask));
  TEST_ASSERT_NULL(parseSettingParam("brightness", "500", p, mask));
  TEST_ASSERT_EQUAL(5, p.minWait);
  TEST_ASSERT_EQUAL(100, p.brightness);
  TEST_ASSERT_EQUAL(kMinWait | kBrightness, mask);
}

void test_parse_rejects_bad_input() {
  Settings p{};
  uint32_t mask = 0;
  TEST_ASSERT_NOT_NULL(parseSettingParam("minWait", "5s", p, mask));
  TEST_ASSERT_NOT_NULL(parseSettingParam("minWait", "", p, mask));
  TEST_ASSERT_NOT_NULL(parseSettingParam("hopping", "yes", p, mask));
  TEST_ASSERT_NOT_NULL(parseSettingParam("colour", "#ffc940", p, mask));
  TEST_ASSERT_NOT_NULL(parseSettingParam("name", "", p, mask));
  TEST_ASSERT_NOT_NULL(parseSettingParam("name", "this name is far too long", p, mask));
  TEST_ASSERT_NOT_NULL(parseSettingParam("bogus", "1", p, mask));
  TEST_ASSERT_EQUAL(0, mask);
}

void test_parse_bool_colour_name() {
  Settings p{};
  uint32_t mask = 0;
  TEST_ASSERT_NULL(parseSettingParam("hopping", "0", p, mask));
  TEST_ASSERT_NULL(parseSettingParam("stayLit", "true", p, mask));
  TEST_ASSERT_NULL(parseSettingParam("holdServo", "1", p, mask));
  TEST_ASSERT_EQUAL(1, p.holdServo);
  TEST_ASSERT_NULL(parseSettingParam("onboardLed", "1", p, mask));
  TEST_ASSERT_EQUAL(1, p.onboardLed);
  mask &= ~(kHoldServo | kOnboardLed);
  TEST_ASSERT_NULL(parseSettingParam("colour", "FF8000", p, mask));
  TEST_ASSERT_NULL(parseSettingParam("name", "Porch", p, mask));
  TEST_ASSERT_EQUAL(0, p.hopping);
  TEST_ASSERT_EQUAL(1, p.stayLit);
  TEST_ASSERT_EQUAL_HEX8(0xff, p.r);
  TEST_ASSERT_EQUAL_HEX8(0x80, p.g);
  TEST_ASSERT_EQUAL_STRING("Porch", p.name);
  TEST_ASSERT_EQUAL(kHopping | kStayLit | kColour | kName, mask);
}

void test_apply_masked_only_touches_masked_fields() {
  Settings base = defaultSettings(A), patch{};
  patch.minWait = 3;
  patch.pulseA = 999;  // not in mask
  Settings out = applyMasked(base, patch, kMinWait);
  TEST_ASSERT_EQUAL(3, out.minWait);
  TEST_ASSERT_EQUAL(500, out.pulseA);
  TEST_ASSERT_EQUAL_STRING(base.name, out.name);
  TEST_ASSERT_EQUAL(10, base.minWait);  // base untouched
}

// ---------- protocol ----------

void test_parse_packet_checks_header_and_size() {
  BeaconPacket b{makeHeader(PacketType::Beacon, 0x4742, 1), 1000, 0};
  PacketType t;
  TEST_ASSERT_TRUE(parsePacket((const uint8_t*)&b, sizeof b, 0x4742, t));
  TEST_ASSERT_TRUE(t == PacketType::Beacon);
  TEST_ASSERT_FALSE(parsePacket((const uint8_t*)&b, sizeof b, 0x1111, t));      // other group
  TEST_ASSERT_FALSE(parsePacket((const uint8_t*)&b, sizeof b - 1, 0x4742, t));  // truncated
  b.h.magic[0] = 'X';
  TEST_ASSERT_FALSE(parsePacket((const uint8_t*)&b, sizeof b, 0x4742, t));
}

void test_command_targeting() {
  CommandPacket c{};
  c.target = kBroadcast;
  TEST_ASSERT_TRUE(isTargeted(c, A));
  c.target = B;
  TEST_ASSERT_FALSE(isTargeted(c, A));
  TEST_ASSERT_TRUE(isTargeted(c, B));
}

// ---------- election ----------

void test_becomes_master_when_nothing_heard() {
  Election e(A);
  e.begin(0, 3000);
  e.tick(2999);
  TEST_ASSERT_TRUE(e.role() == Role::Searching);
  e.tick(3000);
  TEST_ASSERT_TRUE(e.role() == Role::Master);
}

void test_follows_beacon_while_searching() {
  Election e(A);
  e.begin(0, 3000);
  e.onBeacon(1000, B, 50000);
  e.tick(4000);
  TEST_ASSERT_TRUE(e.role() == Role::Follower);
  TEST_ASSERT_TRUE(e.master() == B);
}

void test_two_masters_longer_uptime_wins() {
  Election older(A), newer(B);
  older.begin(0, 0);
  older.tick(0);
  newer.begin(0, 0);
  newer.tick(0);
  // older has been up 60 s, newer 20 s
  newer.onBeacon(20000, A, 60000);
  older.onBeacon(60000, B, 20000);
  TEST_ASSERT_TRUE(newer.role() == Role::Follower);
  TEST_ASSERT_TRUE(older.role() == Role::Master);
}

void test_uptime_tie_goes_to_lower_mac() {
  TEST_ASSERT_TRUE(outranks(1000, A, 1000, B));
  TEST_ASSERT_FALSE(outranks(1000, B, 1000, A));
}

void test_follower_switches_to_higher_ranked_master() {
  Election e(C);
  e.begin(0, 3000);
  e.onBeacon(100, B, 10000);   // follow B (up 10 s)
  e.onBeacon(200, A, 5000);    // A is younger -> ignore
  TEST_ASSERT_TRUE(e.master() == B);
  e.onBeacon(300, A, 90000);   // A is older -> switch
  TEST_ASSERT_TRUE(e.master() == A);
}

void test_follower_searches_again_after_timeout() {
  Election e(A);
  e.begin(0, 3000);
  e.onBeacon(1000, B, 50000);
  e.tick(1000 + kMasterTimeoutMs);
  TEST_ASSERT_TRUE(e.role() == Role::Follower);
  e.tick(1001 + kMasterTimeoutMs);
  TEST_ASSERT_TRUE(e.role() == Role::Searching);
  TEST_ASSERT_FALSE(e.armed());
  e.tick(999999);  // not armed: must not self-elect until begin() again
  TEST_ASSERT_TRUE(e.role() == Role::Searching);
}

void test_ignores_own_beacon() {
  Election e(A);
  e.begin(0, 3000);
  e.onBeacon(10, A, 10);
  TEST_ASSERT_TRUE(e.role() == Role::Searching);
}

// ---------- roster ----------

static StatusBody status(uint32_t uptime, uint8_t lit) {
  StatusBody s{};
  s.uptimeMs = uptime;
  s.lit = lit;
  s.settings = defaultSettings(A);
  return s;
}

void test_roster_dirty_tracking() {
  Roster r;
  int published = 0;
  auto count = [&](const RosterEntry&) { published++; return true; };
  r.upsert(A, status(1000, 0), 0);
  r.drainDirty(count, 10);
  TEST_ASSERT_EQUAL(1, published);
  r.upsert(A, status(4000, 0), 3000);  // uptime only
  r.drainDirty(count, 10);
  TEST_ASSERT_EQUAL(1, published);
  r.upsert(A, status(5000, 1), 4000);  // lit changed
  r.drainDirty(count, 10);
  TEST_ASSERT_EQUAL(2, published);
  TEST_ASSERT_EQUAL(1, r.size());
}

void test_roster_drain_respects_limit_and_refusal() {
  Roster r;
  r.upsert(A, status(0, 0), 0);
  r.upsert(B, status(0, 0), 0);
  r.upsert(C, status(0, 0), 0);
  int taken = 0;
  r.drainDirty([&](const RosterEntry&) { taken++; return true; }, 2);
  TEST_ASSERT_EQUAL(2, taken);
  r.drainDirty([](const RosterEntry&) { return false; }, 5);  // busy: nothing cleared
  taken = 0;
  r.drainDirty([&](const RosterEntry&) { taken++; return true; }, 5);
  TEST_ASSERT_EQUAL(1, taken);  // only the one left over
}

void test_roster_removes_gone_bugs() {
  Roster r;
  r.upsert(A, status(0, 0), 0);
  r.upsert(B, status(0, 0), Roster::kGoneMs);
  r.sweep(Roster::kGoneMs + 1);  // A offline, B still fresh
  int gone = 0;
  r.removeGone(Roster::kGoneMs + 1, [&](const Mac& m) { gone++; TEST_ASSERT_TRUE(m == A); });
  TEST_ASSERT_EQUAL(1, gone);
  TEST_ASSERT_EQUAL(1, r.size());
  TEST_ASSERT_NULL(r.find(A));
  TEST_ASSERT_NOT_NULL(r.find(B));
}

void test_roster_offline_and_back() {
  Roster r;
  r.upsert(A, status(0, 0), 0);
  r.drainDirty([](const RosterEntry&) { return true; }, 10);
  r.sweep(Roster::kOfflineMs);
  TEST_ASSERT_TRUE(r.at(0).online);
  r.sweep(Roster::kOfflineMs + 1);
  TEST_ASSERT_FALSE(r.at(0).online);
  TEST_ASSERT_TRUE(r.at(0).dirty);
  r.upsert(A, status(0, 0), 20000);
  TEST_ASSERT_TRUE(r.at(0).online);
}

void test_roster_counts_restarts_and_dropouts() {
  Roster r;
  r.upsert(A, status(60000, 0), 0);
  r.upsert(A, status(63000, 0), 3000);
  TEST_ASSERT_EQUAL(0, r.at(0).restarts);
  r.upsert(A, status(2000, 0), 6000);  // uptime went backwards: it rebooted
  TEST_ASSERT_EQUAL(1, r.at(0).restarts);
  TEST_ASSERT_TRUE(r.at(0).dirty);
  r.sweep(6000 + Roster::kOfflineMs + 1);
  TEST_ASSERT_EQUAL(1, r.at(0).dropouts);
  r.sweep(6000 + Roster::kOfflineMs + 5000);  // still offline: not counted twice
  TEST_ASSERT_EQUAL(1, r.at(0).dropouts);
  r.upsert(A, status(30000, 0), 40000);  // back, uptime kept rising: not a restart
  TEST_ASSERT_EQUAL(1, r.at(0).restarts);
  TEST_ASSERT_TRUE(r.at(0).online);
}

void test_roster_capacity() {
  Roster r;
  for (size_t i = 0; i < Roster::kCapacity; i++) {
    Mac m = {{1, 0, 0, 0, 0, (uint8_t)i}};
    TEST_ASSERT_TRUE(r.upsert(m, status(0, 0), 0));
  }
  TEST_ASSERT_FALSE(r.upsert(C, status(0, 0), 0));
}

// ---------- firefly glow ----------

static uint32_t rngMax(uint32_t n) { return n - 1; }   // always the largest choice
static uint32_t rngZero(uint32_t) { return 0; }        // always the smallest choice

void test_pulse_shape() {
  TEST_ASSERT_EQUAL(0, pulseShape(0, 600));
  TEST_ASSERT_EQUAL(255, pulseShape(300, 600));
  TEST_ASSERT_EQUAL(0, pulseShape(600, 600));
  TEST_ASSERT_TRUE(pulseShape(150, 600) > 0 && pulseShape(150, 600) < 255);
}

void test_pick_in_range() {
  TEST_ASSERT_EQUAL(3, pickInRange(3, 9, rngZero));
  TEST_ASSERT_EQUAL(9, pickInRange(3, 9, rngMax));
  TEST_ASSERT_EQUAL(3, pickInRange(9, 3, rngZero));  // reversed bounds are fine
  TEST_ASSERT_EQUAL(5, pickInRange(5, 5, rngMax));
}

void test_glow_waits_then_pulses() {
  FireflyGlow g;
  GlowParams p{1, 6, 600, 600, 2, 30};
  TEST_ASSERT_EQUAL(0, g.level(0, p, rngZero));  // not started
  g.startAt(0, 1000);
  TEST_ASSERT_EQUAL(0, g.level(999, p, rngZero));
  g.level(1000, p, rngZero);                      // burst starts
  TEST_ASSERT_EQUAL(255, g.level(1300, p, rngZero));  // peak of a 600 ms pulse
}

void test_glow_burst_uses_ranges() {
  FireflyGlow g;
  GlowParams p{2, 4, 100, 300, 5, 9};
  g.startAt(0, 0);
  g.level(0, p, rngMax);                    // 4 pulses (max), each 300 ms (max), gaps 1.2x = 360 ms
  TEST_ASSERT_TRUE(g.level(150, p, rngMax) > 0);
  uint32_t now = 0;
  for (int pulse = 0; pulse < 4; pulse++) {  // walk through the burst
    now += 300 + 360;
    g.level(now, p, rngMax);
  }
  TEST_ASSERT_EQUAL(0, g.level(now + 8000, p, rngMax));  // next burst after 9 s (max)
  g.level(now + 9000, p, rngMax);
  TEST_ASSERT_EQUAL(2, g.bursts());
}

void test_glow_zero_wait_bursts_back_to_back() {
  FireflyGlow g;
  GlowParams p{1, 1, 100, 100, 0, 0};
  g.startAt(0, 0);
  g.level(0, p, rngZero);
  g.level(160, p, rngZero);  // pulse 100 + gap 60: burst over, next immediately
  g.level(160, p, rngZero);
  TEST_ASSERT_EQUAL(2, g.bursts());
}

void test_glow_stop() {
  FireflyGlow g;
  GlowParams p{1, 3, 600, 600, 2, 30};
  g.startAt(0, 0);
  g.level(0, p, rngMax);
  g.stop();
  TEST_ASSERT_EQUAL(0, g.level(300, p, rngMax));
}

// A cycling "random" source, so successive picks differ.
static uint32_t rngCounter = 0;
static uint32_t rngCycle(uint32_t n) { return (rngCounter++ * 7) % n; }

// Each pulse in one burst rolls its own length from the range.
void test_glow_pulse_lengths_vary_within_burst() {
  FireflyGlow g;
  GlowParams p{4, 4, 100, 600, 30, 30};  // exactly 4 pulses, each 100..600 ms
  rngCounter = 0;
  g.startAt(0, 0);
  uint32_t now = 0, lengths[4] = {}, pulse = 0, litFor = 0;
  bool wasLit = false;
  while (pulse < 4 && now < 20000) {  // measure how long each pulse is lit, 1 ms steps
    bool lit = g.level(now, p, rngCycle) > 0;
    if (lit) litFor++;
    if (wasLit && !lit) lengths[pulse++] = litFor, litFor = 0;
    wasLit = lit;
    now++;
  }
  TEST_ASSERT_EQUAL(4, pulse);
  bool allSame = lengths[0] == lengths[1] && lengths[1] == lengths[2] && lengths[2] == lengths[3];
  TEST_ASSERT_FALSE(allSame);
  for (uint32_t len : lengths) TEST_ASSERT_TRUE(len >= 90 && len <= 600);
}

// Peak brightness of each pulse follows the per-pulse brightness range.
void test_glow_pulse_brightness() {
  FireflyGlow g;
  GlowParams fixedDim{1, 1, 600, 600, 30, 30, 50, 50};
  g.startAt(0, 0);
  g.level(0, fixedDim, rngMax);
  TEST_ASSERT_INT_WITHIN(1, 127, g.level(300, fixedDim, rngMax));  // 50% of 255 at the peak

  FireflyGlow r;
  GlowParams range{1, 1, 600, 600, 30, 30, 20, 90};
  r.startAt(0, 0);
  r.level(0, range, rngZero);  // rngZero picks the minimum: 20%
  TEST_ASSERT_INT_WITHIN(1, 51, r.level(300, range, rngZero));
}

void test_glow_counts_bursts() {
  FireflyGlow g;
  GlowParams p{1, 1, 100, 100, 1, 1};
  g.startAt(0, 0);
  TEST_ASSERT_EQUAL(0, g.bursts());
  g.level(0, p, rngZero);
  TEST_ASSERT_EQUAL(1, g.bursts());
  g.level(160, p, rngZero);   // burst over (100 + 60 gap), next 1 s later
  g.level(1160, p, rngZero);
  TEST_ASSERT_EQUAL(2, g.bursts());
}

void test_rainbow_and_glow_random_settings() {
  Settings p{};
  uint32_t mask = 0;
  TEST_ASSERT_NULL(parseSettingParam("rainbowHop", "1", p, mask));
  TEST_ASSERT_NULL(parseSettingParam("glowColourMode", "jump", p, mask));
  TEST_ASSERT_NOT_NULL(parseSettingParam("glowColourMode", "purple", p, mask));
  TEST_ASSERT_EQUAL(kRainbowHop | kGlowColourMode, mask);
  Settings out = applyMasked(defaultSettings(A), p, mask);
  TEST_ASSERT_EQUAL(1, out.rainbowHop);
  TEST_ASSERT_EQUAL(kGlowMatchJump, out.glowColourMode);
  TEST_ASSERT_EQUAL_STRING("jump", glowModeName(out.glowColourMode));
  TEST_ASSERT_EQUAL(kGlowFixed, defaultSettings(A).glowColourMode);
  TEST_ASSERT_EQUAL(0, defaultSettings(A).rainbowHop);
}

// ---------- servo swing ----------

void test_ease_in_out() {
  TEST_ASSERT_EQUAL(0, easeInOut(0));
  TEST_ASSERT_EQUAL(500, easeInOut(500));
  TEST_ASSERT_EQUAL(1000, easeInOut(1000));
  TEST_ASSERT_TRUE(easeInOut(100) < 100);   // slow start
  TEST_ASSERT_TRUE(easeInOut(900) > 900);   // slow landing
}

void test_swing_pulse_moves_gradually_both_ways() {
  TEST_ASSERT_EQUAL(500, swingPulse(500, 2500, 0, 1000));
  TEST_ASSERT_EQUAL(1500, swingPulse(500, 2500, 500, 1000));   // halfway
  TEST_ASSERT_EQUAL(2500, swingPulse(500, 2500, 1000, 1000));  // landed
  TEST_ASSERT_EQUAL(2500, swingPulse(500, 2500, 5000, 1000));  // stays landed
  TEST_ASSERT_EQUAL(1500, swingPulse(2500, 500, 500, 1000));   // reverse direction
  uint16_t prev = 2500;
  for (uint32_t t = 0; t <= 1000; t += 50) {                   // monotonic on the way back
    uint16_t us = swingPulse(2500, 500, t, 1000);
    TEST_ASSERT_TRUE(us <= prev);
    prev = us;
  }
  TEST_ASSERT_EQUAL(2500, swingPulse(500, 2500, 0, 0));        // zero length = jump
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_mac_hex_round_trip);
  RUN_TEST(test_default_name_from_mac);
  RUN_TEST(test_new_fields_are_appended);
  RUN_TEST(test_glow_settings_parse_and_default);
  RUN_TEST(test_led_delay_default_and_clamp);
  RUN_TEST(test_parse_numbers_clamp_and_set_mask);
  RUN_TEST(test_parse_rejects_bad_input);
  RUN_TEST(test_parse_bool_colour_name);
  RUN_TEST(test_apply_masked_only_touches_masked_fields);
  RUN_TEST(test_parse_packet_checks_header_and_size);
  RUN_TEST(test_command_targeting);
  RUN_TEST(test_becomes_master_when_nothing_heard);
  RUN_TEST(test_follows_beacon_while_searching);
  RUN_TEST(test_two_masters_longer_uptime_wins);
  RUN_TEST(test_uptime_tie_goes_to_lower_mac);
  RUN_TEST(test_follower_switches_to_higher_ranked_master);
  RUN_TEST(test_follower_searches_again_after_timeout);
  RUN_TEST(test_ignores_own_beacon);
  RUN_TEST(test_roster_dirty_tracking);
  RUN_TEST(test_roster_drain_respects_limit_and_refusal);
  RUN_TEST(test_roster_removes_gone_bugs);
  RUN_TEST(test_roster_offline_and_back);
  RUN_TEST(test_roster_counts_restarts_and_dropouts);
  RUN_TEST(test_roster_capacity);
  RUN_TEST(test_pulse_shape);
  RUN_TEST(test_pick_in_range);
  RUN_TEST(test_glow_waits_then_pulses);
  RUN_TEST(test_glow_burst_uses_ranges);
  RUN_TEST(test_glow_zero_wait_bursts_back_to_back);
  RUN_TEST(test_glow_stop);
  RUN_TEST(test_glow_pulse_lengths_vary_within_burst);
  RUN_TEST(test_glow_pulse_brightness);
  RUN_TEST(test_glow_counts_bursts);
  RUN_TEST(test_rainbow_and_glow_random_settings);
  RUN_TEST(test_ease_in_out);
  RUN_TEST(test_swing_pulse_moves_gradually_both_ways);
  return UNITY_END();
}
