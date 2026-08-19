# Wi-Fi Reconnect Recovery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When unattended fetch cycles keep failing, power-cycle the Wi-Fi
radio before each retry after the first (instead of repeating the exact
same `wm.autoConnect()` call against a never-reset radio), and draw the
existing error screen once — not every wake — after failures have
persisted well past what a transient outage explains, so a stuck frame is
discoverable by looking at it.

**Architecture:** A new pure/testable helper (`logic/stuck_error.h`,
mirrors `logic/quiet_hours.h`) decides *when* to escalate from a
consecutive-failure count + the configured refresh interval.
`connectWifi()` grows a `forceRadioReset` parameter that power-cycles the
radio (`WiFi.mode(WIFI_OFF)` → `WIFI_STA`) without touching saved
credentials. `doFetchCycle()` in `main.cpp` owns the actual
`RTC_DATA_ATTR` streak/flag state (same lifetime contract as the existing
`bootCount`/`lastVbatMv`), increments it on either failure branch, resets
it only on a fully successful cycle, and calls the new helper to decide
whether to draw `showError()` on an otherwise-silent unattended wake.

**Tech Stack:** Arduino/ESP32 (PlatformIO, `framework = arduino`), Unity
for native host tests. No new files outside `src/logic/`, no `lib_deps`
changes, no partition/settings changes.

## Global Constraints

- `STUCK_ERROR_SECS = 6 * 3600` (6 hours) — single tunable constant in
  `src/logic/stuck_error.h`.
- The radio power-cycle never erases saved Wi-Fi credentials (no
  `WiFi.disconnect(..., true)` / `eraseap`) — only `WiFi.mode()` toggles.
- `fetchFailStreak`/`stuckErrorShown` are `RTC_DATA_ATTR` (survive deep
  sleep, reset on power loss/reflash) — same contract as the existing
  `bootCount`/`lastVbatMv` in `main.cpp`.
- The escalation error screen draws **once** per outage (guarded by
  `stuckErrorShown`), not on every subsequent failed wake.
- Interactive wakes (`doFetchCycle(true)`) are unaffected by the
  escalation screen — they already call `showError()` on every failure;
  only the streak bookkeeping and radio-reset flag are shared with them.
- No changes to `power.cpp`/`goToSleep()` — sleep-interval math stays
  exactly as it is today.

---

### Task 1: `shouldShowStuckError` pure logic + native tests

**Files:**
- Create: `src/logic/stuck_error.h`
- Test: `test/test_stuck_error/main.cpp`

**Interfaces:**
- Produces: `constexpr uint32_t STUCK_ERROR_SECS` and `inline bool
  shouldShowStuckError(uint32_t fetchFailStreak, uint32_t sleepSecs, bool
  alreadyShown)` — pure C++, no Arduino deps, following this repo's
  `src/logic/*.h` convention (see `src/logic/quiet_hours.h`). Task 3
  (`main.cpp`) consumes this.

- [ ] **Step 1: Write the failing test**

Create `test/test_stuck_error/main.cpp`:

```cpp
#include <unity.h>
#include "logic/stuck_error.h"

void setUp() {}
void tearDown() {}

void test_zero_streak_never_fires() {
    TEST_ASSERT_FALSE(shouldShowStuckError(0, 600, false));
}

void test_below_threshold_does_not_fire() {
    // 35 attempts * 600 s = 21000 s < 21600 (6 h).
    TEST_ASSERT_FALSE(shouldShowStuckError(35, 600, false));
}

void test_at_threshold_fires() {
    // 36 attempts * 600 s = 21600 s == 6 h exactly.
    TEST_ASSERT_TRUE(shouldShowStuckError(36, 600, false));
}

void test_past_threshold_fires() {
    TEST_ASSERT_TRUE(shouldShowStuckError(1000, 600, false));
}

void test_already_shown_suppresses() {
    TEST_ASSERT_FALSE(shouldShowStuckError(1000, 600, true));
}

void test_scales_with_sleep_secs() {
    // A longer configured interval reaches 6 h in fewer attempts.
    TEST_ASSERT_FALSE(shouldShowStuckError(1, 3600, false)); // 1 h < 6 h
    TEST_ASSERT_TRUE(shouldShowStuckError(6, 3600, false));  // 6 h == 6 h
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_zero_streak_never_fires);
    RUN_TEST(test_below_threshold_does_not_fire);
    RUN_TEST(test_at_threshold_fires);
    RUN_TEST(test_past_threshold_fires);
    RUN_TEST(test_already_shown_suppresses);
    RUN_TEST(test_scales_with_sleep_secs);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_stuck_error`
