# Paperframe: EE02-only, rebrand, and a fixed-grid status page — design

Status: approved for planning. Source: in-session mockup + Q&A (2026-09-09).
Supersedes the multi-panel fitting engine from
`docs/superpowers/specs/2026-08-18-unified-status-screen-design.md`.

## Context

Three decisions collapse into one piece of work:

1. **The project is EE02-only now.** One developer, one board: a XIAO
   ESP32-S3 on the 13.3" Spectra-6 colour panel, 1200×1600, portrait-native.
   EE03/EE04/EE05 were never physically verified (every spec since August
   says so). Their code paths — a landscape-native rotation branch, a
   16-level-gray sprite mode, a ~10-panel combo table — are untested
   complexity that also forced the status renderer to be a general solver.
2. **The status page should look like the 2026-09-09 mockup**: a centred
   window on a 12-column grid, 24px gutters, 48px inner padding, with
   status / settings / device details / button legend all sharing column
   alignments. A fixed panel size makes this a hand-placed layout, not a
   content-fitting problem.
3. **Rebrand to "Paperframe".** "Hello ePaper" was a placeholder.

Doing these together is deliberate: stripping the boards is what makes the
fixed-grid renderer possible, and the rebrand touches the same strings the
new status page introduces.

## Decisions locked (from the in-session Q&A)

| Fork | Choice |
|------|--------|
| Board support | **EE02 only.** Delete `ee03`/`ee04`/`ee05` PlatformIO envs and every board conditional. Panel is a compile-time constant: 1200×1600, Spectra-6 colour, portrait-native. |
| Layout engine | **Fixed 12-column grid.** Delete `fitScreen()`, the font ladders, `fitSize()`, the height-aware shrink pass, the QR-scale search, and the reduction cascade. Keep the one shared renderer and the `ScreenState` (Normal / Onboarding / Error) split. |
| Sequencing | **One spec, one PR** covering board removal + status redesign + rebrand + version-string change. |
| Version string | **Build number, for display and OTA.** No semver. Status page header shows `Firmware build <N>` (`FW_BUILD_NUMBER`). OTA ordering key unchanged. `FW_GIT_HASH` stays in `/log` + `/debug` for provenance only. |
| Branding | **"Paperframe"** as the product name. Generic nouns in copy stay **image / device / display** (per the 2026-09-07 vocabulary sweep). AP `Paperframe-Setup`, default device name `paperframe`, mDNS `paperframe.local`. |
| Dropped from the status page | **Last-fetch time** and **battery-voltage detail** — both remain on `/debug`. |
| Button legend | **Keep current actions, relabel.** KEY1 → `Status`, KEY2 → `Refresh image`, KEY3 → `Pin image`. No behaviour change. |
| "Next image refresh" | **Relative.** `in 2h 15m` / `in 12m` / `< 1 min`, from `plannedSleepSecs()` — works before NTP. `Pinned` when held; `Paused until 07:00` in quiet hours. |
| Window frame | **Drawn.** Rounded-rect border + outer margin rendered on the panel, matching the mockup. |

## Grid

Panel is 1200 (w) × 1600 (h), portrait. All values below are constants in
the new renderer, not solved.

```
outer margin        60 px   (panel edge → window border, all four sides)
window              1080 × 1480, 2px border, corner radius 24
inner padding       48 px   (window border → content box)
content box         984 (w) × 1384 (h)
columns             12 × 60 px
gutters             11 × 24 px            (col + gutter unit = 84 px)
half-window split   between col 6 and col 7
zone rules          1 px, full content-box width
zone band padding   40 px above/below each rule
```

Vertical stack is top-down; zones take their natural height and the
legend pins to the bottom of the content box. There is generous slack —
the panel is 4:3 portrait and the content is not tall.

## Status page (Normal state)

