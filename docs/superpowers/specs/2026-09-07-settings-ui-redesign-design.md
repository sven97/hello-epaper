# Settings page redesign: auto-save, fewer groups, no loose buttons

## Context

The settings portal (`portal_html.h` + `portal.cpp`) grew by accretion as
features landed (quiet hours, on-device config, status dashboard, auto
firmware update, the manual check button). It's now four `<fieldset>`s
plus four standalone `<form>` buttons with three different meanings of
"button", and the copy mixes "photo"/"picture"/"frame" freely. A UX read
(see the 2026-09-06 review in-session) found:

- **"Save & apply"** submits the settings `<form>`; the other three
  buttons are their own forms POSTing elsewhere — an implementation
  detail (HTML can't nest forms) leaking into the layout.
- Every save triggers a **full image fetch + ~30 s panel refresh**, even
  when only a firmware toggle changed.
- The portal times out after 10 min; **unsaved edits are silently lost**.
- **"Fetch new picture now"** duplicates the physical KEY2 button and
  `/debug/key2`.
- **"Check for firmware update now"** is a click-and-wait when the answer
  ("are we current?") could just be shown.
- **"Forget Wi-Fi"** is an oddly specific escape hatch.

This spec rebuilds the page around **per-field auto-save**, folds it to
three visible groups + one collapsed **Advanced** disclosure, replaces the
manual firmware button with an **auto-check-on-open status pill**, and
swaps "Forget Wi-Fi" for **Factory reset**. It also unifies the
user-facing vocabulary and drops the firmware check-interval setting
(hard-coded to daily).

The portal already serves from the device over the LAN — none of the
Artifact CSP constraints apply, and a small amount of inline vanilla JS is
fine. The `portal_html.h` "no JS" note is retired.

## Decisions locked (from the in-session Q&A)

| Fork | Choice |
|------|--------|
| Save model | **Per-field JS auto-save.** Selects/checkboxes on `change`, text on debounced `blur`. Per-field `saved ✓` / inline error. New partial-save endpoint. No Save button. |
| Panel re-render on change | **Only when a *visible* setting changed** (image URL → fetch; orientation → re-render cached), quietly. Name / timezone / pause / auto-update → persist only, no panel touch. |
| Firmware button | **Removed.** Replaced by an auto-check on page open that fills a status pill; click the pill to install (inline confirm). |
| Firmware check interval | **Removed as a setting.** Hard-coded `OTA_CHECK_INTERVAL_SECS = 24 h`. |
| "Forget Wi-Fi" | **Replaced by "Factory reset"** (wipe all settings + Wi-Fi, reboot to onboarding), in Advanced. |
| Vocabulary | **image** (not photo/picture), **device** (not frame), **display** (not panel) in all user-facing strings. |

## Page layout

```
<NAME> — settings
battery 86% · next image 23:50 · build 146            ← + firmware pill, JS-filled

┌ Image ────────────────────────────────────────────┐
│ New image every        [ 15 min ▾ ]               │
│ Image source URL       [ http://nas.local:8080… ] │
│   Must return a baseline JPEG. Tokens: {width} {height} {seed}
│ ☐ Pause — keep the current image                  │
│ ☐ Skip updates between [ 00:00 ▾ ] and [ 00:00 ▾ ]│
└───────────────────────────────────────────────────┘

┌ Device ───────────────────────────────────────────┐
│ Timezone               [ Auto (IP geolocation) ▾ ]│
│ Device name            [ ee02 ]                    │
│   lowercase letters, digits, hyphens · reachable at ee02.local
│ Orientation            [ Portrait ▾ ]             │
└───────────────────────────────────────────────────┘

▸ Advanced
    ☑ Install updates automatically
    [ Factory reset ]   Erases all settings and Wi-Fi; the device
                        restarts into setup mode.
```

- **Status strip** replaces the current `%STATUS%` note. `statusLine()`
  gains ` · build <N>`; the firmware state (` · up to date` /
  ` update to <N>` pill / ` · checking…` / ` · update check failed`) is
  appended client-side after `/ota/peek` returns.
- **Advanced** is a native `<details>` — collapse works with no JS.
- The three fieldsets collapse to two; quiet hours becomes one row inside
  Image (it was a whole fieldset for one checkbox + two selects).
- No `<button type=submit>` anywhere on the page except the two inside the
  Advanced affordances (install-confirm, factory-reset-confirm).

## Auto-save

### Endpoint: `POST /set`

Body: `f=<field>&v=<value>` (one field per request). The handler
`switch`es on `f`, validates just that field with the existing
`logic/validate.h` helpers, and on success persists it and updates the
in-RAM `settings` (and `held` / `tzOff` where those are the backing
store). Response:

- `200 text/plain "ok"` — saved. JS shows a transient `✓` by the field.
- `400 text/plain "<message>"` — rejected, nothing written. JS shows the
  message in red by the field; the control keeps the user's (invalid)
  value so they can fix it.

Fields: `sleep`, `url`, `paused`, `quiet_en`, `quiet_start`, `quiet_end`,
`tz`, `name`, `rot`, `ota_en`. (`ota_secs` is gone.) Unknown `f` → 400.

`handleSave()` and the `/save` route are **deleted**. `buildPage()` no
longer takes an `error` argument or substitutes `%ERROR%` (per-field
errors replace the top-of-page `.msg` box).

### Client JS (`portal_html.h`, inline `<script>`, ~50 lines)

- One delegated listener: `change` on selects/checkboxes, `blur` on text
  inputs. Text inputs also track their last-saved value and no-op if
  unchanged.
- `blur` on text is debounced only in the sense of "skip if unchanged";
  no timer needed (blur is already a discrete event).
- `save(f, v)` → `fetch('/set', {method:'POST', headers:{'Content-Type':
  'application/x-www-form-urlencoded'}, body: 'f='+enc(f)+'&v='+enc(v)})`;
  render `✓` or the error text into a `<span class="hint" data-for="f">`.
- Degrades: with JS disabled the controls still render and show current
  values; they just don't persist. (Acceptable — the portal is a
  first-party device page; every target browser runs JS.)

