# Settings UI Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the settings portal around per-field auto-save (no Save
button), fold four fieldsets + four loose buttons into two groups + a
collapsed **Advanced** disclosure, replace the manual "Check for firmware
update" button with an auto-check-on-open status pill, swap "Forget Wi-Fi"
for "Factory reset", hard-code the firmware check interval to daily, and
unify the user-facing vocabulary (image / device / display).

**Architecture:** A new `POST /set` endpoint persists one validated field
per request (`logic/validate.h` helpers, `200 ok` / `400 <msg>`). ~50
lines of inline vanilla JS in `portal_html.h` wire control events to it
and render per-field `✓` / error. A module-level `pendingPanel` flag,
set by `/set` for visible-field changes and acted on **between**
`server.handleClient()` calls in `runPortal()` and the dev `loop()`,
retires the "save = exit portal, fetch, sleep" model —
`PortalResult` narrows to `{KeyExit, Timeout}`. Firmware: `GET /ota/peek`
(gate relaxed — a peek is harmless) + `POST /ota/install` (uses a new
`canManualUpdate()` so it works with auto-update off) replace the old
action routes. `POST /factory-reset` does `wm.resetSettings()` +
`prefs.clear()` + reboot.

**Tech Stack:** Arduino/ESP32 (PlatformIO, `framework = arduino`), the
bundled `WebServer`, Unity for native host tests. Inline vanilla JS in the
device-served page (no framework, no CDN — the Artifact CSP does not apply
here). The `portal_html.h` "no JS" note is retired.

## Global Constraints

- **Per-field save**: `/set` writes exactly one field, validates only
  that field, and on rejection writes nothing and returns the message as
  `text/plain`. No top-of-page `.msg` error box.
- **Panel re-render policy**: only `url` (valid + changed) → fetch+redraw,
  and `rot` → re-dither the cached image (no network). Every other field
  persists silently. Never re-render on pause/unpause.
- **`pendingPanel` never runs inside an HTTP handler** — it is a flag the
  service loop consumes between `handleClient()` calls, so the ~30 s draw
  doesn't stall the request that set it.
- **Firmware peek** (`/ota/peek`) ignores the `otaEnabled` toggle —
  requires only `FW_BUILD_NUMBER > 0` and a non-`-dirty` hash.
- **Firmware install** (`/ota/install`) via `canManualUpdate()`: every
  gate `shouldCheckForUpdate` checks *except* `enabled` (battery ≥ 40 %,
  not `-dirty`, not on trial, build > 0).
- **Check interval** is the compile-time constant
  `OTA_CHECK_INTERVAL_SECS = 24*60*60`. No runtime setting; NVS key
  `otaSecs` is abandoned in place (no migration).
- **Vocabulary**: user-facing strings only — `image` not photo/picture,
  `device` not frame, `display` not panel. Not `docs/`, not identifiers,
  not `src/logic` comments.
- Every task ends on a green `pio run -e ee02` (and `pio test -e native`
  where logic changed).

---

### Task 1: `canManualUpdate()` pure logic + native tests

**Files:**
- Modify: `src/logic/firmware_update.h`
- Test: `test/test_firmware_update/main.cpp`

**Interfaces:**
- Produces: `bool canManualUpdate(const OtaGate &)` — every gate
  `shouldCheckForUpdate` applies except `enabled`. `shouldCheckForUpdate`
  becomes `g.enabled && canManualUpdate(g)`. Consumed by Task 3.

- [ ] **Step 1: Extend the test**

In `test/test_firmware_update/main.cpp`, add:

```cpp
void test_manual_ignores_enabled_toggle() {
    OtaGate g = ok(); g.enabled = false;
    TEST_ASSERT_TRUE(canManualUpdate(g));          // toggle doesn't matter
    TEST_ASSERT_FALSE(shouldCheckForUpdate(g));    // ...but the auto path still needs it
}

void test_manual_still_honours_other_gates() {
    OtaGate g = ok(); g.enabled = false;
    g.deviceDirty = true;   TEST_ASSERT_FALSE(canManualUpdate(g)); g.deviceDirty = false;
    g.trialPending = true;  TEST_ASSERT_FALSE(canManualUpdate(g)); g.trialPending = false;
    g.deviceBuild = 0;      TEST_ASSERT_FALSE(canManualUpdate(g)); g.deviceBuild = 100;
    g.batteryPct = OTA_MIN_BATTERY_PCT - 1; TEST_ASSERT_FALSE(canManualUpdate(g));
}
```