```
┌ window ───────────────────────────────────────────────────────┐
│                                                               │
│  Paperframe                              ← title, Bold 24pt    │
│  Firmware build 1247                      ← chrome             │
│  ───────────────────────────────────────────────────────────  │
│  cols 1–6                     │ cols 7–12                      │
│  Next image refresh           │ Battery   [■■■□] 84%           │
│  in 2h 15m       ← Bold 24pt  │                                │
│                               │ Studio Wi-Fi        ← Bold     │
│                               │ .ıll  Strong signal            │
│  ───────────────────────────────────────────────────────────  │
│  cols 1–5      │ cols 6–12                                     │
│  ┌─────────┐   │ Open settings                    ← Bold       │
│  │ QR      │   │ Scan with your phone.                         │
│  │ code    │   │ Connect to the same Wi-Fi.                    │
│  └─────────┘   │ http://paperframe.local                      │
│               │ Or http://192.168.1.42                        │
│  ───────────────────────────────────────────────────────────  │
│  cols 1–6                     │ cols 7–12                      │
│  Device ID                    │ 13.3" Spectra 6                │
│  PF-A82F                      │ 1200 × 1600                    │
│                               │ ■ □ ▨ ▤ ▩ ▦   ← 6 colour swatch│
│  ───────────────────────────────────────────────────────────  │
│  cols 1–4        cols 5–8         cols 9–12                    │
│  (1) Status      (2) Refresh image   (3) Pin image            │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

- **Header** — `Paperframe` (title) then `Firmware build <FW_BUILD_NUMBER>`
  (chrome). The build number replaces the git hash that
  `status_content.h` currently prints; the hash is still on `/debug`.
- **Status band, left** — `Next image refresh` label + a large relative
  value. `nextPhotoValue()` is replaced by a relative formatter over
  `plannedSleepSecs()`:
  - held → `Pinned`
  - quiet hours active → `Paused until HH:MM` (wake time from
    `quiet_hours.h`)
  - otherwise → `in Xh Ym`, `in Xm`, or `< 1 min`
- **Status band, right** — battery icon + `84%` (percent only), then the
  SSID as a bold sub-heading with the signal-bar icon + strength label
  (`wifiStrengthBucket()` / `wifiStrengthLabel()`, unchanged).
- **Action zone** — QR (fixed scale, sized once to cols 1–5), then
  `Open settings` and two instruction lines. Two URL lines: the mDNS URL
  (`portalUrl()`, now `http://paperframe.local`) and `Or http://<ip>`
  from `prefs.getString("lastIp")` (already stashed by `net.cpp:140`).
  The IP line is dropped only if no IP is stored (never connected).
- **Device zone, left** — `Device ID` + `PF-<hex4>`, the low two bytes of
  `ESP.getEfuseMac()` upper-cased. Stable, survives rename/factory-reset.
- **Device zone, right** — panel spec (compile-time constants) and a
  six-cell colour swatch (black / white / yellow / red / blue / green).
  The swatch is the one place colour is drawn on this page; it doubles as
  a panel self-test. Everything else is black on white.
- **Legend** — three equal thirds, keycap glyph `1`/`2`/`3` +
  `Status` / `Refresh image` / `Pin image`.

## Onboarding and Error states

Same grid, same renderer, `ScreenState` selects the content:

| Zone | Onboarding | Error |
|------|-----------|-------|
| Header | `Paperframe` / `Firmware build <N>` | same |
| Status left | `Set me up` / `Not connected yet` | `Next image refresh` / relative (unchanged) |
| Status right | battery % only; Wi-Fi row → `Not connected` | battery %; Wi-Fi row → `Connection failed` + last SSID |
| Action | QR encodes `WIFI:S:Paperframe-Setup;;`; text: `Join "Paperframe-Setup"`, `then open http://192.168.4.1` | settings URL + IP; instruction line folds the failure reason |
| Device | ID + panel spec + swatch (self-test is useful on first boot) | same as Normal |
| Legend | `(1) Status` only; 2 and 3 blank | `(1) Status  (2) Retry  (3) Pin image` |