### Panel re-render on change

`/set` sets a module-level `pendingPanel` flag when a **visible** field
changed:

| field | effect |
|-------|--------|
| `url` (valid, changed) | `pendingPanel = Fetch` |
| `rot` | `pendingPanel = RenderCached` (re-dither the cached image at the new orientation; no network) |
| everything else | none |

Both portal service loops act on the flag **between** `handleClient()`
calls, without exiting the portal:

- `runPortal()` (KEY1 path): after `server.handleClient()`, `if
  (pendingPanel) { auto p = pendingPanel; pendingPanel = None;
  applyOrientation(); if (p == Fetch) doFetchCycle(true); else
  renderCachedPhoto()+epaper.update(); }`. The ~30 s draw blocks the
  portal — the JS shows "updating display…" from the `/set` 200 response
  for `url`/`rot`.
- dev `loop()`: same check alongside the existing `takePortalAction()`.

This retires the "save = exit portal, apply, fetch, sleep" model
(`PortalResult::Saved` from a form submit). `PortalResult` keeps
`KeyExit` / `Timeout` / `ForgetWifi` → but `ForgetWifi` is also gone
(factory reset reboots outright), so `PortalResult` narrows to `KeyExit`
/ `Timeout`. `runStatusMode()` no longer special-cases a post-portal
fetch; on KEY1/timeout exit it redisplays the cached image exactly as the
"nothing changed" path does today.

## Firmware update UX

### `GET /ota/peek`

Runs a check-only peek (no download/flash) and returns one line of
`text/plain`:

```
uptodate 146
available 147
blocked
unreachable
```