and `RUN_TEST(test_manual_ignores_enabled_toggle);` /
`RUN_TEST(test_manual_still_honours_other_gates);` in `main()`.

- [ ] **Step 2: Run to verify it fails**

Run: `pio test -e native -f test_firmware_update`
Expected: FAIL to compile — `canManualUpdate` undeclared.

- [ ] **Step 3: Implement**

In `src/logic/firmware_update.h`, replace:

```cpp
inline bool shouldCheckForUpdate(const OtaGate &g) {
    return g.enabled && !g.deviceDirty && !g.trialPending
        && g.deviceBuild > 0 && g.batteryPct >= OTA_MIN_BATTERY_PCT;
}
```

with:

```cpp
// Manual portal check/install: the user asked, so the "auto-update"
// toggle doesn't apply — but every safety gate still does.
inline bool canManualUpdate(const OtaGate &g) {
    return !g.deviceDirty && !g.trialPending
        && g.deviceBuild > 0 && g.batteryPct >= OTA_MIN_BATTERY_PCT;
}

inline bool shouldCheckForUpdate(const OtaGate &g) {
    return g.enabled && canManualUpdate(g);
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `pio test -e native -f test_firmware_update`
Expected: PASS, all cases green.

- [ ] **Step 5: Commit**

```bash
git add src/logic/firmware_update.h test/test_firmware_update/main.cpp
git commit -m "firmware_update: add canManualUpdate (all gates but the auto-update toggle)"
```

---

### Task 2: Hard-code the firmware check interval

**Files:**
- Modify: `src/config.h`, `src/settings.h`, `src/settings.cpp`,
  `src/net.cpp`, `src/portal.cpp`, `src/portal_html.h`

**Interfaces:**
- Removes: `Settings::otaCheckSecs`, NVS key `otaSecs`,
  `DEFAULT_OTA_CHECK_SECS`, `otaIntervalOptions()`, form field `ota_secs`.
- Produces: `constexpr uint32_t OTA_CHECK_INTERVAL_SECS` in `config.h`.

- [ ] **Step 1: `config.h`**

Replace:

```cpp
constexpr bool     DEFAULT_OTA_ENABLED    = true;         // opt-out
constexpr uint32_t DEFAULT_OTA_CHECK_SECS = 24 * 60 * 60; // daily
```

with:

```cpp
constexpr bool     DEFAULT_OTA_ENABLED       = true;          // opt-out
constexpr uint32_t OTA_CHECK_INTERVAL_SECS   = 24 * 60 * 60;  // fixed: daily
```

- [ ] **Step 2: `settings.h` / `settings.cpp`**

Remove `uint32_t otaCheckSecs;` from `struct Settings`. In
`loadSettings()` remove the `settings.otaCheckSecs = prefs.getUInt("otaSecs", ...)`
line; in `saveSettings()` remove `prefs.putUInt("otaSecs", ...)`. Update
the NVS-keys comment (drop `otaSecs`).

- [ ] **Step 3: `net.cpp`**

In `maybeRunOtaCheck()`, the cadence check uses `settings.otaCheckSecs`
— change to `OTA_CHECK_INTERVAL_SECS`:

```cpp
if (os.lastCheckEpoch != 0 &&
    now - (time_t)os.lastCheckEpoch < (time_t)OTA_CHECK_INTERVAL_SECS)
    return;
