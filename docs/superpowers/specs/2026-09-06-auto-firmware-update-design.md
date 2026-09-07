# Auto firmware update: pull-on-wake OTA from a rolling GitHub Release

## Context

The frame is a deep-sleep device: it wakes on a timer (or a button),
brings Wi-Fi up, fetches a photo, refreshes the panel, sleeps. Updating
its firmware today means physically retrieving the board, plugging USB,
and `pio run -t upload`. For a wall-mounted 13.3" panel that's a real
chore, and it means bug fixes (e.g. the recent Wi-Fi reconnect recovery
work) never reach a board that's already deployed and behaving.

This spec adds an opt-out **auto-update path**: on a scheduled wake, after
the photo has refreshed, the device fetches a small manifest from a fixed
URL, compares a monotonic build number against its own, and — if the
published build is newer, auto-update is enabled, and the battery is
healthy — downloads the new app image into the inactive OTA slot, verifies
its MD5, and reboots into it. An app-level boot-health guard reverts to
the previous slot if the new image fails to prove itself.

Nothing about the hardware or partition layout has to change: the board
is a XIAO ESP32-S3 Plus (16 MB flash) and `platformio.ini` already builds
against `default_16MB.csv`, whose table already has dual OTA app slots
(`app0`/`app1`, 6.4 MB each) plus `otadata`. The running firmware is
~1.2 MB, so an image comfortably fits the inactive slot with the running
one untouched during download.

### Decisions locked before this spec (from the brainstorm)

| Fork | Choice | Consequence |
|------|--------|-------------|
| Where CI publishes | **Rolling `firmware-latest` GitHub Release** — assets clobbered every push to `main` | One stable tag, no growing tag list; asset URLs are permanent |
| "Newer" test | **CI-generated monotonic build number** (`git rev-list --count HEAD`), stamped as `FW_BUILD_NUMBER` alongside `FW_GIT_HASH` | Forward-only updates; `docs/versioning.md` gains one CI-generated (never hand-bumped) number |
| Rollback safety | **App-level boot-health guard** — RTC trial state, revert via `esp_ota_set_boot_partition()` | No custom bootloader; a hard brick *before* the guard runs in `setup()` still needs USB recovery |
| Image trust | **HTTPS + manifest MD5**, no signing | Trusts the TLS connection to GitHub (same `setInsecure()` posture the photo fetch already uses); zero key management |

## Distribution: the `firmware-latest` release

CI gains a `publish` job (`needs: [test, build]`, gated
`if: github.ref == 'refs/heads/main' && github.event_name == 'push'`). The
existing `build` matrix job uploads each env's `firmware.bin` as a
workflow artifact renamed `firmware-<env>.bin`; `publish` downloads all
four, computes the shared metadata, writes the manifest, and moves the
`firmware-latest` tag onto the current commit with all five files as
release assets (clobbering the previous ones).

```
firmware-latest  (release)
├── firmware-ee02.bin
├── firmware-ee03.bin
├── firmware-ee04.bin
├── firmware-ee05.bin
└── manifest.txt
```

Asset download URLs are stable and derivable from the board model, so the
manifest doesn't carry them:

```
https://github.com/sven97/hello-epaper/releases/download/firmware-latest/firmware-ee02.bin
```

### Manifest format

Plain `key=value` lines, not JSON — matches the house style (no JSON
anywhere in the firmware today; `renderUrlTemplate` and friends are
hand-rolled) and gives a clean pure-logic parse target with no new
`lib_deps`. `\n` or `\r\n`, leading/trailing spaces tolerated, unknown
keys ignored, `#` comment lines ignored.

```
build=1284
hash=a1b2c3d4
md5_ee02=6f1e...c9
md5_ee03=0ab4...12
md5_ee04=77de...aa
md5_ee05=91c0...5f
```

- `build` — `git rev-list --count HEAD` at publish time. Monotonic on a
  linear `main`. The only field the newer-than comparison uses.
