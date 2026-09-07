# Auto Firmware Update Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On a scheduled wake, after the photo has refreshed, the frame
fetches a small `key=value` manifest from a fixed rolling
`firmware-latest` GitHub Release, compares a CI-stamped monotonic build
number against its own, and — if the published build is newer,
auto-update is enabled, the battery is ≥ 40 %, and the build is
trusted — streams the new app image into the inactive OTA slot with MD5
verification and reboots into it. An app-level boot-health guard in
`setup()` reverts to the previous slot if the new image can't prove
itself (renders no photo across a small failure/boot budget).

**Architecture:** Three pure headers in the `src/logic/` mould
(`firmware_manifest.h` parser, `firmware_update.h` eligibility gates,
`ota_trial.h` revert verdict), each host-tested via `pio test -e native`.
`tools/version.py` stamps `FW_BUILD_NUMBER` next to `FW_GIT_HASH`.
`src/net.cpp` gains `maybeRunOtaCheck()`, which does the manifest GET and,
on a decision to install, drives the `Update` API directly (static
release assets always send Content-Length, so `Update.writeStream()` +
`Update.setMD5()` is enough — no `HTTPUpdate` wrapper). `src/main.cpp`
owns the `RTC_DATA_ATTR` cadence/trial state (same lifetime contract as
`bootCount`/`fetchFailStreak`), runs the boot-health guard early in
`setup()`, and calls `maybeRunOtaCheck()` at the end of a successful
unattended `doFetchCycle()`. Settings gain `otaEnabled` + `otaCheckSecs`,
surfaced in the portal form and on `/debug`. CI gains a `publish` job.

**Tech Stack:** Arduino/ESP32 (PlatformIO, `framework = arduino`), Unity
for native host tests, GitHub Actions. No new `lib_deps` (`Update` ships
with arduino-esp32). No partition-table change (`default_16MB.csv`
already has `app0`/`app1`/`otadata`).

## Global Constraints

- **Trusted-build gate:** auto-update is skipped entirely when
  `FW_BUILD_NUMBER == 0` (no git provenance) or `FW_GIT_HASH` ends in
  `-dirty` (local working build must not replace itself with CI's clean
  build of a nearby commit).
- **Battery gate:** `OTA_MIN_BATTERY_PCT = 40` — computed from the
  already-read RTC `lastVbatMv`, no fresh ADC read at decision time.
- **Placement:** the check runs only at the end of a *fully successful,
  unattended* `doFetchCycle()` — after `epaper.update()`. Never on
  interactive (button / power-on) wakes; never when the fetch itself
  failed.
- **Non-propagating:** any manifest/download failure logs exactly one
  `devLog` line and returns. The photo is already on the panel; the wake
  is otherwise complete.
- **Trial state is `RTC_DATA_ATTR`** (survives deep sleep, cleared by
  power loss / USB reflash) — same contract as the existing
  `bootCount` / `lastVbatMv` / `fetchFailStreak`.
- **`otaPendingBuild` is set *before* `Update.writeStream()`** so a
  brownout mid-flash still leaves coherent trial bookkeeping.
- **Boot-health guard runs before the quiet-hours / pinned fast-exit** in
  `setup()`, so a boot loop that only ever quiet-exits still counts
  toward the boot budget.
- **Revert is best-effort:** `esp_ota_set_boot_partition()` on the passive
  slot; if it returns non-`ESP_OK` (passive image invalid) the guard logs
  and keeps running the current image rather than restarting.
- **Manifest format:** `key=value`, one per line, `\n` or `\r\n`, leading
  and trailing spaces tolerated, `#` comment lines and unknown keys
  ignored. No JSON, no new dependency.
- CI `publish` job runs only on `push` to `main`, never on PRs.

---

### Task 1: `FW_BUILD_NUMBER` stamping

**Files:**
- Modify: `tools/version.py`
- Modify: `docs/versioning.md`

**Interfaces:**
- Produces: compile-time `FW_BUILD_NUMBER` (bare integer literal, e.g.
  `1284`; `0` outside a git checkout). Consumed by Tasks 4, 6, 7 and the
  `/debug` page (Task 8).

- [ ] **Step 1: Add `build_number()` to `tools/version.py`**

After the existing `git_hash()` function and before the final
`env.Append(...)` line, add:

```python
def build_number():
    try:
        return subprocess.check_output(
            ["git", "rev-list", "--count", "HEAD"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except (subprocess.CalledProcessError, OSError):
        return "0"
```

Then change the tail from:

```python
env.Append(CPPDEFINES=[("FW_GIT_HASH", '\\"%s\\"' % git_hash())])
```

to:

```python
env.Append(CPPDEFINES=[
    ("FW_GIT_HASH", '\\"%s\\"' % git_hash()),
    ("FW_BUILD_NUMBER", build_number()),  # bare int, unquoted — compared numerically on-device
])
```