```

- [ ] **Step 4: `portal.cpp`**

- Delete `otaIntervalOptions()`.
- In `buildPage()` delete the `page.replace("%OTA_OPTS%", ...)` line.
- In `handleSave()` delete the `otaSecs` parse + clamp + the
  `settings.otaCheckSecs = otaSecs;` assignment.
- In `handleDebug()` change the OTA-state string from
  `"on, every " + String(settings.otaCheckSecs / 3600) + "h"` to just
  `"on"`.

- [ ] **Step 5: `portal_html.h`**

In the Firmware fieldset, delete:

```html
<label>Check for updates</label><select name="ota_secs">%OTA_OPTS%</select>
```

- [ ] **Step 6: Build**

Run: `pio run -e ee02` — Expected: SUCCESS.
Run: `pio test -e native` — Expected: 118/… still green (no logic touched).

- [ ] **Step 7: Commit**

```bash
git add src/config.h src/settings.h src/settings.cpp src/net.cpp src/portal.cpp src/portal_html.h
git commit -m "ota: drop the check-interval setting, hard-code to daily"
```

---

### Task 3: Relax the peek gate; forced install uses `canManualUpdate`

**Files:**
- Modify: `src/net.cpp`

**Interfaces:**
- Consumes: `canManualUpdate()` (Task 1).

- [ ] **Step 1: `maybeRunOtaCheck()` eligibility**

Replace:

```cpp
    if (!shouldCheckForUpdate(gate)) {
        if (force || (settings.otaEnabled && !gate.trialPending))
            devLog.printf("ota: check skipped (enabled=%d build=%u dirty=%d "
                          "trial=%d batt=%d%%)\n", ...);
        return;
    }
```

with:

```cpp
    bool eligible = force ? canManualUpdate(gate) : shouldCheckForUpdate(gate);
    if (!eligible) {
        if (force || (settings.otaEnabled && !gate.trialPending))
            devLog.printf("ota: check skipped (enabled=%d build=%u dirty=%d "
                          "trial=%d batt=%d%%)\n",
                          (int)gate.enabled, gate.deviceBuild,
                          (int)gate.deviceDirty, (int)gate.trialPending,
                          batteryPct);
        return;
    }
```

Then replace the later `if (!shouldInstallUpdate(gate, mi.build))` with an
`isNewer` check (eligibility is already decided above):

```cpp
    if (mi.build <= gate.deviceBuild) {
        devLog.printf("ota: up to date (running %u, latest %u)\n",
                      gate.deviceBuild, mi.build);
        return;
    }
```

- [ ] **Step 2: `otaPeek()` — relax the gate**

Replace its `if (!shouldCheckForUpdate(gate)) { ... return OtaPeekResult::Blocked; }`
block with a peek-only gate (no `enabled`, no battery — a peek neither
flashes nor drains):

```cpp
    if (gate.deviceDirty || gate.deviceBuild == 0) {
        devLog.printf("ota: peek blocked (build=%u dirty=%d)\n",
                      gate.deviceBuild, (int)gate.deviceDirty);
        return OtaPeekResult::Blocked;
    }
    if (gate.trialPending) {
        devLog.println("ota: peek blocked (an update is on trial)");
        return OtaPeekResult::Blocked;
    }
```

- [ ] **Step 3: Build**

Run: `pio run -e ee02` — Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/net.cpp
git commit -m "ota: peek ignores the auto-update toggle; forced install uses canManualUpdate"
```

---

### Task 4: New endpoints (`/set`, `/ota/peek`, `/ota/install`, `/factory-reset`)

**Files:**
- Modify: `src/portal.cpp`

**Interfaces:**
- Produces: `handleSet` / `handleOtaPeek` / `handleOtaInstall` /
  `handleFactoryReset` + their `server.on(...)` routes, and a
  file-scope `enum class PendingPanel { None, RenderCached, Fetch }
  pendingPanel = PendingPanel::None;`.
- Additive — the old handlers/routes stay until Task 5, so the build
  stays green throughout.

- [ ] **Step 1: `pendingPanel` state**

Near the other portal file-scope statics:

```cpp
enum class PendingPanel : uint8_t { None, RenderCached, Fetch };
static PendingPanel pendingPanel = PendingPanel::None;
```

- [ ] **Step 2: `handleSet`**