- `hash` — `git rev-parse --short=8 HEAD`, for humans reading `/debug`
  and for cross-checking against `FW_GIT_HASH`.
- `md5_<env>` — MD5 of that board's `.bin`, passed to `Update` so a
  truncated or corrupted download is rejected before the slot swap.

## Version stamping: `FW_BUILD_NUMBER`

`tools/version.py` gains a second define alongside `FW_GIT_HASH`:

```python
def build_number():
    try:
        return subprocess.check_output(
            ["git", "rev-list", "--count", "HEAD"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except (subprocess.CalledProcessError, OSError):
        return "0"

env.Append(CPPDEFINES=[("FW_BUILD_NUMBER", build_number())])  # bare int, unquoted
```

- Outside a git checkout it falls back to `0` (mirrors `FW_GIT_HASH`'s
  `"unknown"`).
- `FW_BUILD_NUMBER == 0` **disables auto-update** on that device — a build
  with no provenance can't reason about "newer".
- A `-dirty` `FW_GIT_HASH` **also disables auto-update** — a local working
  build must not replace itself with CI's clean build of a nearby commit
  on the next wake.

`docs/versioning.md` gets a short paragraph: the git hash is still the
human-facing identity; `FW_BUILD_NUMBER` is a CI-derived ordering key that
exists solely so a device can tell whether a published image is ahead of
it. Still nothing to hand-bump.

## Pure logic (host-tested, `src/logic/`)

Two headers in the `quiet_hours.h` / `stuck_error.h` mould — no Arduino
deps, `pio test -e native`.

### `src/logic/firmware_manifest.h`

```cpp
struct ManifestInfo {
    uint32_t build = 0;
    char     hash[16] = {0};
    char     md5[33]  = {0};   // for the board key requested
};

// Parse `body` (NUL-terminated manifest text) for `boardKey` (e.g. "ee02",
// lower-case). Returns true only if `build` and `md5_<boardKey>` were both
// found and well-formed (build parses to >0, md5 is 32 lower-hex chars).
bool parseManifest(const char *body, const char *boardKey, ManifestInfo &out);
```

### `src/logic/firmware_update.h`

```cpp
// Everything the caller already knows by the time it would start a check.
struct OtaGate {
    bool     enabled;          // settings.otaEnabled
    uint32_t deviceBuild;      // FW_BUILD_NUMBER (0 => untrusted)
    bool     deviceDirty;      // FW_GIT_HASH ends in "-dirty"
    bool     trialPending;     // an OTA image is currently on trial
    int      batteryPct;       // batteryPercent(lastVbatMv)
};

constexpr int OTA_MIN_BATTERY_PCT = 40;

// Should the manifest even be fetched this wake? (Cadence is enforced
// separately by the RTC lastOtaCheckEpoch clock in the caller.)
inline bool shouldCheckForUpdate(const OtaGate &g) {
    return g.enabled && !g.deviceDirty && !g.trialPending
        && g.deviceBuild > 0 && g.batteryPct >= OTA_MIN_BATTERY_PCT;
}

// Given a parsed manifest, is this the moment to flash?
inline bool shouldInstallUpdate(const OtaGate &g, uint32_t manifestBuild) {
    return shouldCheckForUpdate(g) && manifestBuild > g.deviceBuild;
}
```

### `src/logic/ota_trial.h`