Expected: FAIL to compile — `logic/stuck_error.h` does not exist yet.

- [ ] **Step 3: Write minimal implementation**

Create `src/logic/stuck_error.h`:

```cpp
#pragma once
// Unattended-wake stuck-error escalation math. Pure logic: host-testable,
// no Arduino deps -- same pattern as quiet_hours.h. See
// docs/superpowers/specs/2026-08-19-wifi-reconnect-recovery-design.md.
#include <cstdint>

// 6 h: long enough that no ordinary transient outage (AP reboot, brief
// ISP blip) should trigger it, short enough that a genuinely stuck frame
// is discovered same-day rather than after a multi-day silent gap.
constexpr uint32_t STUCK_ERROR_SECS = 6UL * 3600;

// fetchFailStreak * sleepSecs is only an estimate of elapsed wall-clock
// time (it undercounts if quiet hours delayed wakes in between) -- errs
// toward waiting longer before flagging, which is the safe direction.
inline bool shouldShowStuckError(uint32_t fetchFailStreak, uint32_t sleepSecs,
                                 bool alreadyShown) {
    if (alreadyShown || fetchFailStreak == 0) return false;
    return (uint64_t)fetchFailStreak * sleepSecs >= STUCK_ERROR_SECS;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_stuck_error`
Expected: PASS, all 6 assertions green.

- [ ] **Step 5: Commit**

```bash
git add src/logic/stuck_error.h test/test_stuck_error/main.cpp
git commit -m "Add shouldShowStuckError: pure escalation-threshold logic"
```

---

### Task 2: `connectWifi()` — radio power-cycle before a retry

**Files:**
- Modify: `src/net.h`
- Modify: `src/net.cpp` (`connectWifi()`)

**Interfaces:**
- Changes: `bool connectWifi(bool allowPortal = true, bool
  forceRadioReset = false)` — Task 3 (`main.cpp`) passes `forceRadioReset`.

- [ ] **Step 1: Update the declaration and doc comment in `src/net.h`**

This:

```cpp
// Connect with saved credentials. With allowPortal (the default), first
// boot / stale credentials open the captive portal (AP_NAME) and the
// panel shows instructions. With allowPortal=false — unattended timer
// wakes — never touch the panel or open an AP: fail fast and return
// false so the caller can keep the current photo and retry next wake.
bool connectWifi(bool allowPortal = true);
```

becomes:

```cpp
// Connect with saved credentials. With allowPortal (the default), first
// boot / stale credentials open the captive portal (AP_NAME) and the
// panel shows instructions. With allowPortal=false — unattended timer
// wakes — never touch the panel or open an AP: fail fast and return
// false so the caller can keep the current photo and retry next wake.
// forceRadioReset power-cycles the Wi-Fi radio (WiFi.mode OFF -> STA)
// before attempting to connect — never erases saved credentials, only
// clears any stuck radio/connection-manager state. The caller passes
// true once a wake has already failed at least once this outage (see
// doFetchCycle()'s fetchFailStreak).
bool connectWifi(bool allowPortal = true, bool forceRadioReset = false);
```

- [ ] **Step 2: Implement the reset in `src/net.cpp`**