```cpp
// One field per request. Validates just that field; on rejection writes
// nothing and returns the message. Sets pendingPanel for visible fields.
static void handleSet() {
    lastActivityMs = millis();
    String f = server.arg("f");
    String v = server.arg("v");
    String err;

    if (f == "sleep") {
        uint32_t s = (uint32_t)v.toInt();
        if (!isValidSleepSecs(s)) err = "Invalid interval.";
        else { settings.sleepSecs = s; saveSettings(); }
    } else if (f == "url") {
        if (!isValidImageUrl(v.c_str()))
            err = "Must be http(s) and under 512 characters.";
        else if (v != settings.imageUrl) {
            settings.imageUrl = v; saveSettings();
            pendingPanel = PendingPanel::Fetch;
        }
    } else if (f == "paused") {
        held = (v == "1" || v == "true" || v == "on");
        prefs.putBool("held", held);
    } else if (f == "quiet_en") {
        settings.quietEnabled = (v == "1" || v == "true" || v == "on");
        saveSettings();
    } else if (f == "quiet_start" || f == "quiet_end") {
        int h = v.toInt();
        if (!isValidHour(h)) err = "Invalid hour.";
        else {
            if (f == "quiet_start") settings.quietStartHour = (uint8_t)h;
            else                    settings.quietEndHour = (uint8_t)h;
            if (settings.quietEnabled &&
                settings.quietStartHour == settings.quietEndHour)
                err = "Start and end must differ.";
            else saveSettings();
        }
    } else if (f == "tz") {
        if (v == "auto") { settings.tzAuto = true; saveSettings(); }
        else if (!isValidTzOffsetSec(v.toInt())) err = "Invalid offset.";
        else { settings.tzAuto = false; saveSettings(); prefs.putLong("tzOff", v.toInt()); }
    } else if (f == "name") {
        if (!isValidDeviceName(v.c_str()))
            err = "1-24 of a-z, 0-9, hyphen (not at the ends).";
        else { settings.name = v; saveSettings(); }
    } else if (f == "rot") {
        int r = v.toInt();
        if (!isValidRotation(r)) err = "Invalid orientation.";
        else if ((uint8_t)r != settings.rotation) {
            settings.rotation = (uint8_t)r; saveSettings();
            pendingPanel = PendingPanel::RenderCached;
        }
    } else if (f == "ota_en") {
        settings.otaEnabled = (v == "1" || v == "true" || v == "on");
        saveSettings();
    } else {
        err = "Unknown field.";
    }

    if (err.isEmpty()) server.send(200, "text/plain", "ok");
    else               server.send(400, "text/plain", err);
}
```

(Note `isValidImageUrl` caps at 512; the spec/UI text says 512 — the
existing `handleSave` message said "under 512", keep that wording.)

- [ ] **Step 3: `handleOtaPeek`**

```cpp
static void handleOtaPeek() {
    lastActivityMs = millis();
    uint32_t latest = 0;
    const char *body;
    switch (otaPeek(latest)) {
        case OtaPeekResult::Available:   { server.send(200, "text/plain",
            "available " + String(latest)); return; }
        case OtaPeekResult::UpToDate:    body = "uptodate"; break;
        case OtaPeekResult::Blocked:     body = "blocked"; break;
        default:                         body = "unreachable"; break;
    }
    server.send(200, "text/plain", body);
}
```

- [ ] **Step 4: `handleOtaInstall`**

```cpp
static void handleOtaInstall() {
    lastActivityMs = millis();
    devLog.println("portal: manual firmware install");
    otaInstallNow();            // reboots on success
    server.send(200, "text/plain", "nothing newer to install");
}
```

- [ ] **Step 5: `handleFactoryReset`**

```cpp
static void handleFactoryReset() {
    lastActivityMs = millis();
    server.send(200, "text/plain", "erasing");
    devLog.println("portal: factory reset");
    delay(300);                 // flush the response
    WiFiManager wm;
    wm.resetSettings();         // Wi-Fi credentials (separate NVS)
    prefs.clear();              // wipes the whole "frame" namespace
    delay(100);
    ESP.restart();
}
```

- [ ] **Step 6: Routes**

In `startPortal()`'s route block, add:

```cpp
        server.on("/set", HTTP_POST, handleSet);
        server.on("/ota/peek", HTTP_GET, handleOtaPeek);
        server.on("/ota/install", HTTP_POST, handleOtaInstall);
        server.on("/factory-reset", HTTP_POST, handleFactoryReset);
```

- [ ] **Step 7: Build**