```cpp
enum class OtaTrialVerdict : uint8_t { NotOnTrial, Continue, ConfirmGood, Revert };

constexpr uint8_t OTA_TRIAL_MAX_BOOTS       = 12; // any wake, incl. quiet/pinned
constexpr uint8_t OTA_TRIAL_MAX_FETCH_FAILS = 3;  // consecutive failed cycles

struct OtaTrialState {
    uint32_t pendingBuild;   // build we OTA'd to; 0 => not on trial
    uint32_t runningBuild;   // FW_BUILD_NUMBER now
    uint8_t  boots;          // wakes since the OTA reboot
    uint8_t  fetchFails;     // consecutive doFetchCycle() failures since
    bool     fetchSucceeded; // a full cycle rendered a photo since
};

inline OtaTrialVerdict otaTrialVerdict(const OtaTrialState &s) {
    if (s.pendingBuild == 0) return OtaTrialVerdict::NotOnTrial;
    // Reboot landed on something other than the image we flashed — either
    // the revert already happened, or the swap didn't take. Stop tracking.
    if (s.runningBuild != s.pendingBuild) return OtaTrialVerdict::Revert;
    if (s.fetchSucceeded) return OtaTrialVerdict::ConfirmGood;
    if (s.fetchFails >= OTA_TRIAL_MAX_FETCH_FAILS) return OtaTrialVerdict::Revert;
    if (s.boots >= OTA_TRIAL_MAX_BOOTS) return OtaTrialVerdict::Revert;
    return OtaTrialVerdict::Continue;
}
```

`runningBuild != pendingBuild` maps to `Revert` so the caller has one
"undo the trial bookkeeping and make sure we're pointed at a known-good
slot" branch; when the revert already happened it's a cheap no-op
re-assert of the boot partition.

## RTC-persisted state (`src/main.cpp`)

Alongside the existing `bootCount` / `lastVbatMv` / `fetchFailStreak`:

```cpp
RTC_DATA_ATTR uint32_t lastOtaCheckEpoch = 0; // wall-clock of last manifest fetch
RTC_DATA_ATTR uint32_t otaPendingBuild   = 0; // build on trial; 0 => none
RTC_DATA_ATTR uint8_t  otaTrialBoots     = 0;
RTC_DATA_ATTR uint8_t  otaTrialFetchFails = 0;
```

Same "survives deep sleep, cleared by power loss / reflash" contract as
the neighbours — appropriate here: a power cycle also resets the Wi-Fi
radio and, importantly, a USB reflash is exactly the manual-recovery path
the trial guard is a backstop for, so wiping trial state then is correct.

## Boot-health guard (`src/main.cpp::setup()`)

Runs right after `loadSettings()`, **before** the quiet-hours / pinned
fast-exit paths, so even a wake that will `quickSleep()` still counts
toward `otaTrialBoots` (a boot loop that only ever quiet-exits still gets
caught):

```cpp
if (otaPendingBuild != 0) {
    OtaTrialState ts{ otaPendingBuild, FW_BUILD_NUMBER,
                      otaTrialBoots, otaTrialFetchFails, /*fetchSucceeded=*/false };
    switch (otaTrialVerdict(ts)) {
    case OtaTrialVerdict::Revert: {
        const esp_partition_t *prev = esp_ota_get_next_update_partition(nullptr);
        devLog.printf("ota: trial for build %u failed (boots=%u fails=%u) — reverting\n",
                      otaPendingBuild, otaTrialBoots, otaTrialFetchFails);
        otaPendingBuild = otaTrialBoots = otaTrialFetchFails = 0;
        if (prev && esp_ota_set_boot_partition(prev) == ESP_OK) esp_restart();
        break; // set_boot_partition failed: fall through and keep running
    }
    case OtaTrialVerdict::Continue:
        otaTrialBoots++;
        devLog.printf("ota: build %u on trial (boot %u/%u)\n",
                      otaPendingBuild, otaTrialBoots, OTA_TRIAL_MAX_BOOTS);
        break;
    default: // NotOnTrial can't happen here; ConfirmGood is decided in doFetchCycle()
        break;
    }
}
```

`ConfirmGood` is intentionally *not* actioned here — the guard only ever
sees `fetchSucceeded=false` because no fetch has run yet this wake. The
clear happens in `doFetchCycle()` (below) the moment a photo actually
renders, which is the real "this image works" signal.

### What the guard does and doesn't catch