`showProvisioningScreen()` (net.cpp) and `showError()` (display.cpp) stay
thin wrappers that build a `ScreenContent` and call the shared renderer,
exactly as the 2026-08-18 design established — only the renderer changes.

## Board strip

### `platformio.ini`
- Delete `[env:ee03]`, `[env:ee04]`, `[env:ee05]`.
- Fold `[xiao_base]` into `[env:ee02]` (only one consumer left). Keep
  `[env:native]` untouched.
- Keep `board_upload.flash_size = 16MB`, `default_16MB.csv`,
  `extra_scripts = pre:tools/version.py`.
- Move the four `-D` brand strings here into `config.h` as plain
  constants (see below); the build flag that stays is
  `BOARD_SCREEN_COMBO=510` + `USE_XIAO_EPAPER_DISPLAY_BOARD_EE02` (needed
  by Seeed_GFX to pick the panel driver).

### `src/config.h`
- Remove every `#ifndef … #define` board fallback. Hard-code:
  `AP_NAME = "Paperframe-Setup"`, `DEFAULT_DEVICE_NAME = "paperframe"`,
  `BOARD_MODEL = "EE02"`, plus new `PANEL_W = 1200`, `PANEL_H = 1600`,
  `PANEL_DESC = "13.3\" Spectra 6"`.
- Button pins/masks unchanged.

### `src/display.h` / `src/display.cpp`
- `PANEL_NATIVE_LANDSCAPE` deleted; the panel is portrait-native.
  `setRotation(PANEL_NATIVE_LANDSCAPE ? 1 : 0)` → `setRotation(0)`.