Run: `pio run -e ee02` — Expected: SUCCESS (unused-function warnings are
fine; they're wired up in Task 5).

- [ ] **Step 8: Commit**

```bash
git add src/portal.cpp
git commit -m "portal: add /set, /ota/peek, /ota/install, /factory-reset handlers"
```

---

### Task 5: The swap — new page, JS, `pendingPanel` loop, delete the old model

**Files:**
- Modify: `src/portal_html.h`, `src/portal.cpp`, `src/portal.h`,
  `src/main.cpp`

**Interfaces:**
- `PortalResult` → `{ KeyExit, Timeout }`.
- `buildPage()` loses its `error` parameter.
- Deletes: `handleSave`, `handleNewPic`, `handleCheckUpdate`,
  `handleInstallUpdate`, `handleForgetWifi` + their routes.

- [ ] **Step 1: Rewrite `PORTAL_HTML` in `src/portal_html.h`**

New body — status strip, two fieldsets, `<details>` Advanced, inline
`<script>`. Placeholders: `%NAME%` `%STATUS%` `%SLEEP_OPTS%` `%URL%`
`%PAUSED%` `%QUIET_EN%` `%QS_OPTS%` `%QE_OPTS%` `%TZ_OPTS%` `%ROT_OPTS%`
`%OTA_EN%` `%HASH%` `%BUILD%`. Each field control gets a trailing
`<span class="hint" data-for="<field>"></span>`. Full markup:

```html
<h1>%NAME% — settings</h1>
<p class="note" id="strip">%STATUS% &middot; build %BUILD%
  <span id="fw"></span></p>

<fieldset><legend>Image</legend>
<label>New image every</label>
<select name="sleep">%SLEEP_OPTS%</select><span class="hint" data-for="sleep"></span>
<label>Image source URL</label>
<input type="text" name="url" value="%URL%" maxlength="512">
<span class="hint" data-for="url"></span>
<div class="note">Must return a baseline (non-progressive) JPEG.
Tokens: {width} {height} {seed}</div>
<label><input type="checkbox" name="paused" %PAUSED%> Pause — keep the current image</label>
<span class="hint" data-for="paused"></span>
<label><input type="checkbox" name="quiet_en" %QUIET_EN%> Skip updates between</label>
<div class="row"><div><select name="quiet_start">%QS_OPTS%</select></div>
<div><select name="quiet_end">%QE_OPTS%</select></div></div>
<span class="hint" data-for="quiet_end"></span>
</fieldset>

<fieldset><legend>Device</legend>
<label>Timezone</label><select name="tz">%TZ_OPTS%</select>
<span class="hint" data-for="tz"></span>
<label>Device name</label>
<input type="text" name="name" value="%NAME%" maxlength="24">
<div class="note">lowercase letters, digits, hyphens &middot;
reachable at <b>%NAME%.local</b></div>
<span class="hint" data-for="name"></span>
<label>Orientation</label><select name="rot">%ROT_OPTS%</select>
<span class="hint" data-for="rot"></span>
</fieldset>

<details><summary>Advanced</summary>
<label><input type="checkbox" name="ota_en" %OTA_EN%> Install updates automatically</label>
<span class="hint" data-for="ota_en"></span>
<div class="note" style="margin-top:.8rem">Running %HASH%</div>
<div id="fr"><button type="button" class="danger" onclick="frExpand()">Factory reset</button></div>
<div class="note">Erases all settings and Wi-Fi. The device restarts into setup mode.</div>
</details>

<script>
/* inline, no framework — served from the device */
</script>
```

- [ ] **Step 2: The inline `<script>`**

```js
var $ = function (s, r) { return (r || document).querySelector(s); };
function hint(f, msg, ok) {
  var h = $('.hint[data-for="' + f + '"]');
  if (!h) return;
  h.textContent = ok ? ' ✓' : ' ' + msg;
  h.style.color = ok ? '#0a0' : '#c00';
  if (ok) setTimeout(function () { if (h.textContent === ' ✓') h.textContent = ''; }, 1500);
}
function save(f, v) {
  fetch('/set', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'f=' + encodeURIComponent(f) + '&v=' + encodeURIComponent(v)
  }).then(function (r) {
    return r.text().then(function (t) { hint(f, t, r.ok); });
  }).catch(function () { hint(f, 'offline', false); });
}
function fieldVal(el) {
  return el.type === 'checkbox' ? (el.checked ? '1' : '0') : el.value;
}
document.addEventListener('change', function (e) {
  var el = e.target;
  if (!el.name || el.tagName === 'INPUT' && el.type === 'text') return;
  save(el.name, fieldVal(el));
});
document.addEventListener('blur', function (e) {
  var el = e.target;
  if (!el.name || !(el.tagName === 'INPUT' && el.type === 'text')) return;
  if (el.value === el.defaultValue) return;
  el.defaultValue = el.value;
  save(el.name, el.value);
}, true);

/* firmware pill */
fetch('/ota/peek').then(function (r) { return r.text(); }).then(function (t) {
  var fw = $('#fw'), p = t.trim().split(' ');
  if (p[0] === 'uptodate') fw.textContent = ' · up to date';
  else if (p[0] === 'available') {
    fw.innerHTML = ' <button type="button" class="primary" id="upd">update to ' + p[1] + '</button>';
    $('#upd').onclick = function () {
      fw.innerHTML = ' Install build ' + p[1] + '? ' +
        '<button type="button" class="primary" id="doupd">Install</button> ' +
        '<button type="button" id="noupd">Cancel</button>';
      $('#noupd').onclick = function () { location.reload(); };
      $('#doupd').onclick = function () {
        fw.textContent = ' Installing — the device restarts in ~30 s';
        fetch('/ota/install', { method: 'POST' }).catch(function () {});
      };
    };
  } else fw.innerHTML = ' · <a href="/log">update check failed</a>';
}).catch(function () { $('#fw').textContent = ' · update check failed'; });

function frExpand() {
  $('#fr').innerHTML = 'Erase all settings and Wi-Fi? ' +
    '<button type="button" class="danger" id="frgo">Erase everything</button> ' +
    '<button type="button" id="frno" onclick="location.reload()">Cancel</button>';
  $('#frgo').onclick = function () {
    $('#fr').textContent = 'Erasing — the device restarts into setup mode.';
    fetch('/factory-reset', { method: 'POST' }).catch(function () {});
  };
}
```

Add `.hint{font-size:.85rem}` and keep `.danger` in the `<style>` block.

- [ ] **Step 3: `buildPage()` — drop the error arg**

```cpp
static String buildPage() {
    String page = FPSTR(PORTAL_HTML);
    page.replace("%NAME%", settings.name);
    page.replace("%STATUS%", statusLine());
    page.replace("%SLEEP_OPTS%", sleepOptions(settings.sleepSecs));
    page.replace("%PAUSED%", held ? "checked" : "");
    page.replace("%QUIET_EN%", settings.quietEnabled ? "checked" : "");
    page.replace("%QS_OPTS%", selectOptions(0, 23, settings.quietStartHour, ":00"));
    page.replace("%QE_OPTS%", selectOptions(0, 23, settings.quietEndHour, ":00"));
    page.replace("%TZ_OPTS%", tzOptions());
    page.replace("%ROT_OPTS%", rotOptions());
    page.replace("%OTA_EN%", settings.otaEnabled ? "checked" : "");
    page.replace("%HASH%", FW_GIT_HASH);
    page.replace("%BUILD%", String((uint32_t)FW_BUILD_NUMBER));
    page.replace("%URL%", htmlEscape(settings.imageUrl)); // last: stored URL may contain %TOKENS%
    return page;
}
```

Update `handleRoot()` → `server.send(200, "text/html", buildPage());`.
In `portal.h`, if `buildPage` is declared there, update the signature;
it's `static` in the .cpp, so likely just the .cpp.

- [ ] **Step 4: `statusLine()` — "next image", drop trailing build (now in HTML)**

The status strip HTML already appends `&middot; build %BUILD%`. Leave
`statusLine()` producing `battery NN% · next image HH:MM` (rename "next
photo" → "next image" here).

- [ ] **Step 5: `PortalResult` → `{ KeyExit, Timeout }` (`src/portal.h`)**

```cpp
enum class PortalResult { KeyExit, Timeout };
```

Update the doc comment (drop the ForgetWifi note).

- [ ] **Step 6: Delete the old handlers + routes (`src/portal.cpp`)**

Delete `handleSave`, `handleNewPic`, `handleCheckUpdate`,
`handleInstallUpdate`, `handleForgetWifi`. Delete their
`server.on("/save"...)`, `/action/newpic`, `/action/checkupdate`,
`/action/installupdate`, `/action/forgetwifi` route registrations.
`result` is now only ever `KeyExit` / `Timeout`; the `Saved` /
`ForgetWifi` enum cases and any `switch` arms for them go.

- [ ] **Step 7: `pendingPanel` in `runPortal()`**

In the `while (!exitRequested)` loop, after `server.handleClient();`:

```cpp
        if (pendingPanel != PendingPanel::None) {
            PendingPanel p = pendingPanel;
            pendingPanel = PendingPanel::None;
            applyOrientation();
            setLed(LedMode::Heartbeat);
            if (p == PendingPanel::Fetch) {
                doFetchCycle(true);
            } else if (renderCachedPhoto()) {
                devLog.println("updating display (takes ~20-30 s)...");
                epaper.update();
            }
            setLed(LedMode::Solid);
            lastActivityMs = millis(); // the long draw isn't idleness
        }
```

`runPortal` will need whatever includes `doFetchCycle` needs — it's a
`static` in `main.cpp`. **Move the panel action into a small exported
helper** instead: add to `net.h`/`net.cpp` (or `display`) is wrong layer;
simplest — add `void applyPendingPanel(bool fetch)` to `main.cpp` and
declare it `extern` for `portal.cpp`, OR keep the logic in `portal.cpp`
and call the already-exported `renderCachedPhoto()` + `epaper.update()`
for `RenderCached`, and for `Fetch` set a flag the **caller** consumes.
Chosen: `runPortal` handles `RenderCached` itself (only needs
`renderCachedPhoto` + `epaper`, both already reachable from portal.cpp),
and for `Fetch` it returns a new outcome. **Simpler still:** portal.cpp
already can't see `doFetchCycle`. So:

- `RenderCached` → handled inline in `runPortal` (portal.cpp has
  `display.h`).
- `Fetch` → `runPortal` sets `exitRequested` with a new internal reason
  and returns `PortalResult::KeyExit`-equivalent... no.

**Final decision:** expose `bool takePortalFetch()` from `portal.cpp`
(returns true once if a `url` change asked for a fetch), and have the
KEY1 caller (`runStatusMode` in `main.cpp`) and the dev `loop()` call
`doFetchCycle(true)` when it returns true — mirroring today's
`takePortalAction()`. `RenderCached` stays fully inside `runPortal` /
`servicePortal`. So:

```cpp
// portal.cpp
static bool pendingFetch = false;   // url changed
static bool pendingRender = false;  // rot changed
bool takePortalFetch() { bool f = pendingFetch; pendingFetch = false; return f; }
static void servicePendingRender() {
    if (!pendingRender) return;
    pendingRender = false;
    applyOrientation();
    if (renderCachedPhoto()) { devLog.println("updating display..."); epaper.update(); }
    lastActivityMs = millis();
}
```

`handleSet` sets `pendingFetch` / `pendingRender` instead of the enum.
`runPortal`'s loop and `servicePortal()` both call
`servicePendingRender()` after `handleClient()`.

- [ ] **Step 8: `main.cpp` — `runStatusMode()` + dev `loop()`**

- `runStatusMode()`: after `runPortal()` returns, delete the
  `if (r == PortalResult::Saved || r == PortalResult::ForgetWifi) { doFetchCycle(true); return true; }`
  block. Add: `if (takePortalFetch()) { doFetchCycle(true); return true; }`.
  The remaining `KeyExit` / `Timeout` path (redisplay cached image) is
  unchanged.
- dev `loop()`: replace the `if (takePortalAction()) { … doFetchCycle(true); … }`
  block with `if (takePortalFetch()) { setLed(Solid); doFetchCycle(true); setLed(Off); }`.
  `servicePortal()` already runs each loop and now also flushes
  `servicePendingRender()`.
- Remove `takePortalAction()` / `setPortalPersistent`-adjacent dead bits
  only if fully unused (leave `setPortalPersistent`/`servicePortal`).

- [ ] **Step 9: `portal.h`**

Declare `bool takePortalFetch();`. Remove `bool takePortalAction();` if
now unused everywhere (grep first).

- [ ] **Step 10: Build**

Run: `pio run -e ee02` — Expected: SUCCESS.
Run: `pio test -e native` — Expected: green.

- [ ] **Step 11: Commit**

```bash
git add src/portal_html.h src/portal.cpp src/portal.h src/main.cpp
git commit -m "portal: per-field auto-save page, firmware pill, Advanced/Factory reset; drop the form-submit model"
```

---

### Task 6: Vocabulary sweep

**Files:**
- Modify: `src/portal_html.h`, `src/portal.cpp`, `src/main.cpp`,
  `src/net.cpp`, `src/display.cpp`, `src/photocache.cpp` (user-facing
  strings + `devLog` lines only)

- [ ] **Step 1: Find the strings**

```bash
grep -rnE '"[^"]*(photo|picture|frame|panel)[^"]*"' src/*.cpp src/*.h \
  | grep -viE 'logic/|BOARD_MODEL|DEVICE_NAME|framebuffer|epaper|Panel [A-Z]|panel-combos'
```

- [ ] **Step 2: Apply**

- `photo` / `picture` → `image` (e.g. "keeping photo" → "keeping image",
  "New picture on the way" — that handler is deleted; `showError` /
  status-screen wording; devLog).
- `frame` → `device` in user-visible text; leave `RTC_DATA_ATTR`
  comments, `docs`, and any `AP_NAME`/product-name literals.
- `panel` → `display` where it means the screen in user-facing copy
  ("updating panel" devLog → "updating display"); leave `epaper`,
  `initPanelColorMode`, `PANEL_NATIVE_LANDSCAPE`, `panel-combos.md`.
- `<title>` already handled in Task 5.

Keep changes limited to string literals a user reads (`devLog.print*`,
`server.send` bodies, `showError`, `drawStatusScreen` labels, HTML).

- [ ] **Step 3: Build**

Run: `pio run -e ee02` — Expected: SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "Unify user-facing vocabulary: image / device / display"
```

---

### Task 7: Full verification pass

**Files:** none (verification only)

- [ ] **Step 1: Native suite**

Run: `pio test -e native` — Expected: PASS, including the new
`canManualUpdate` cases.

- [ ] **Step 2: Build every env**

```bash
pio run -e ee02 && pio run -e ee03 && pio run -e ee04 && pio run -e ee05
```

- [ ] **Step 3: Flash + browser (per spec's test list)**

Flash `ee02`. Over `http://ee02.local`:

1. **Auto-save**: toggle each control → `✓` appears; reload → persisted.
   Invalid name (`AB_`) and invalid URL (`ftp://x`) → inline red error,
   `/log` and a reload show nothing was written, other fields still save.
2. **Timeout**: change a field, leave it 10 min, reopen — change is
   still there.
3. **Re-render policy**: change orientation → `/log` shows *no* GET, the
   display re-draws rotated. Change the image URL → `/log` shows a GET,
   display updates. Change timezone / name / auto-update → `/log` shows
   no display or network activity.
4. **Firmware pill**: on a build behind `firmware-latest` → strip shows
   `update to N` → click → inline confirm → Install → device reboots →
   `/debug` new build, `/log` `on trial` → `confirmed good`. On the
   latest → ` · up to date`. Block internet → ` · update check failed`,
   no error box.
5. **Install with auto-update off**: untick "Install updates
   automatically", reload → pill still shows, Install still works.
6. **Factory reset**: Advanced → Factory reset → Erase everything →
   device reboots to `EE02-Setup`, panel shows setup text; after
   re-provisioning `/debug` shows defaults (image URL, interval,
   name `ee02`, etc.).

Needs a human at the board with control of the LAN/internet and the
physical KEY1. The assistant can drive `curl` against the endpoints but
can't judge the display or re-provision Wi-Fi.

- [ ] **Step 4: Update the spec status**

Add a one-line "shipped" note (merge commit, any deviations) to the top
of `docs/superpowers/specs/2026-09-07-settings-ui-redesign-design.md`.

- [ ] **Step 5: Commit any fixes from Step 3**

If verification required code changes, commit them with a description of
what broke.