- **Catches:** new image boots but can't fetch/render (3 failed cycles),
  or boots but wedges somewhere that still lets `setup()` run each wake
  (12 boots). Revert is automatic, within ~a day at the default cadence.
- **Doesn't catch:** an image that faults *before* this guard code runs
  (bootloader-level, or a crash in early `setup()` / a constructor). The
  brainstorm accepted this as the cost of not shipping a custom
  anti-rollback bootloader. Recovery there is USB reflash, which also
  clears the RTC trial state.

## Update check (`src/net.cpp`, new `maybeRunOtaCheck()`)

Called from `doFetchCycle()` **at the end of a fully successful cycle** —
after `epaper.update()` — and **only on unattended wakes**
(`!interactive`):

```cpp
fetchFailStreak = 0;
stuckErrorShown = false;
syncClock();
recordFetchMetadata();
epaper.update();
setLed(LedMode::Solid);
if (!interactive) maybeRunOtaCheck();   // may not return (reboots on a successful flash)
```

Rationale for placement:

- **After the render**, not before: the panel always shows a current
  photo first; the flash+reboot then happens with a fresh picture already
  up, and the *next* wake resumes normally. A wake that flashes simply
  doesn't also fetch — acceptable at daily cadence.
- **Only after success**: if the fetch cycle itself failed, the network is
  suspect and the manifest/image download would likely fail too; the
  existing stuck-error escalation already tells the user to intervene.
  Trade-off: a board wedged on a build that *can't* fetch also can't
  self-heal via OTA — that's the USB-recovery case.
- **Post-render heap**: the JPEG framebuffer (`ps_malloc` of
  `w*h*2` bytes) is freed by the time we get here, so the TLS + `Update`
  buffers don't stack on top of peak decode memory.

`maybeRunOtaCheck()`:

1. **Cadence gate** — `now > CLOCK_SANE_EPOCH` and
   `now - lastOtaCheckEpoch >= settings.otaCheckSecs`. Otherwise return.
   (Clock is sane here: `syncClock()` just ran.)
2. **Eligibility gate** — build `OtaGate` from `settings.otaEnabled`,
   `FW_BUILD_NUMBER`, `FW_GIT_HASH` dirtiness, `otaPendingBuild != 0`,
   `batteryPercent(lastVbatMv)`; `if (!shouldCheckForUpdate(g)) return;`
   Log the reason when it's a config/battery gate (not just silence).
3. Set `lastOtaCheckEpoch = now` (a failed fetch still counts — don't
   retry-storm a flaky manifest host every wake).
4. **Fetch manifest** — `WiFiClientSecure` + `setInsecure()`,
   `HTTPClient` with `HTTPC_FORCE_FOLLOW_REDIRECTS`, ~8 s timeout, into a
   small `String` (manifest is well under 1 KB; cap the read at 4 KB and
   bail if larger). Any error → `devLog` one line, return. **Never
   propagates** — the photo is already on the panel and the wake is
   otherwise done.
5. `parseManifest(body, BOARD_MODEL_LOWER, mi)` — fail → log, return.
6. `if (!shouldInstallUpdate(g, mi.build)) { devLog "ota: up to date (build N)"; return; }`
7. **Install** —
   ```cpp
   devLog.printf("ota: build %u -> %u, downloading %s\n",
                 FW_BUILD_NUMBER, mi.build, binUrl.c_str());
   otaPendingBuild = mi.build;   // set BEFORE the flash so a mid-flash
   otaTrialBoots = 0;            // brownout still leaves a coherent trial
   otaTrialFetchFails = 0;       // record (guard treats running!=pending
                                 // as Revert, i.e. re-assert good slot)
   httpUpdate.rebootOnUpdate(true);
   httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
   // MD5 from manifest: reject a corrupt/truncated image before swap
   httpUpdate.setMD5sum(mi.md5);  // (exact API name TBD in plan — Update.setMD5)
   t_httpUpdate_return r = httpUpdate.update(secureClient, binUrl);
   // Only reached on failure (success reboots):
   devLog.printf("ota: update failed (%d): %s\n", r,
                 httpUpdate.getLastErrorString().c_str());
   otaPendingBuild = 0;          // no image was committed — clear the trial
   ```
   `binUrl` is the fixed release-download URL for `BOARD_MODEL` lower-cased.