Reuses `otaPeek()` (`net.cpp`), with its gate **relaxed**: a peek is
harmless, so it no longer requires `settings.otaEnabled` — only
`FW_BUILD_NUMBER > 0` and a non-`-dirty` hash. (`otaPeek` still stamps the
last-check time, so an on-open peek also satisfies the daily cadence.)

### Client behavior

On `DOMContentLoaded`, after painting the page, `fetch('/ota/peek')`:

- while pending: status strip shows ` · checking…`
- `uptodate` → ` · up to date`
- `unreachable` / `blocked` → ` · update check failed` (quiet grey, links
  to `/log`)
- `available N` → renders `update to N` as a pill (button styling) in the
  status strip. Click → the pill expands in place to
  `Install build N?  [Install]  [Cancel]`. `Install` →
  `fetch('/ota/install', {method:'POST'})` then show "Installing — the
  device restarts in ~30 s"; the fetch itself will error as the board
  reboots (caught, ignored).

### `POST /ota/install`

`otaInstallNow()` unchanged in spirit — re-validates against the manifest
and, if still newer, downloads + MD5-verifies + flashes + reboots. Gate
**relaxed for the manual path**: add `canManualUpdate(gate)` to
`logic/firmware_update.h` (everything `shouldCheckForUpdate` checks
*except* `enabled`), and have `maybeRunOtaCheck(..., force=true)` use it
so "Install" works even when "Install updates automatically" is off. The
battery ≥ 40 % / not-`-dirty` / not-on-trial gates still hold.

Routes `/action/checkupdate`, `/action/installupdate` and their handlers
are deleted; `/ota/peek` + `/ota/install` replace them.

### Interval setting removed

- `Settings::otaCheckSecs` deleted from `settings.h` / `settings.cpp`;
  NVS key `otaSecs` becomes dead (left in place on existing devices, no
  migration).
- `config.h`: `DEFAULT_OTA_CHECK_SECS` → `constexpr uint32_t
  OTA_CHECK_INTERVAL_SECS = 24 * 60 * 60;`
- `maybeRunOtaCheck()` uses the constant instead of
  `settings.otaCheckSecs`.
- `/debug`: "auto-update on, every 24h" → "auto-update on" / "off".
- Portal form loses the `ota_secs` `<select>`, `otaIntervalOptions()`,
  `%OTA_OPTS%`.

## Advanced: Factory reset

### `POST /factory-reset`

```cpp
WiFiManager wm;
wm.resetSettings();     // Wi-Fi credentials (separate NVS)
prefs.clear();          // wipes the whole "frame" namespace: settings,
                        // held, tzOff, lastEpoch, OTA state, ...
delay(300);             // let the HTTP 200 flush
ESP.restart();
```

Next boot has no credentials → the existing onboarding path runs
(`EE02-Setup` AP, panel shows setup instructions) and every setting is
back to its `config.h` default.

### Affordance

`[ Factory reset ]` button → expands in place to
`Erase all settings and Wi-Fi?  [Erase everything]  [Cancel]` →
`Erase everything` does `fetch('/factory-reset', {method:'POST'})` then
shows "Erasing — the device restarts into setup mode." No native
`confirm()` dialog (consistent with the install affordance).

`handleForgetWifi()` and `/action/forgetwifi` are deleted.

## Vocabulary sweep

User-facing strings only (`portal_html.h`, `portal.cpp` `sendDone` /
`devLog` copy, `PORTAL_DONE_HTML`, page `<title>`). Not `docs/`, not
identifiers, not `src/logic` comments.

| was | now |
|-----|-----|
| photo, picture | image |
| frame | device |
| panel (as the screen) | display |
| "Refresh every" | "New image every" |
| "next photo HH:MM" | "next image HH:MM" |
| "Paused (pin current picture)" | "Pause — keep the current image" |
| "Skip refreshes between" | "Skip updates between" |
| "Name (mDNS: x.local)" | "Device name" + note "…reachable at x.local" |
| `<title>%NAME% — EE02 Frame</title>` | `<title>%NAME% — settings</title>` |