Also update the module docstring's last sentence to mention the second
stamp (keep it short — "and `FW_BUILD_NUMBER` (commit count) as the
auto-update ordering key; see docs/versioning.md").

- [ ] **Step 2: Document it in `docs/versioning.md`**

Append a section:

```markdown
## `FW_BUILD_NUMBER` — the auto-update ordering key

The git hash identifies *which* commit a build came from but can't answer
"is this newer than what I'm running" — hashes don't order. The on-device
auto-update path (see
`docs/superpowers/specs/2026-09-06-auto-firmware-update-design.md`) needs
that answer, so `tools/version.py` also stamps `FW_BUILD_NUMBER`:
`git rev-list --count HEAD`, the commit count reachable from the current
commit. Monotonic on a linear `main`. Outside a git checkout it falls
back to `0`, which the firmware treats as "never auto-update" — a build
with no provenance can't reason about "newer". Like the hash, it is never
hand-edited; CI and the local build derive it the same way.
```

- [ ] **Step 3: Build to verify the stamp compiles in**

Run: `pio run -e ee02`
Expected: SUCCESS. (Nothing references `FW_BUILD_NUMBER` yet; this only
confirms the define is well-formed — a bare integer, not a string.)

- [ ] **Step 4: Commit**

```bash
git add tools/version.py docs/versioning.md
git commit -m "version.py: stamp FW_BUILD_NUMBER (commit count) for auto-update ordering"
```

---

### Task 2: `firmware_manifest.h` pure parser + native tests

**Files:**
- Create: `src/logic/firmware_manifest.h`
- Test: `test/test_firmware_manifest/main.cpp`

**Interfaces:**
- Produces: `struct ManifestInfo { uint32_t build; char hash[16]; char
  md5[33]; }` and `bool parseManifest(const char *body, const char
  *boardKey, ManifestInfo &out)`. Pure C++, no Arduino deps. Consumed by
  Task 6.

- [ ] **Step 1: Write the failing test**

Create `test/test_firmware_manifest/main.cpp`:

```cpp
#include <unity.h>
#include <cstring>
#include "logic/firmware_manifest.h"

void setUp() {}
void tearDown() {}

static const char *GOOD =
    "# generated by ci.yml\n"
    "build=1284\n"
    "hash=a1b2c3d4\n"
    "md5_ee02=0123456789abcdef0123456789abcdef\n"
    "md5_ee03=ffffffffffffffffffffffffffffffff\n";

void test_valid_parse() {
    ManifestInfo m;
    TEST_ASSERT_TRUE(parseManifest(GOOD, "ee02", m));
    TEST_ASSERT_EQUAL_UINT32(1284, m.build);
    TEST_ASSERT_EQUAL_STRING("a1b2c3d4", m.hash);
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", m.md5);
}

void test_picks_correct_board() {
    ManifestInfo m;
    TEST_ASSERT_TRUE(parseManifest(GOOD, "ee03", m));
    TEST_ASSERT_EQUAL_STRING("ffffffffffffffffffffffffffffffff", m.md5);
}

void test_crlf_and_surrounding_spaces() {
    const char *body =
        "  build = 7 \r\n"
        "\tmd5_ee04 =  ABCDEF0123456789ABCDEF0123456789  \r\n";
    ManifestInfo m;
    TEST_ASSERT_TRUE(parseManifest(body, "ee04", m));
    TEST_ASSERT_EQUAL_UINT32(7, m.build);
    // md5 normalized to lower-case
    TEST_ASSERT_EQUAL_STRING("abcdef0123456789abcdef0123456789", m.md5);
}

void test_missing_build_fails() {
    const char *body = "md5_ee02=0123456789abcdef0123456789abcdef\n";
    ManifestInfo m;
    TEST_ASSERT_FALSE(parseManifest(body, "ee02", m));
}

void test_zero_or_nonnumeric_build_fails() {
    ManifestInfo m;
    TEST_ASSERT_FALSE(parseManifest(
        "build=0\nmd5_ee02=0123456789abcdef0123456789abcdef\n", "ee02", m));
    TEST_ASSERT_FALSE(parseManifest(
        "build=x9\nmd5_ee02=0123456789abcdef0123456789abcdef\n", "ee02", m));
}

void test_board_absent_fails() {
    ManifestInfo m;
    TEST_ASSERT_FALSE(parseManifest(GOOD, "ee05", m));
}

void test_bad_md5_length_or_charset_fails() {
    ManifestInfo m;
    TEST_ASSERT_FALSE(parseManifest("build=1\nmd5_ee02=deadbeef\n", "ee02", m));
    TEST_ASSERT_FALSE(parseManifest(
        "build=1\nmd5_ee02=zzzz456789abcdef0123456789abcdef\n", "ee02", m));
}

void test_prefix_key_not_confused() {
    // "md5_ee02" must not match a line "md5_ee020=..."
    const char *body = "build=1\nmd5_ee020=0123456789abcdef0123456789abcdef\n";
    ManifestInfo m;
    TEST_ASSERT_FALSE(parseManifest(body, "ee02", m));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_parse);
    RUN_TEST(test_picks_correct_board);
    RUN_TEST(test_crlf_and_surrounding_spaces);
    RUN_TEST(test_missing_build_fails);
    RUN_TEST(test_zero_or_nonnumeric_build_fails);
    RUN_TEST(test_board_absent_fails);
    RUN_TEST(test_bad_md5_length_or_charset_fails);
    RUN_TEST(test_prefix_key_not_confused);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_firmware_manifest`
Expected: FAIL to compile — `logic/firmware_manifest.h` does not exist.

- [ ] **Step 3: Write the implementation**

Create `src/logic/firmware_manifest.h`:

```cpp
#pragma once
// Parser for the auto-update manifest (key=value lines). Pure logic:
// host-testable, no Arduino deps -- same pattern as url_template.h. See
// docs/superpowers/specs/2026-09-06-auto-firmware-update-design.md.
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cstdio>

struct ManifestInfo {
    uint32_t build = 0;
    char     hash[16] = {0};
    char     md5[33]  = {0};   // for the requested board key
};

namespace fwmanifest_detail {

// Copy the value for an exact `key` from line-based `body` into
// out[0..outCap). Match is anchored at the first non-space char of a
// line; the key must be followed (after optional spaces) by '='. The
// value runs to the next whitespace / EOL. Returns true on a non-empty
// value.
inline bool findValue(const char *body, const char *key,
                      char *out, size_t outCap) {
    const size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        const char *s = p;
        while (*s == ' ' || *s == '\t') s++;
        if (strncmp(s, key, klen) == 0) {
            const char *q = s + klen;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '=') {
                q++;
                while (*q == ' ' || *q == '\t') q++;
                size_t n = 0;
                while (q[n] && q[n] != '\n' && q[n] != '\r' &&
                       q[n] != ' ' && q[n] != '\t' && n + 1 < outCap) n++;
                memcpy(out, q, n);
                out[n] = '\0';
                return n > 0;
            }
        }
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return false;
}

inline bool isLowerHex32(const char *s) {
    for (int i = 0; i < 32; i++) {
        const char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return s[32] == '\0';
}

} // namespace fwmanifest_detail

// boardKey is the lower-case model, e.g. "ee02".
inline bool parseManifest(const char *body, const char *boardKey,
                          ManifestInfo &out) {
    using namespace fwmanifest_detail;
    char buf[64];

    if (!findValue(body, "build", buf, sizeof(buf))) return false;
    char *end = nullptr;
    const unsigned long b = strtoul(buf, &end, 10);
    if (end == buf || *end != '\0' || b == 0) return false;
    out.build = (uint32_t)b;

    if (findValue(body, "hash", buf, sizeof(buf)))
        snprintf(out.hash, sizeof(out.hash), "%s", buf);

    char key[24];
    snprintf(key, sizeof(key), "md5_%s", boardKey);
    if (!findValue(body, key, buf, sizeof(buf))) return false;
    for (char *c = buf; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (!isLowerHex32(buf)) return false;
    memcpy(out.md5, buf, 33);
    return true;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_firmware_manifest`
Expected: PASS, all 8 tests green.

- [ ] **Step 5: Commit**

```bash
git add src/logic/firmware_manifest.h test/test_firmware_manifest/main.cpp
git commit -m "Add parseManifest: pure key=value auto-update manifest parser"
```

---

### Task 3: `firmware_update.h` eligibility gates + native tests

**Files:**
- Create: `src/logic/firmware_update.h`
- Test: `test/test_firmware_update/main.cpp`

**Interfaces:**
- Produces: `struct OtaGate`, `constexpr int OTA_MIN_BATTERY_PCT`,
  `bool shouldCheckForUpdate(const OtaGate &)`, `bool
  shouldInstallUpdate(const OtaGate &, uint32_t manifestBuild)`. Consumed
  by Task 6.

- [ ] **Step 1: Write the failing test**

Create `test/test_firmware_update/main.cpp`:

```cpp
#include <unity.h>
#include "logic/firmware_update.h"

void setUp() {}
void tearDown() {}

static OtaGate ok() {
    // enabled, trusted build 100, clean, no trial, healthy battery
    return OtaGate{true, 100, false, false, 80};
}

void test_happy_path_checks_and_installs() {
    TEST_ASSERT_TRUE(shouldCheckForUpdate(ok()));
    TEST_ASSERT_TRUE(shouldInstallUpdate(ok(), 101));
}

void test_not_newer_does_not_install() {
    TEST_ASSERT_FALSE(shouldInstallUpdate(ok(), 100));
    TEST_ASSERT_FALSE(shouldInstallUpdate(ok(), 99));
}

void test_disabled() {
    OtaGate g = ok(); g.enabled = false;
    TEST_ASSERT_FALSE(shouldCheckForUpdate(g));
    TEST_ASSERT_FALSE(shouldInstallUpdate(g, 101));
}

void test_untrusted_build_zero() {
    OtaGate g = ok(); g.deviceBuild = 0;
    TEST_ASSERT_FALSE(shouldCheckForUpdate(g));
}

void test_dirty_build() {
    OtaGate g = ok(); g.deviceDirty = true;
    TEST_ASSERT_FALSE(shouldCheckForUpdate(g));
}

void test_trial_pending() {
    OtaGate g = ok(); g.trialPending = true;
    TEST_ASSERT_FALSE(shouldCheckForUpdate(g));
}

void test_low_battery() {
    OtaGate g = ok(); g.batteryPct = OTA_MIN_BATTERY_PCT - 1;
    TEST_ASSERT_FALSE(shouldCheckForUpdate(g));
    g.batteryPct = OTA_MIN_BATTERY_PCT;
    TEST_ASSERT_TRUE(shouldCheckForUpdate(g));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_happy_path_checks_and_installs);
    RUN_TEST(test_not_newer_does_not_install);
    RUN_TEST(test_disabled);
    RUN_TEST(test_untrusted_build_zero);
    RUN_TEST(test_dirty_build);
    RUN_TEST(test_trial_pending);
    RUN_TEST(test_low_battery);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_firmware_update`
Expected: FAIL to compile — header does not exist.

- [ ] **Step 3: Write the implementation**

Create `src/logic/firmware_update.h`:

```cpp
#pragma once
// Auto-update eligibility gates. Pure logic: host-testable, no Arduino
// deps. See docs/superpowers/specs/2026-09-06-auto-firmware-update-design.md.
#include <cstdint>

// Below this the OTA download+flash (~15-30 s of radio + flash writes) is
// too risky against a brownout -- skip and try again a day later.
constexpr int OTA_MIN_BATTERY_PCT = 40;

struct OtaGate {
    bool     enabled;      // settings.otaEnabled
    uint32_t deviceBuild;  // FW_BUILD_NUMBER (0 => no provenance)
    bool     deviceDirty;  // FW_GIT_HASH ends in "-dirty"
    bool     trialPending; // an OTA image is currently on trial
    int      batteryPct;   // batteryPercent(lastVbatMv)
};

// Worth fetching the manifest at all this wake? (Cadence is enforced
// separately by the RTC lastOtaCheckEpoch clock in the caller.)
inline bool shouldCheckForUpdate(const OtaGate &g) {
    return g.enabled && !g.deviceDirty && !g.trialPending
        && g.deviceBuild > 0 && g.batteryPct >= OTA_MIN_BATTERY_PCT;
}

// Given a parsed manifest build number, flash now?
inline bool shouldInstallUpdate(const OtaGate &g, uint32_t manifestBuild) {
    return shouldCheckForUpdate(g) && manifestBuild > g.deviceBuild;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_firmware_update`
Expected: PASS, all 7 tests green.

- [ ] **Step 5: Commit**

```bash
git add src/logic/firmware_update.h test/test_firmware_update/main.cpp
git commit -m "Add shouldCheckForUpdate/shouldInstallUpdate: auto-update gates"
```

---

### Task 4: `ota_trial.h` revert verdict + native tests

**Files:**
- Create: `src/logic/ota_trial.h`
- Test: `test/test_ota_trial/main.cpp`

**Interfaces:**
- Produces: `enum class OtaTrialVerdict`, `constexpr uint8_t
  OTA_TRIAL_MAX_BOOTS / OTA_TRIAL_MAX_FETCH_FAILS`, `struct
  OtaTrialState`, `OtaTrialVerdict otaTrialVerdict(const OtaTrialState
  &)`. Consumed by Task 7.

- [ ] **Step 1: Write the failing test**

Create `test/test_ota_trial/main.cpp`:

```cpp
#include <unity.h>
#include "logic/ota_trial.h"

void setUp() {}
void tearDown() {}

void test_not_on_trial() {
    OtaTrialState s{0, 100, 0, 0, false};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::NotOnTrial, otaTrialVerdict(s));
}

void test_running_mismatch_reverts() {
    // flashed build 101, but we booted 100 -> revert already happened or
    // the swap didn't take; re-assert the good slot.
    OtaTrialState s{101, 100, 1, 0, false};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::Revert, otaTrialVerdict(s));
}

void test_fetch_success_confirms() {
    OtaTrialState s{101, 101, 5, 2, true};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::ConfirmGood, otaTrialVerdict(s));
}

void test_fetch_fail_limit_reverts() {
    OtaTrialState s{101, 101, 1, OTA_TRIAL_MAX_FETCH_FAILS, false};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::Revert, otaTrialVerdict(s));
}

void test_boot_limit_reverts() {
    OtaTrialState s{101, 101, OTA_TRIAL_MAX_BOOTS, 0, false};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::Revert, otaTrialVerdict(s));
}

void test_within_budget_continues() {
    OtaTrialState s{101, 101, 1, 1, false};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::Continue, otaTrialVerdict(s));
}

void test_confirm_beats_limits() {
    OtaTrialState s{101, 101, OTA_TRIAL_MAX_BOOTS,
                    OTA_TRIAL_MAX_FETCH_FAILS, true};
    TEST_ASSERT_EQUAL(OtaTrialVerdict::ConfirmGood, otaTrialVerdict(s));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_not_on_trial);
    RUN_TEST(test_running_mismatch_reverts);
    RUN_TEST(test_fetch_success_confirms);
    RUN_TEST(test_fetch_fail_limit_reverts);
    RUN_TEST(test_boot_limit_reverts);
    RUN_TEST(test_within_budget_continues);
    RUN_TEST(test_confirm_beats_limits);
    return UNITY_END();
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `pio test -e native -f test_ota_trial`
Expected: FAIL to compile — header does not exist.

- [ ] **Step 3: Write the implementation**

Create `src/logic/ota_trial.h`:

```cpp
#pragma once
// App-level OTA boot-health verdict: does a just-flashed image get to
// stay, or do we roll back to the previous slot? Pure logic:
// host-testable, no Arduino deps. See
// docs/superpowers/specs/2026-09-06-auto-firmware-update-design.md.
#include <cstdint>

enum class OtaTrialVerdict : uint8_t {
    NotOnTrial, Continue, ConfirmGood, Revert
};

// Any wake counts toward the boot budget (incl. quiet-hours / pinned
// fast-exits), so a loop that never reaches a fetch still trips it.
constexpr uint8_t OTA_TRIAL_MAX_BOOTS       = 12;
// Consecutive failed doFetchCycle()s while the trial image is running.
constexpr uint8_t OTA_TRIAL_MAX_FETCH_FAILS = 3;

struct OtaTrialState {
    uint32_t pendingBuild;   // build we OTA'd to; 0 => not on trial
    uint32_t runningBuild;   // FW_BUILD_NUMBER now
    uint8_t  boots;          // wakes since the OTA reboot
    uint8_t  fetchFails;     // consecutive failed cycles since
    bool     fetchSucceeded; // a full cycle rendered a photo since
};

inline OtaTrialVerdict otaTrialVerdict(const OtaTrialState &s) {
    if (s.pendingBuild == 0) return OtaTrialVerdict::NotOnTrial;
    if (s.runningBuild != s.pendingBuild) return OtaTrialVerdict::Revert;
    if (s.fetchSucceeded) return OtaTrialVerdict::ConfirmGood;
    if (s.fetchFails >= OTA_TRIAL_MAX_FETCH_FAILS) return OtaTrialVerdict::Revert;
    if (s.boots >= OTA_TRIAL_MAX_BOOTS) return OtaTrialVerdict::Revert;
    return OtaTrialVerdict::Continue;
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `pio test -e native -f test_ota_trial`
Expected: PASS, all 7 tests green.

- [ ] **Step 5: Commit**

```bash
git add src/logic/ota_trial.h test/test_ota_trial/main.cpp
git commit -m "Add otaTrialVerdict: app-level OTA boot-health / rollback decision"
```

---

### Task 5: Settings fields + config.h constants

**Files:**
- Modify: `src/config.h`
- Modify: `src/settings.h`
- Modify: `src/settings.cpp`

**Interfaces:**
- Produces: `settings.otaEnabled` (bool), `settings.otaCheckSecs`
  (uint32_t), NVS keys `otaEn` / `otaSecs`; `DEFAULT_OTA_ENABLED`,
  `DEFAULT_OTA_CHECK_SECS`, `OTA_MANIFEST_URL`, `OTA_RELEASE_BASE_URL` in
  `config.h`. Consumed by Tasks 6, 7, 8.

- [ ] **Step 1: Add constants to `src/config.h`**

In the "Behavior defaults" block, after `DEFAULT_IMAGE_URL`:

```cpp
// ---- Auto firmware update ----------------------------------------------
// Fixed rolling release published by .github/workflows/ci.yml on every
// push to main; assets are clobbered in place so these URLs are stable.
inline const char *OTA_RELEASE_BASE_URL =
    "https://github.com/sven97/hello-epaper/releases/download/firmware-latest/";
inline const char *OTA_MANIFEST_URL =
    "https://github.com/sven97/hello-epaper/releases/download/firmware-latest/manifest.txt";

constexpr bool     DEFAULT_OTA_ENABLED    = true;         // opt-out
constexpr uint32_t DEFAULT_OTA_CHECK_SECS = 24 * 60 * 60; // daily
```

> **Decision confirmed in review:** `DEFAULT_OTA_ENABLED = true`. If the
> conservative default is wanted instead, flip this to `false` — no other
> code changes.

- [ ] **Step 2: Add fields to `struct Settings` in `src/settings.h`**

```cpp
    uint8_t  rotation;       // epaper.setRotation() arg, 0-3
    bool     otaEnabled;     // auto firmware update on scheduled wakes
    uint32_t otaCheckSecs;   // min interval between manifest checks
```

- [ ] **Step 3: Load / save in `src/settings.cpp`**

In `loadSettings()`, after the `rotation` line:

```cpp
    settings.otaEnabled = prefs.getBool("otaEn", DEFAULT_OTA_ENABLED);
    settings.otaCheckSecs = prefs.getUInt("otaSecs", DEFAULT_OTA_CHECK_SECS);
```

In `saveSettings()`, after the `rot` line:

```cpp
    prefs.putBool("otaEn", settings.otaEnabled);
    prefs.putUInt("otaSecs", settings.otaCheckSecs);
```

Update the NVS-keys comment at the top of the file to append
`otaEn otaSecs`.

- [ ] **Step 4: Build**

Run: `pio run -e ee02`
Expected: SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/config.h src/settings.h src/settings.cpp
git commit -m "settings: add otaEnabled + otaCheckSecs (auto firmware update)"
```

---

### Task 6: `maybeRunOtaCheck()` in net.cpp

**Files:**
- Modify: `src/net.h`
- Modify: `src/net.cpp`

**Interfaces:**
- Produces: `void maybeRunOtaCheck(uint32_t &lastOtaCheckEpoch, uint32_t
  &otaPendingBuild, uint8_t &otaTrialBoots, uint8_t &otaTrialFetchFails,
  int batteryPct)`. Consumed by Task 7. May not return (calls
  `esp_restart()` after a successful flash).

- [ ] **Step 1: Declare it in `src/net.h`**

After the `fetchImage()` declaration:

```cpp
// Auto firmware update. Call only at the end of a fully successful,
// unattended fetch cycle (photo already on the panel). Enforces the
// cadence (lastOtaCheckEpoch) and eligibility gates itself; on a decision
// to install, streams the new image into the passive OTA slot with MD5
// verification, sets the trial bookkeeping, and reboots (does not
// return). Any failure logs one line and returns -- never propagates.
// The four counters are main.cpp's RTC_DATA_ATTR state, passed by
// reference so this owns none of it.
void maybeRunOtaCheck(uint32_t &lastOtaCheckEpoch, uint32_t &otaPendingBuild,
                      uint8_t &otaTrialBoots, uint8_t &otaTrialFetchFails,
                      int batteryPct);
```

- [ ] **Step 2: Implement in `src/net.cpp`**

Add includes near the top (with the other `<...>` includes):

```cpp
#include <Update.h>
#include <esp_ota_ops.h>
#include "logic/firmware_manifest.h"
#include "logic/firmware_update.h"
```

Add at the end of the file:

```cpp
// Lower-case board model ("EE02" -> "ee02"), computed once.
static String boardKeyLower() {
    String s(BOARD_MODEL);
    s.toLowerCase();
    return s;
}

// GET url into `body` (capped). Returns HTTP status, or a negative
// HTTPClient error. Follows redirects (release assets 302 to a CDN).
static int httpGetString(const String &url, String &body, size_t cap) {
    WiFiClientSecure client;
    client.setInsecure(); // learning repo: skip cert validation (matches fetchImage)
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(8000);
    if (!http.begin(client, url)) return -1;
    int code = http.GET();
    if (code == HTTP_CODE_OK) {
        body = http.getString();
        if (body.length() > cap) body = "";
    }
    http.end();
    return code;
}

void maybeRunOtaCheck(uint32_t &lastOtaCheckEpoch, uint32_t &otaPendingBuild,
                      uint8_t &otaTrialBoots, uint8_t &otaTrialFetchFails,
                      int batteryPct) {
    // Cadence: clock is sane here (syncClock() just ran in doFetchCycle()).
    time_t now = time(nullptr);
    if (now <= CLOCK_SANE_EPOCH) return;
    if (lastOtaCheckEpoch != 0 &&
        now - (time_t)lastOtaCheckEpoch < (time_t)settings.otaCheckSecs)
        return;

    String hash(FW_GIT_HASH);
    OtaGate gate{
        settings.otaEnabled,
        (uint32_t)FW_BUILD_NUMBER,
        hash.endsWith("-dirty"),
        otaPendingBuild != 0,
        batteryPct,
    };
    if (!shouldCheckForUpdate(gate)) {
        if (settings.otaEnabled && !gate.trialPending)
            devLog.printf("ota: skipped (build=%u dirty=%d batt=%d%%)\n",
                          gate.deviceBuild, (int)gate.deviceDirty, batteryPct);
        return;
    }

    lastOtaCheckEpoch = (uint32_t)now; // a failed fetch still counts

    String body;
    int code = httpGetString(String(OTA_MANIFEST_URL), body, 4096);
    if (code != HTTP_CODE_OK || body.isEmpty()) {
        devLog.printf("ota: manifest fetch failed (%d)\n", code);
        return;
    }

    ManifestInfo mi;
    String bk = boardKeyLower();
    if (!parseManifest(body.c_str(), bk.c_str(), mi)) {
        devLog.println("ota: manifest parse failed");
        return;
    }
    if (!shouldInstallUpdate(gate, mi.build)) {
        devLog.printf("ota: up to date (running %u, latest %u)\n",
                      gate.deviceBuild, mi.build);
        return;
    }

    String binUrl = String(OTA_RELEASE_BASE_URL) + "firmware-" + bk + ".bin";
    devLog.printf("ota: build %u -> %u, downloading %s\n",
                  gate.deviceBuild, mi.build, binUrl.c_str());

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    if (!http.begin(client, binUrl)) {
        devLog.println("ota: bin http.begin failed");
        return;
    }
    int bcode = http.GET();
    int len = http.getSize();
    if (bcode != HTTP_CODE_OK || len <= 0) {
        devLog.printf("ota: bin GET failed (code %d, len %d)\n", bcode, len);
        http.end();
        return;
    }

    // Commit trial bookkeeping BEFORE the flash: a brownout mid-write
    // then still leaves a coherent "build N is on trial" record for the
    // boot-health guard (which treats running!=pending as Revert).
    otaPendingBuild = mi.build;
    otaTrialBoots = 0;
    otaTrialFetchFails = 0;

    if (!Update.begin((size_t)len, U_FLASH)) {
        devLog.printf("ota: Update.begin failed: %s\n", Update.errorString());
        otaPendingBuild = 0;
        http.end();
        return;
    }
    Update.setMD5(mi.md5);
    size_t written = Update.writeStream(http.getStream());
    http.end();
    if (written != (size_t)len || !Update.end(true)) {
        devLog.printf("ota: flash failed (%u/%d bytes): %s\n",
                      (unsigned)written, len, Update.errorString());
        Update.abort();
        otaPendingBuild = 0; // nothing committed -- clear the trial
        return;
    }
    devLog.printf("ota: build %u written, rebooting into it\n", mi.build);
    delay(100);
    esp_restart();
}
```

Notes for the implementer:
- `Update.end(true)` finalizes, verifies the MD5 set above, validates the
  image, and points the boot partition at the new slot. `esp_restart()`
  then boots it.
- No `HTTPUpdate` wrapper: release assets are static files that always
  send `Content-Length`, so `http.getSize()` + `Update.writeStream()` is
  sufficient and keeps the explicit-control style of `PsramSink` above.
- `FW_BUILD_NUMBER` is a bare int from the build flag; the `(uint32_t)`
  cast is defensive.

- [ ] **Step 3: Build**

Run: `pio run -e ee02`
Expected: SUCCESS. (`maybeRunOtaCheck()` is unused until Task 7 — this
confirms it compiles and links against `Update` / `esp_ota_ops`.)

- [ ] **Step 4: Commit**

```bash
git add src/net.h src/net.cpp
git commit -m "net: add maybeRunOtaCheck — manifest check + streamed OTA flash"
```

---

### Task 7: RTC state, boot-health guard, and wiring in main.cpp

**Files:**
- Modify: `src/main.cpp`

**Interfaces:**
- Consumes: `otaTrialVerdict()` (Task 4), `maybeRunOtaCheck()` (Task 6),
  `esp_ota_*` (revert).

- [ ] **Step 1: Add includes**

With the other local includes in `src/main.cpp`:

```cpp
#include "logic/ota_trial.h"
```

and with the framework includes near the top:

```cpp
#include <esp_ota_ops.h>
```

- [ ] **Step 2: Add RTC_DATA_ATTR state**

After the existing `stuckErrorShown` line:

```cpp
RTC_DATA_ATTR uint32_t lastOtaCheckEpoch = 0;  // wall-clock of last manifest check
RTC_DATA_ATTR uint32_t otaPendingBuild = 0;    // build on trial; 0 => none
RTC_DATA_ATTR uint8_t  otaTrialBoots = 0;
RTC_DATA_ATTR uint8_t  otaTrialFetchFails = 0;
```

- [ ] **Step 3: Add the boot-health guard helper**

Above `doFetchCycle()` (near `maybeShowStuckError()`):

```cpp
// App-level OTA rollback. Runs every wake while an image is on trial,
// BEFORE the quiet-hours/pinned fast-exit so a boot loop that only ever
// quiet-exits still trips the boot budget. ConfirmGood is decided later,
// in doFetchCycle(), the moment a photo actually renders.
static void otaBootHealthGuard() {
    if (otaPendingBuild == 0) return;
    OtaTrialState ts{otaPendingBuild, (uint32_t)FW_BUILD_NUMBER,
                     otaTrialBoots, otaTrialFetchFails, /*fetchSucceeded=*/false};
    switch (otaTrialVerdict(ts)) {
    case OtaTrialVerdict::Revert: {
        const esp_partition_t *prev = esp_ota_get_next_update_partition(nullptr);
        devLog.printf("ota: trial for build %u failed (boots=%u fails=%u) — reverting\n",
                      otaPendingBuild, otaTrialBoots, otaTrialFetchFails);
        otaPendingBuild = 0;
        otaTrialBoots = 0;
        otaTrialFetchFails = 0;
        if (prev && esp_ota_set_boot_partition(prev) == ESP_OK) {
            delay(100);
            esp_restart();
        }
        devLog.println("ota: revert failed — staying on current image");
        break;
    }
    case OtaTrialVerdict::Continue:
        otaTrialBoots++;
        devLog.printf("ota: build %u on trial (boot %u/%u)\n",
                      otaPendingBuild, otaTrialBoots, OTA_TRIAL_MAX_BOOTS);
        break;
    default:
        break;
    }
}
```

- [ ] **Step 4: Call the guard early in `setup()`**

In `setup()`, immediately after `applyUtcOffset(prefs.getLong("tzOff", 0));`
(the one before the `esp_sleep_wakeup_cause_t cause = ...` line) and
before the `if (cause == ESP_SLEEP_WAKEUP_TIMER)` fast-path block:

```cpp
    otaBootHealthGuard(); // may esp_restart() (rollback) — never returns then
```

- [ ] **Step 5: Wire confirm-good / fetch-fail / check into `doFetchCycle()`**

In `doFetchCycle()`, in **both** failure branches, right after
`fetchFailStreak++;`:

```cpp
        if (otaPendingBuild != 0 && otaPendingBuild == (uint32_t)FW_BUILD_NUMBER)
            otaTrialFetchFails++;
```

On the success path, replace this tail:

```cpp
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

with:

```cpp
    fetchFailStreak = 0;
    stuckErrorShown = false;
    syncClock();
    recordFetchMetadata();
    devLog.println("updating panel (takes ~20-30 s)...");
    epaper.update();
    devLog.println("done");
    setLed(LedMode::Solid);

    // The just-flashed image rendered a photo — it works. Close the trial.
    if (otaPendingBuild != 0 && otaPendingBuild == (uint32_t)FW_BUILD_NUMBER) {
        devLog.printf("ota: build %u confirmed good\n", otaPendingBuild);
        otaPendingBuild = 0;
        otaTrialBoots = 0;
        otaTrialFetchFails = 0;
    }

    // Unattended wakes only: check for a newer firmware now that a fresh
    // photo is up and the JPEG framebuffer is freed. May not return.
    if (!interactive)
        maybeRunOtaCheck(lastOtaCheckEpoch, otaPendingBuild,
                         otaTrialBoots, otaTrialFetchFails,
                         batteryPercent(lastVbatMv));
}
```

- [ ] **Step 6: Build**

Run: `pio run -e ee02`
Expected: SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add src/main.cpp
git commit -m "main: OTA boot-health guard + confirm-good + post-render update check"
```

---

### Task 8: Portal form + /debug rows

**Files:**
- Modify: `src/portal_html.h`
- Modify: `src/portal.cpp`

**Interfaces:**
- Consumes: `settings.otaEnabled` / `settings.otaCheckSecs` (Task 5),
  `FW_BUILD_NUMBER`, `lastOtaCheckEpoch` / `otaPendingBuild` (Task 7 —
  see Step 4 note on linkage).

- [ ] **Step 1: Add the form block to `src/portal_html.h`**

After the Orientation `<select name="rot">` row and before `</form>`:

```html
<label><input type="checkbox" name="ota_en" %OTA_EN%> Auto-update firmware</label>
<label>Check for updates</label><select name="ota_secs">%OTA_OPTS%</select>
```

- [ ] **Step 2: Fill the tokens where the form is rendered in `src/portal.cpp`**

Wherever `%ROT_OPTS%` / `%PAUSED%` are replaced for the settings page, add
alongside:

```cpp
page.replace("%OTA_EN%", settings.otaEnabled ? "checked" : "");
page.replace("%OTA_OPTS%", otaIntervalOptions(settings.otaCheckSecs));
```

Add a small helper near the other `*Options()` builders:

```cpp
static String otaIntervalOptions(uint32_t cur) {
    struct { uint32_t secs; const char *label; } opts[] = {
        {24 * 60 * 60, "Daily"},
        {12 * 60 * 60, "Every 12 hours"},
        {7 * 24 * 60 * 60, "Weekly"},
    };
    String s;
    for (auto &o : opts) {
        s += "<option value=\"" + String(o.secs) + "\"";
        if (o.secs == cur) s += " selected";
        s += ">" + String(o.label) + "</option>";
    }
    return s;
}
```

- [ ] **Step 3: Handle the fields in `handleSave()` in `src/portal.cpp`**

After the `int rot = server.arg("rot").toInt();` line and its clamp:

```cpp
    bool otaEn = server.hasArg("ota_en");
    uint32_t otaSecs = (uint32_t)server.arg("ota_secs").toInt();
    // whitelist-clamp to the three offered values; anything else -> daily
    if (otaSecs != 12 * 60 * 60 && otaSecs != 7 * 24 * 60 * 60)
        otaSecs = 24 * 60 * 60;
```

and where the other fields are copied into `settings` before
`saveSettings()`:

```cpp
    settings.otaEnabled = otaEn;
    settings.otaCheckSecs = otaSecs;
```

- [ ] **Step 4: Add rows to the `/debug` page (`handleDebug()` in `src/portal.cpp`)**

The `/debug` HTML lives inline in `portal_html.h` / is assembled in
`handleDebug()`. Add, next to the existing `%HASH%` replacement:

```cpp
    page.replace("%BUILD%", String((uint32_t)FW_BUILD_NUMBER));
    page.replace("%OTA_STATE%",
        String(settings.otaEnabled ? "on" : "off") + ", every " +
        String(settings.otaCheckSecs / 3600) + "h");
    page.replace("%OTA_LAST%",
        lastOtaCheckEpoch ? fmtLocal(lastOtaCheckEpoch) : String("never"));
    page.replace("%OTA_TRIAL%",
        otaPendingBuild ? ("build " + String(otaPendingBuild) +
                           " boot " + String(otaTrialBoots))
                        : String("—"));
```

and the matching `Build #%BUILD%` / `Auto-update %OTA_STATE%` /
`Last check %OTA_LAST%` / `Trial %OTA_TRIAL%` lines in the `/debug`
markup. `lastOtaCheckEpoch` / `otaPendingBuild` / `otaTrialBoots` are
`RTC_DATA_ATTR` in `main.cpp` — add `extern` declarations for them near
the top of `portal.cpp` (the file already externs `prefs` / `held`
patterns; match that), or expose a small `otaDebugSummary()` from
`main.cpp` if the implementer prefers not to widen the extern surface.
Reuse the existing local-time formatter used for the "next fetch" line
(`fmtLocal` above is a placeholder for whatever that helper is actually
called).

- [ ] **Step 5: Build**

Run: `pio run -e ee02`
Expected: SUCCESS.

- [ ] **Step 6: Commit**

```bash
git add src/portal_html.h src/portal.cpp
git commit -m "portal: auto-update toggle + interval in settings; build/OTA rows on /debug"
```

---

### Task 9: CI — publish the rolling `firmware-latest` release

**Files:**
- Modify: `.github/workflows/ci.yml`

**Interfaces:**
- Produces: on every push to `main`, a `firmware-latest` GitHub Release
  carrying `firmware-ee0{2,3,4,5}.bin` + `manifest.txt`, consumed by the
  device's `maybeRunOtaCheck()`.

- [ ] **Step 1: Upload the built binary from the `build` matrix job**

In the `build` job, after the `Firmware build` step:

```yaml
      - name: Upload firmware artifact
        uses: actions/upload-artifact@v4
        with:
          name: fw-${{ matrix.env }}
          path: .pio/build/${{ matrix.env }}/firmware.bin
          if-no-files-found: error
```

- [ ] **Step 2: Add the `publish` job**

Append to `.github/workflows/ci.yml`:

```yaml
  publish:
    needs: [test, build]
    if: github.ref == 'refs/heads/main' && github.event_name == 'push'
    runs-on: ubuntu-latest
    permissions:
      contents: write
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0   # git rev-list --count needs full history
      - uses: actions/download-artifact@v4
        with:
          path: fw         # -> fw/fw-ee02/firmware.bin, ...
      - name: Assemble assets + manifest
        run: |
          set -euo pipefail
          BUILD=$(git rev-list --count HEAD)
          HASH=$(git rev-parse --short=8 HEAD)
          mkdir dist
          {
            echo "# generated by .github/workflows/ci.yml — do not edit"
            echo "build=$BUILD"
            echo "hash=$HASH"
            for e in ee02 ee03 ee04 ee05; do
              cp "fw/fw-$e/firmware.bin" "dist/firmware-$e.bin"
              echo "md5_$e=$(md5sum "dist/firmware-$e.bin" | cut -d' ' -f1)"
            done
          } > dist/manifest.txt
          cat dist/manifest.txt
      - name: Move firmware-latest release to this commit
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          set -euo pipefail
          BUILD=$(git rev-list --count HEAD)
          HASH=$(git rev-parse --short=8 HEAD)
          gh release delete firmware-latest --yes --cleanup-tag || true
          gh release create firmware-latest dist/* \
            --target "$GITHUB_SHA" \
            --title "Rolling firmware (build $BUILD)" \
            --notes "Auto-published from $HASH. Consumed by the on-device auto-update path (docs/superpowers/specs/2026-09-06-auto-firmware-update-design.md)." \
            --prerelease
```

- [ ] **Step 3: Validate the workflow YAML**

Run: `python -c "import yaml,sys; yaml.safe_load(open('.github/workflows/ci.yml'))"`
Expected: no output (valid YAML). If `actionlint` is available, run it too.

- [ ] **Step 4: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: publish rolling firmware-latest release (per-board bins + manifest)"
```

- [ ] **Step 5: Push the branch and confirm CI is green**

Push the feature branch and open the PR. Confirm `test` + `build` pass.
`publish` will not run on the PR (guarded to `push` on `main`) — it first
executes only when this work merges to `main`. After merge, confirm the
`publish` job created the `firmware-latest` release with 5 assets and that
`manifest.txt` has a plausible `build=` (matches `git rev-list --count
HEAD` on `main`).

---

### Task 10: Full verification pass

**Files:** none (verification only)

- [ ] **Step 1: Native test suite**

Run: `pio test -e native`
Expected: PASS — the three new suites (`test_firmware_manifest`,
`test_firmware_update`, `test_ota_trial`) alongside all pre-existing ones.

- [ ] **Step 2: Build every firmware env**

```bash
pio run -e ee02
pio run -e ee03
pio run -e ee04
pio run -e ee05
```

Expected: all 4 SUCCESS.

- [ ] **Step 3: Hardware — happy path**

Merge to `main` so CI publishes at least one `firmware-latest` (build N).
Flash the attached board (`ee02`) at a commit *before* N so
`FW_BUILD_NUMBER < N`. Then over `/log` / `/debug`:

- `/debug` shows `Build #<n-1>`, `Auto-update on, every 24h`,
  `Last check never`.
- Force a scheduled wake (or lower `otaCheckSecs` to "Every 12 hours" and
  wait / use the dev-mode fetch-due path). `/log` shows
  `ota: build <n-1> -> N, downloading …`, then the board reboots.
- After reboot `/debug` shows `Build #N`. On the next successful fetch,
  `/log` shows `ota: build N confirmed good` and `/debug`'s trial row is
  back to `—`.

- [ ] **Step 4: Hardware — rollback**

Publish a deliberately broken build N+1 — e.g. a commit that `abort()`s a
few seconds into `loop()`/after setup, or hard-fails every
`connectWifi()`. Confirm the board flashes N+1, then within the budget
(`OTA_TRIAL_MAX_FETCH_FAILS = 3` failed cycles, or
`OTA_TRIAL_MAX_BOOTS = 12` wakes) `/log` after recovery shows
`ota: trial for build <N+1> failed … — reverting` and `/debug` shows
`Build #N` again. Revert the broken commit on `main` afterwards.

- [ ] **Step 5: Hardware — gates**

- Toggle `Auto-update firmware` **off** in the portal; publish N+2;
  confirm no `ota:` lines appear on subsequent scheduled wakes.
- With auto-update on, drain/simulate battery `< 40%` and confirm `/log`
  shows `ota: skipped (… batt=NN%)` and no download.
- Build locally with an uncommitted change (`FW_GIT_HASH` `-dirty`), flash
  over USB, confirm `ota: skipped (… dirty=1 …)` even with a newer build
  published.
- Confirm an interactive wake (KEY2 / power-on) never triggers a check,
  even when one is due.

This step needs a human with the board, control over what CI publishes,
and the ability to observe the physical panel and battery. The assistant
can read `/log` and `/debug` over HTTP but cannot flash, power-cycle, or
judge the panel.

- [ ] **Step 6: Commit any fixes from Steps 3–5**

If hardware verification required code changes, commit them now with a
description of what broke and the fix. If nothing needed fixing, no
commit for this task.

- [ ] **Step 7: Update the spec's status**

Add a one-line note at the top of
`docs/superpowers/specs/2026-09-06-auto-firmware-update-design.md`
recording that it shipped, the merge commit, and any deviations made
during implementation (e.g. the direct-`Update` approach instead of
`HTTPUpdate`, already reflected in this plan).