`BOARD_MODEL` is `"EE02"`; the manifest keys and asset names use `ee02`.
Add `inline const char *BOARD_MODEL_LOWER = ...` to `config.h` (or lower
it at the call site) so there's one source of truth.

## `doFetchCycle()` — confirm-good on render

At the same success point (before `maybeRunOtaCheck()`):

```cpp
if (otaPendingBuild != 0 && otaPendingBuild == FW_BUILD_NUMBER) {
    devLog.printf("ota: build %u confirmed good\n", otaPendingBuild);
    otaPendingBuild = otaTrialBoots = otaTrialFetchFails = 0;
}
```

And on either failure branch, alongside `fetchFailStreak++`:

```cpp
if (otaPendingBuild != 0 && otaPendingBuild == FW_BUILD_NUMBER)
    otaTrialFetchFails++;
```

(Only count failures of the image actually on trial. A successful cycle
resets `otaTrialFetchFails` implicitly by clearing the whole trial.)

## Settings

Two new fields in `struct Settings` (`settings.h`), NVS keys `otaEn` /
`otaSecs`, defaults in `config.h`:

```cpp
bool     otaEnabled;    // DEFAULT_OTA_ENABLED   = true
uint32_t otaCheckSecs;  // DEFAULT_OTA_CHECK_SECS = 24*60*60
```

`loadSettings()` / `saveSettings()` extend with the same pattern as the
neighbours (`getBool` / `getUInt`).

**Default `otaEnabled = true`** — the feature's whole point is hands-off,
and a deployed board is precisely the one that benefits. Flagged here as
the one decision worth an explicit veto in review: the conservative
alternative is default-`false` (opt in from the portal once).

### Portal form (`portal_html.h` + `portal.cpp::handleSave`)

New block after Orientation:

```html
<label><input type="checkbox" name="ota_en" %OTA_EN%> Auto-update firmware</label>
<label>Check for updates</label><select name="ota_secs">%OTA_OPTS%</select>
```

`%OTA_OPTS%` — `Daily` (86400) / `Every 12 hours` (43200) / `Weekly`
(604800), selected by `settings.otaCheckSecs`. `handleSave` reads
`server.arg("ota_en")` (checkbox → bool) and `server.arg("ota_secs").toInt()`
with a whitelist-clamp to those three values.

### `/debug` page (`portal.cpp::handleDebug`)

Add rows: `FW_BUILD_NUMBER`; `auto-update on/off` + interval; last check
(`lastOtaCheckEpoch` rendered local, or `never`); trial status
(`otaPendingBuild` + `otaTrialBoots` when nonzero). Purely
`%TOKEN%` additions to the existing template.

## Files touched

- `.github/workflows/ci.yml` — `build` job uploads renamed artifacts;
  new `publish` job (checkout `fetch-depth: 0`, download artifacts,
  compute `build`/`hash`/md5s, write `manifest.txt`, move `firmware-latest`
  tag + clobber assets via `softprops/action-gh-release` or `gh release`).
- `tools/version.py` — add `FW_BUILD_NUMBER`.
- `docs/versioning.md` — paragraph on `FW_BUILD_NUMBER`.
- `src/logic/firmware_manifest.h`, `src/logic/firmware_update.h`,
  `src/logic/ota_trial.h` — new pure headers.
- `test/test_firmware_manifest/`, `test/test_firmware_update/`,
  `test/test_ota_trial/` — new native tests.
- `src/main.cpp` — RTC trial state; boot-health guard in `setup()`;
  confirm-good / fetch-fail bookkeeping and `maybeRunOtaCheck()` call in
  `doFetchCycle()`.