`devLog` lines (`"keeping photo"`, `"redisplay the cached photo"`, …) are
swept too — cosmetic, but they show on `/log`.

## Files touched

- `src/portal_html.h` — near rewrite: new structure, `<details>`,
  inline `<script>`, retire the no-JS note, drop `%ERROR%` / `%OTA_OPTS%`
  / firmware button forms, add the status-strip / pill / advanced markup.
- `src/portal.cpp` — delete `handleSave` / `handleNewPic` /
  `handleCheckUpdate` / `handleInstallUpdate` / `handleForgetWifi` and
  their routes and `otaIntervalOptions()`; add `handleSet` / `handleOtaPeek`
  / `handleOtaInstall` / `handleFactoryReset` and routes; `buildPage()`
  loses the `error` arg; `pendingPanel` flag + its check wired into
  `runPortal()`; `statusLine()` gains ` · build N`.
- `src/portal.h` — `PortalResult` narrows to `{ KeyExit, Timeout }`;
  `buildPage` signature.
- `src/main.cpp` — `runStatusMode()` drops the post-portal fetch special
  case; dev `loop()` acts on `pendingPanel`.
- `src/settings.h` / `src/settings.cpp` — remove `otaCheckSecs` /
  `otaSecs`.
- `src/config.h` — `DEFAULT_OTA_CHECK_SECS` → `OTA_CHECK_INTERVAL_SECS`.
- `src/net.cpp` / `src/net.h` — `maybeRunOtaCheck()` uses the constant;
  `otaPeek()` gate relaxed; `canManualUpdate()` path for forced installs.
- `src/logic/firmware_update.h` — add `canManualUpdate()`; native test.

## Testing

### Native (`pio test -e native`)

- `test_firmware_update`: add `canManualUpdate()` cases — `enabled=false`
  but otherwise-OK → true; still false on `-dirty` / `build==0` /
  `trialPending` / low battery.
- Existing suites unchanged.

### Build

`pio run` for ee02/ee03/ee04/ee05.

### Hardware / browser (this project's bar)

1. **Auto-save**: change each control; confirm `✓` appears, reload the
   page, value persisted. Enter an invalid name / URL; confirm inline
   red error, nothing persisted, other fields still saveable.
2. **Timeout**: change a field, wait out the 10-min portal timeout,
   reopen — the change is still there (no lost edits).
3. **Re-render policy**: change orientation → display re-draws the cached
   image rotated, no network fetch in `/log`. Change the image URL →
   `/log` shows a fetch, display updates. Change timezone / name /
   auto-update toggle → no display activity.
4. **Firmware pill**: open the page on a build behind `firmware-latest` →
   strip shows ` update to N`; click → inline confirm → Install →
   device reboots → `/debug` shows the new build, guard trace in `/log`
   (`on trial` → `confirmed good`). On the latest build → ` · up to
   date`. Kill Wi-Fi to the internet → ` · update check failed`, no
   error box.
5. **Install with auto-update off**: untick "Install updates
   automatically", reload, confirm the pill still appears and Install
   still works.
6. **Factory reset**: expand Advanced → Factory reset → confirm →
   device reboots into `EE02-Setup`, panel shows setup instructions,
   `/debug` (after re-provisioning) shows all settings at defaults.

## Out of scope

- **Inline Wi-Fi network editing** (change SSID/password without the
  hotspot). Discussed and deferred — switching Wi-Fi over the Wi-Fi
  you're leaving needs the hotspot as a recovery path anyway; factory
  reset covers the switch case for now.
- **Release notes / changelog in the update pill** — just the build
  number.
- **A scan-and-pick Wi-Fi list.**
- **Localization** — English only.
- Any change to the on-device status/onboarding screens
  (`drawFrameScreen`, `showError`) beyond the photo→image wording.
- Re-rendering the display on a pause/unpause toggle.