This (`connectWifi()`'s signature and body up to the `autoConnect()` call):

```cpp
bool connectWifi(bool allowPortal) {
    provisioningScreenShown = false; // each attempt may open a fresh portal
    // Both this firmware's settings portal and WiFiManager's captive
    // portal bind port 80 — free ours in case provisioning must open.
    if (allowPortal) stopPortal();
    WiFiManager wm;
    wm.setAPCallback(configModeCallback);
    wm.setConfigPortalTimeout(300);
    wm.setEnableConfigPortal(allowPortal);
    // No saved credentials means the portal WILL open: draw the instructions
    // now, before autoConnect(), so the portal web server isn't blocked
    // behind the ~30 s panel draw when the user tries to reach it.
    if (allowPortal && !wm.getWiFiIsSaved()) showProvisioningScreenOnce();
    devLog.println("connecting (saved credentials, or captive portal)...");
    bool ok = wm.autoConnect(AP_NAME);
```

becomes:

```cpp
bool connectWifi(bool allowPortal, bool forceRadioReset) {
    provisioningScreenShown = false; // each attempt may open a fresh portal
    // Both this firmware's settings portal and WiFiManager's captive
    // portal bind port 80 — free ours in case provisioning must open.
    if (allowPortal) stopPortal();
    WiFiManager wm;
    wm.setAPCallback(configModeCallback);
    wm.setConfigPortalTimeout(300);
    wm.setEnableConfigPortal(allowPortal);
    // No saved credentials means the portal WILL open: draw the instructions
    // now, before autoConnect(), so the portal web server isn't blocked
    // behind the ~30 s panel draw when the user tries to reach it.
    if (allowPortal && !wm.getWiFiIsSaved()) showProvisioningScreenOnce();
    if (forceRadioReset) {
        // A previous attempt this outage already failed against a radio
        // that was never reset -- power-cycle it before retrying, in case
        // something (e.g. a stale cached BSSID/channel hint) is stuck
        // rather than the saved credentials themselves being wrong.
        // Credentials are untouched -- only the radio driver state.
        devLog.println("wifi: power-cycling radio before retry...");
        WiFi.mode(WIFI_OFF);
        delay(100);
        WiFi.mode(WIFI_STA);
    }
    devLog.println("connecting (saved credentials, or captive portal)...");
    bool ok = wm.autoConnect(AP_NAME);
```

- [ ] **Step 3: Build to verify it compiles**

Run: `pio run -e ee02`
Expected: SUCCESS. (No caller passes `forceRadioReset=true` yet, so this
only checks the new parameter/branch compiles.)

- [ ] **Step 4: Commit**

```bash
git add src/net.h src/net.cpp
git commit -m "connectWifi: add forceRadioReset to power-cycle the radio before a retry"
```

---

### Task 3: Failure-streak tracking + stuck-error escalation in `main.cpp`

**Files:**
- Modify: `src/main.cpp` (`RTC_DATA_ATTR` state, `doFetchCycle()`)
- Modify: `src/display.cpp` (`showError()` doc comment — now also called
  from an unattended path)

**Interfaces:**
- Consumes: `shouldShowStuckError()` (Task 1), `connectWifi(bool, bool)`
  (Task 2).

- [ ] **Step 1: Add the include**

In `src/main.cpp`, add alongside the other local includes:

```cpp
#include "logic/stuck_error.h"
```

- [ ] **Step 2: Add the new `RTC_DATA_ATTR` state**

This:

```cpp
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR int32_t lastVbatMv = -1; // survives deep sleep, not reset/flash
```

becomes:

```cpp
RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR int32_t lastVbatMv = -1; // survives deep sleep, not reset/flash
RTC_DATA_ATTR uint32_t fetchFailStreak = 0; // consecutive full-cycle failures
RTC_DATA_ATTR bool stuckErrorShown = false; // one-time flag per outage
```

- [ ] **Step 3: Add the escalation helper and rewrite `doFetchCycle()`**

This whole function:

```cpp
static void doFetchCycle(bool interactive) {
    setLed(LedMode::Heartbeat);
    if (!connectWifi(interactive)) {
        if (interactive) showError("Wi-Fi connection failed");
        else devLog.println("wifi failed — keeping photo, retry next wake");
        setLed(LedMode::Solid);
        return;
    }
    String err;
    if (!fetchImage(err)) {
        if (interactive) showError(err);
        else devLog.println("fetch failed (" + err + ") — keeping photo");
        setLed(LedMode::Solid);
        return;
    }
    syncClock();
    recordFetchMetadata();
    devLog.println("updating panel (takes ~20-30 s)...");
    epaper.update();
    devLog.println("done");
    setLed(LedMode::Solid);
}
```

becomes:

```cpp
// Unattended-wake escalation: once the streak has failed long enough
// (per settings.sleepSecs) that a transient outage can't explain it,
// draw the error screen once so a stuck frame is discoverable by looking
// at it, instead of only by noticing a stale photo days later. Never
// called for interactive wakes -- those already show an error on every
// single failure.
static void maybeShowStuckError() {
    if (!shouldShowStuckError(fetchFailStreak, settings.sleepSecs,
                              stuckErrorShown))
        return;
    stuckErrorShown = true;
    showError("Wi-Fi hasn't reconnected in over 6h - press KEY1 to check");
}

static void doFetchCycle(bool interactive) {
    setLed(LedMode::Heartbeat);
    // Every retry after the first failure of a new outage forces a fresh
    // radio state first -- see connectWifi()'s forceRadioReset.
    bool forceRadioReset = fetchFailStreak > 0;
    if (!connectWifi(interactive, forceRadioReset)) {
        fetchFailStreak++;
        if (interactive) {
            showError("Wi-Fi connection failed");
        } else {
            devLog.println("wifi failed — keeping photo, retry next wake");
            maybeShowStuckError();
        }
        setLed(LedMode::Solid);
        return;
    }
    String err;
    if (!fetchImage(err)) {
        fetchFailStreak++;
        if (interactive) {
            showError(err);
        } else {
            devLog.println("fetch failed (" + err + ") — keeping photo");
            maybeShowStuckError();
        }
        setLed(LedMode::Solid);
        return;
    }
    fetchFailStreak = 0;
    stuckErrorShown = false;
    syncClock();
    recordFetchMetadata();
    devLog.println("updating panel (takes ~20-30 s)...");
    epaper.update();
    devLog.println("done");
    setLed(LedMode::Solid);
}
```

Note `errorMsg` is a fixed `char[64]` (`logic/status_content.h`) — the
literal message above is 56 characters, comfortably inside the limit with
no attempt-count interpolation (kept out deliberately: an unbounded
streak's digit count could otherwise creep toward the buffer edge for no
real benefit — "over 6h" is already the actionable fact).

- [ ] **Step 4: Update `showError()`'s doc comment in `src/display.cpp`**

This:

```cpp
// Full-panel error screen. Only drawn when someone is watching
// (button-initiated actions) — unattended wakes keep the photo.
void showError(const String &msg) {
```

becomes:

```cpp
// Full-panel error screen. Drawn when someone is watching (button-
// initiated actions), and — once per outage — from an unattended wake
// once failures have persisted long enough to escalate (see main.cpp's
// maybeShowStuckError()). An ordinary unattended failure still keeps the
// photo untouched; this is the deliberate exception.
void showError(const String &msg) {
```

- [ ] **Step 5: Build to verify it compiles**

Run: `pio run -e ee02`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp src/display.cpp
git commit -m "doFetchCycle: track failure streak, escalate to a one-time error screen"
```

---

### Task 4: Full verification pass

**Files:** none (verification only)

- [ ] **Step 1: Run the native test suite**

Run: `pio test -e native`
Expected: PASS, including the new `test_stuck_error` cases from Task 1
alongside all existing suites.

- [ ] **Step 2: Build every firmware env**

```bash
pio run -e ee02
pio run -e ee03
pio run -e ee04
pio run -e ee05
```

Expected: all 4 SUCCESS.

- [ ] **Step 3: Flash to hardware and verify manually**

Flash `ee02` (or whichever env matches the attached board) over USB, then:

- **Radio-reset path:** with dev mode's portal up (`/log`), force a Wi-Fi
  failure on the second attempt of an outage (e.g. temporarily change the
  saved password via the portal, or power off the AP) and confirm `/log`
  shows `"wifi: power-cycling radio before retry..."` on the *second*
  consecutive failure, not the first.
- **Escalation path:** temporarily lower `STUCK_ERROR_SECS` in
  `src/logic/stuck_error.h` (e.g. to `60`) for this bench check only, keep
  Wi-Fi down across several scheduled wakes, and confirm the panel draws
  the error screen exactly once (not again on the next failed wake after
  that), then confirm a subsequent successful fetch (bring Wi-Fi back)
  clears back to the normal photo and resets the streak. Revert
  `STUCK_ERROR_SECS` to `6UL * 3600` afterward — do not commit the
  lowered value.
- Confirm quiet-hours/pinned fast-exit wakes (`main.cpp:164-177`) still
  short-circuit via `quickSleep()` without ever reaching `doFetchCycle()`
  — `fetchFailStreak` must not advance during those.
- Confirm KEY1 (interactive) failures still show an error immediately,
  exactly as before this change, unaffected by the streak/escalation
  logic.

This step needs a human with the physical board and control over the
Wi-Fi AP — the assistant can watch `/log` over HTTP but can't power-cycle
the router or see the physical panel.

- [ ] **Step 4: Commit if Step 3 uncovered fixes**

If manual verification required any code changes, commit them now with a
description of what was fixed. If nothing needed fixing, no commit is
needed for this task.