- `src/net.h` / `src/net.cpp` — `maybeRunOtaCheck()`; `<HTTPUpdate.h>`,
  `<esp_ota_ops.h>` includes. No new `lib_deps` (`HTTPUpdate` ships with
  arduino-esp32).
- `src/config.h` — `DEFAULT_OTA_ENABLED`, `DEFAULT_OTA_CHECK_SECS`,
  `BOARD_MODEL_LOWER`, the fixed release URL base.
- `src/settings.h` / `src/settings.cpp` — two fields, load/save.
- `src/portal_html.h` / `src/portal.cpp` — form block, save handler,
  `/debug` rows.

## Testing

### Native (`pio test -e native`)

- `test_firmware_manifest` — valid parse; `\r\n`; leading/trailing
  spaces; `#` comments; unknown keys ignored; missing `build`; missing
  `md5_<board>`; `build=0` / non-numeric; md5 wrong length / non-hex;
  board key absent from an otherwise-valid manifest.
- `test_firmware_update` — `shouldCheckForUpdate` / `shouldInstallUpdate`
  truth table: disabled; `deviceBuild==0`; `deviceDirty`; `trialPending`;
  battery `<40` / `>=40`; manifest build `<` / `==` / `>` device.
- `test_ota_trial` — `otaTrialVerdict`: not on trial; `runningBuild !=
  pendingBuild` → Revert; `fetchSucceeded` → ConfirmGood; fetch-fail
  limit → Revert; boot limit → Revert; below both limits → Continue;
  ConfirmGood wins over the fail/boot limits.

### Build

`pio run -e ee02` (and the CI matrix for ee03/04/05).

### Hardware (this project's bar for "done")

1. Flash build N over USB. `/debug` shows build N, `auto-update on`,
   `last check never`.
2. Merge a trivial commit to `main`; CI publishes build N+1. On the next
   wake past the cadence gate, `/log` shows `ota: build N -> N+1`,
   the board reboots, `/debug` shows N+1, and after the following
   successful fetch `/log` shows `build N+1 confirmed good`.
3. Publish a deliberately broken N+2 (e.g. `abort()` early in `loop()` /
   after a few boots, or a bad Wi-Fi path). Confirm the board flashes it,
   fails to confirm, and reverts to N+1 within the boot/fetch-fail budget
   — `/log` after recovery shows `trial for build N+2 failed — reverting`.
4. Set `Auto-update firmware` off in the portal; publish N+3; confirm no
   check fires (`/log`).
5. Drain the battery below 40%; confirm `maybeRunOtaCheck()` logs the
   battery gate and skips.
6. Confirm a `-dirty` local build never attempts a check even with a
   newer build published.

## Out of scope

- **Cryptographic image signing / cert pinning.** Chose HTTPS + MD5;
  `setInsecure()` stays consistent with the existing photo fetch. Signing
  is a clean follow-up if the threat model changes (public key baked in,
  `Update` verify hook).
- **Custom anti-rollback bootloader.** A fault before the `setup()` guard
  runs is not auto-recoverable — USB reflash. Accepted in the brainstorm.
- **Downgrade / pin-to-build from the portal.** The manifest's `build` is
  the only target; there's no UI to hold a board on an old image or force
  a specific one.
- **Delta / compressed updates.** Full app image every time (~1.2 MB over
  Wi-Fi, once per released build — negligible at daily cadence).
- **Release channels / staged rollout.** One rolling `firmware-latest` for
  every board of a given model.
- **OTA of the bootloader or partition table.** App image (`U_FLASH`)
  only; the partition layout already has room and is not expected to
  change.
- **Checking on interactive wakes.** Button / power-on wakes skip the
  check — a 30 s flash while someone waits for a photo is worse than
  waiting for the next scheduled wake.
- **Persisting OTA history across power loss** beyond the RTC trial
  counters — `/debug` shows the current state only.