- `PortraitScope` becomes a no-op guard (kept as a type so call sites
  don't churn) — the panel is always portrait; only `settings.rotation`
  for *photo* display still varies.
- Delete the `USE_MUTIGRAY_EPAPER` / `GRAY_LEVEL16` branch in
  `ditherToPanel` and the `initPanelColorMode()` gray path — keep only
  `USE_COLORFULL_EPAPER`.
- `main.cpp`: drop the `initPanelColorMode()` call + comment.

### `src/portal.cpp`
- `rotOptions()` simplifies: portrait-native, so the four rotation labels
  are a fixed table (`Portrait` / `Landscape` / `Portrait flipped` /
  `Landscape flipped`), no `PANEL_NATIVE_LANDSCAPE` branch.
- `%BOARD%` replacement stays (shows `BOARD_MODEL`).

### `src/net.cpp`
- `boardKey()` may stay (`"EE02"` → `"ee02"`) or become the literal
  `"ee02"` — the OTA manifest is still keyed by board and the published
  asset name must not change.

### `src/logic/layout_math.h` — mostly deleted
- Remove: `TITLE_SIZES` / `STAT_SIZES` / `CHROME_SIZES` ladders,
  `fitSize`, `ladderFor`, `clampToLadder`, `ContentConfig` /
  `configForLevel` / `REDUCTION_LEVELS`, `ContentShape` / `computeFit` /
  `shrinkOneStep`, `RoleSizes`, the QR-scale search.
- Keep (or move into the renderer): the grid constants above, a small
  `qrScaleForBox()` that returns the largest module scale fitting cols
  1–5 (now a one-liner, no height budget), and any pure geometry helper
  the renderer wants unit-tested.

### `src/logic/status_content.h` — simplified
- Drop `ContentConfig`, all reduction-level plumbing, `MAX_CONTENT_LINES`
  churn. `buildContent(ScreenState, ScreenContent, …)` returns a flat
  struct of resolved strings per zone (title, subtitle, nextRefresh,
  batteryPct, ssid, wifiLabel, settingsUrl, ipUrl, deviceId, panelDesc,
  legend[3]). No fitting — the renderer places each at its grid slot.

### `src/ui.cpp`
- Rewrite `drawFrameScreen()` as the fixed-grid renderer: draw the
  window frame, then each zone at its column span, with `drawFittedText`
  reduced to `drawText` (font is chosen per role by a fixed map, no
  fitting). `gatherLiveContent()` drops voltage + last-fetch, adds
  `lastIp` and the efuse-MAC device ID, and swaps `nextPhotoValue()` for
  the relative formatter.
- `fontFor()` keeps just the sizes the grid uses (title 56, sub-heading
  ~29, body/chrome ~22, small ~16).

### `.github/workflows/ci.yml`
- Drop `ee03`/`ee04`/`ee05` from the build matrix; keep `ee02` + native
  tests.

### Docs
- Delete `docs/panel-combos.md`.
- Trim `docs/hardware-checklist.md` to the EE02 board.
- `docs/versioning.md`: note the build number is now the on-device
  *display* string too (status page header), not only the OTA key.
- `README.md`: rebrand, EE02-only, update the feature list.
- Replace `docs/img/status-page-mockup.png` with the new design.

## Rebrand sweep

| Location | From | To |
|---|---|---|
| Status page title (`status_content.h`) | `Hello ePaper` | `Paperframe` |
| Portal `<title>` / `<h1>` (`portal_html.h`) | `Hello ePaper` | `Paperframe` |
| AP name (`config.h` / `platformio.ini`) | `EE02-Setup` | `Paperframe-Setup` |
| Default device name | `ee02` | `paperframe` |
| mDNS host (derived from name) | `ee02.local` | `paperframe.local` |
| `main.cpp` header comment, `README.md` | `Hello ePaper` / `EE02 e-paper photo frame` | `Paperframe` |

- **Migration**: only the *default* device name changes. Devices with a
  stored `settings.name` keep it (and their `<name>.local`); the new
  default applies on fresh boot / factory reset. Call this out in the PR.
- `BOARD_MODEL` (`"EE02"`) is the hardware model, shown in the device
  zone alongside the panel spec — it is not part of the rebrand.
- Body copy keeps **image / device / display** — "Paperframe" is the
  proper noun, "device" the common one, consistent with the mockup
  ("Device ID", "Open settings").

## Testing

### Native (`pio test -e native`)
- Rewrite `test/test_layout_math/` → grid geometry: column x-origins,
  half-window split, `qrScaleForBox()` result for the 1200-wide panel.
- Rewrite `test/test_status_content/` → `buildContent()` per state:
  relative-time formatting (held / quiet-hours / `in Xh Ym` / `< 1 min`
  / sub-minute), IP line present/absent, device-ID formatting from a
  known MAC, onboarding vs error field values.
- `test_battery`, `test_wifi_strength`, `test_quiet_hours`,
  `test_bmp_thumbnail`, `test_firmware_manifest`, `test_firmware_update`,
  `test_ota_trial`, `test_ring_log`, `test_stuck_error`,
  `test_url_template`, `test_validate` — unaffected, must still pass.

### Build
- `pio run -e ee02` clean. (No other firmware env exists after this.)

### Hardware (this project's bar — real EE02, per workflow preference)
- Flash; KEY1 shows the new status page. Verify against the mockup:
  window frame, six-colour swatch renders as six distinct colours, QR
  scans to `http://paperframe.local`, IP line matches `/debug`.
- Pull the image URL to a bad host → error screen renders on the same
  grid with the failure reason in the action line.
- Factory reset → onboarding screen shows `Paperframe-Setup`, phone
  joins the AP, QR onboarding works, `paperframe.local` resolves.
- Confirm relative time counts down across two wake cycles; set quiet
  hours and confirm `Paused until …`.
- OTA unchanged: `/ota/peek` still compares build numbers; a forced
  install still works.

## Out of scope

- Semver / changelog / git tags (explicitly rejected — build number
  stays the single ordering key).
- New button behaviours (`Settings`, `Sleep/wake` from the mockup) —
  legend is relabelled only.
- Any change to photo fetch/display, quiet-hours logic, or the OTA
  transport.
- `docs/img` mockup is illustrative; exact px spacing is tuned on
  hardware within the grid constants above.
