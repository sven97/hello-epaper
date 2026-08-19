# Wi-Fi reconnect recovery: radio power-cycle + stuck-error escalation

## Context

Observed on hardware: with a 10-minute refresh interval, the frame ran fine
for 1-2 days, then got stuck showing one photo for days. Pressing KEY1
(`runStatusMode()`) drew the cached photo/status screen fine (no network
needed for that), then `connectWifi()` failed against the *same saved
credentials* and WiFiManager fell back to its captive portal — i.e. Wi-Fi
had been failing to reconnect the entire stuck period, not just failing to
fetch a photo.

This is architecturally expected, not a bug in the retry logic itself:
`doFetchCycle(false)` (unattended timer wake) already retries forever, once
per scheduled wake, by design (`net.cpp`/`main.cpp`, see prior discussion in
this session). But every retry is the exact same `wm.autoConnect()` call
against a radio that's never reset, and `allowPortal=false` means it can
never fall back to reprovisioning on its own. Two consequences observed:

1. If whatever broke the connection is something a plain retry-with-same-
   state can't clear (leading hypothesis: ESP-IDF's Wi-Fi driver persists a
   "fast connect" BSSID/channel hint in flash across deep sleep/reboots: if
   the router's channel or BSSID changed, e.g. it rebooted or auto-channel
   re-negotiated, every subsequent `autoConnect()` call can keep retrying
   the stale hint and fail identically forever) — the frame retries
   correctly per the existing design, but never actually recovers.
2. Nothing on the panel or in a way the user would casually notice ever
   indicates this is happening — `devLog` is RAM-only (see
   `2026-08-13-debug-visibility-design.md`) and doesn't survive deep sleep,
   so even the failure log from the stuck period is gone by the time
   anyone looks. The photo just goes stale silently.

This spec adds two independent mitigations to `connectWifi()`/
`doFetchCycle()`:

- **Radio power-cycle before retrying**, once a wake has already failed at
  least once — addresses hypothesis (1), without touching saved
  credentials or opening any portal.
- **A one-time visible error screen** once failures have persisted well
  past what a transient outage would explain — addresses (2), so a stuck
  frame is discoverable by looking at it instead of only by noticing you
  haven't seen a new photo in a while.

Not in scope: guaranteeing hypothesis (1) is the actual root cause (no way
to confirm without hardware logs from a future failure — see Out of
scope), and not auto-opening the portal unattended (decided against: it
only helps if a human happens to be nearby when it fires, and firing it in
front of nobody is pure extra radio/battery cost for the same outcome as
just showing the error screen).

## Failure-streak tracking

New `RTC_DATA_ATTR` state in `main.cpp`, alongside the existing
`bootCount`/`lastVbatMv` (same "survives deep sleep, resets on power
loss/reflash" contract — appropriate here since a power cycle already
resets the Wi-Fi radio too):

```cpp
RTC_DATA_ATTR uint32_t fetchFailStreak = 0;
RTC_DATA_ATTR bool stuckErrorShown = false;
```

- `fetchFailStreak` counts **consecutive full `doFetchCycle()` failures**,
  regardless of which stage failed (Wi-Fi connect or the HTTP GET) — from
  the user's perspective "stuck for days" doesn't care which stage broke.
- Incremented at the top of both failure branches in `doFetchCycle()`.
- Reset to `0` (and `stuckErrorShown = false`) only on a fully successful
  cycle (photo actually rendered and `epaper.update()` called) — a Wi-Fi
  connect that succeeds but is followed by an HTTP failure does **not**
  reset the streak, since the user is still looking at a stale photo
  either way.
- Quiet-hours/pinned fast-exits (`main.cpp:164-177`, `quickSleep()`) never
  reach `doFetchCycle()`, so they correctly don't advance or reset the
  streak — no attempt happened, nothing to count.
- Applies to both `doFetchCycle(false)` (timer) and `doFetchCycle(true)`
  (button/power-on) call sites uniformly — an interactive failure still
  counts toward and can clear the streak, since it's a real signal about
  network state either way.

## `connectWifi()`: radio power-cycle on retry

Signature grows one parameter:

```cpp
bool connectWifi(bool allowPortal = true, bool forceRadioReset = false);
```

Caller passes `forceRadioReset = (fetchFailStreak > 0)` — i.e. the first
failure of a new outage gets a plain retry (today's behavior, cheapest
case, covers ordinary transient blips), and *every* retry after that
forces a fresh radio state first:

```cpp
if (forceRadioReset) {
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_STA);
}
```
...inserted before `wm.autoConnect(AP_NAME)`. This does **not** touch
stored credentials (no `eraseap`/`erase` calls) — only power-cycles the
radio driver, which is the well-documented, low-risk way to clear stuck
internal radio/connection-manager state on ESP32-Arduino. It's cheap
(~100-200 ms) so applying it on every retry after the first (rather than
gating it behind its own separate threshold) keeps this simple and costs
nothing meaningful even on the interactive path.

This is explicitly a **hypothesis-driven mitigation**, not a verified fix
for what actually happened on the bench — flagged here so it isn't later
mistaken for a confirmed root-cause fix.

## Stuck-error escalation

New pure/testable helper, `src/logic/stuck_error.h` (mirrors
`logic/quiet_hours.h`'s style — host-testable, no Arduino deps):

```cpp
#pragma once
#include <cstdint>

// 6 h: long enough that no ordinary transient outage (AP reboot, brief
// ISP blip) should trigger it, short enough that a genuinely stuck frame
// is discovered same-day rather than after a multi-day silent gap.
constexpr uint32_t STUCK_ERROR_SECS = 6UL * 3600;

// streak * sleepSecs is only an estimate of elapsed wall-clock time (it
// undercounts if quiet hours delayed wakes in between) -- errs toward
// waiting longer before flagging, which is the safe direction here.
inline bool shouldShowStuckError(uint32_t fetchFailStreak, uint32_t sleepSecs,
                                 bool alreadyShown) {
    if (alreadyShown || fetchFailStreak == 0) return false;
    return (uint64_t)fetchFailStreak * sleepSecs >= STUCK_ERROR_SECS;
}
```

`doFetchCycle()` calls this after incrementing the streak on either
failure branch (unattended wakes only — `interactive` wakes already show
an error immediately every time, so layering this on top would be
redundant):

```cpp
fetchFailStreak++;
if (!interactive && shouldShowStuckError(fetchFailStreak, settings.sleepSecs,
                                         stuckErrorShown)) {
    stuckErrorShown = true;
    showError("Wi-Fi hasn't reconnected in over 6h (" +
              String(fetchFailStreak) + " attempts) — press KEY1 to check");
}
```

Drawn **once** per outage (guarded by `stuckErrorShown`), not redrawn on
every subsequent failed wake — avoids burning a full panel refresh (and
associated e-ink wear) every 10 minutes for a condition that, once shown,
doesn't need repeating until it either recovers (streak resets) or the
board reboots (RTC memory clears, `stuckErrorShown` starts `false` again).

## Implementation surface

- `src/logic/stuck_error.h` (new) — `STUCK_ERROR_SECS` +
  `shouldShowStuckError()`.
- `src/main.cpp`: `fetchFailStreak`/`stuckErrorShown` RTC state;
  `doFetchCycle()` increments/resets the streak and calls
  `shouldShowStuckError()` on unattended failures.
- `src/net.cpp` / `src/net.h`: `connectWifi()` gains `forceRadioReset`
  param and the `WiFi.mode(WIFI_OFF)`→`WIFI_STA` power-cycle.
- No changes to `power.cpp`/`goToSleep()` — the sleep-interval math is
  already unconditional (see prior discussion); this spec doesn't touch
  it.

## Testing

- New native test `test/test_stuck_error` (`pio test -e native`) for
  `shouldShowStuckError()`: below threshold, exactly at threshold,
  `alreadyShown` suppression, `fetchFailStreak == 0` no-op.
- `pio run -e ee02` build check.
- Hardware verification (per this project's usual bar): can't reproduce
  the original router-side failure on demand, so verify what's
  mechanically checkable —
  - `/log` (or serial) shows the `WiFi.mode(WIFI_OFF)`→`WIFI_STA` cycle
    firing on the second consecutive failure, not the first.
  - Force `fetchFailStreak` past the threshold (temporarily lower
    `STUCK_ERROR_SECS` for the bench test) and confirm the error screen
    draws once, then stays absent on further failed wakes, then confirm a
    subsequent successful fetch clears back to the normal photo and resets
    the streak.
  - Confirm quiet-hours/pinned wakes still fast-exit without incrementing
    the streak.

## Out of scope

- Confirming the actual root cause of the original multi-day stall (would
  need to catch it live with persistent logging, which nothing here adds).
- Auto-opening the WiFiManager portal unattended — explicitly decided
  against (see Context).
- Persisting `fetchFailStreak`/failure history across power loss or
  surfacing it on the status/debug screens — this spec only adds the
  one-time error screen; richer diagnostics are a separate feature if the
  problem recurs.
- Any change to the sleep-interval/scheduling logic itself — unattended
  wakes still happen at the same cadence regardless of failure streak.
