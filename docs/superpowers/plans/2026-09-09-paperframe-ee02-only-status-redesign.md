# Paperframe: EE02-only + fixed-grid status page — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

Design: `docs/superpowers/specs/2026-09-09-paperframe-ee02-only-status-redesign-design.md`

**Goal:** Make the firmware EE02-only (delete `ee03`/`ee04`/`ee05` and
every board conditional), replace the multi-panel content-fitting engine
with a fixed 12-column grid renderer for the status / onboarding / error
screen matching the 2026-09-09 mockup, show the build number instead of
the git hash on that screen, and rebrand "Hello ePaper" → "Paperframe".

**Architecture:** The panel becomes compile-time constants (`PANEL_W
1200`, `PANEL_H 1600`, portrait-native, Spectra-6 colour). `layout_math.h`
loses its font ladders, `fitSize`, the shrink pass, `ContentConfig` /
reduction cascade, and `computeFit`; what remains is grid constants and a
one-line `qrScaleForBox()`. `status_content.h` loses the reduction
plumbing and exposes `buildStatusData(ScreenState, const ScreenContent&,
uint32_t buildNumber, const char* apName) -> StatusData`, a flat struct of
resolved per-zone strings. `ui.cpp`'s `drawFrameScreen()` is rewritten to
place each zone at a fixed column span, draw the window frame, and render
the six-colour swatch. The three call sites (`drawStatusScreen`,
`showProvisioningScreen`, `showError`) keep their signatures. OTA,
photo fetch/display, and quiet-hours logic are untouched.

**Tech Stack:** Arduino/ESP32 (PlatformIO, `framework = arduino`),
Seeed_GFX `EPaper`, Unity for native host tests in `src/logic/`. No new
font or library assets — every font size the grid uses is already
compiled in (see the header comment in `src/ui.cpp`).

## Global Constraints

- **One firmware env.** After Task 1 the only build is `pio run -e ee02`;
  `pio test -e native` is the other gate. Every task ends on both green
  (native only where logic changed).
- **No board conditionals left.** No `#ifdef USE_MUTIGRAY_EPAPER`, no
  `PANEL_NATIVE_LANDSCAPE`, no `BOARD_MODEL_STR` fallback `#ifndef`. The
  Seeed_GFX driver selector (`BOARD_SCREEN_COMBO=510`,
  `USE_XIAO_EPAPER_DISPLAY_BOARD_EE02`) stays — it is a library input,
  not our conditional.
- **OTA is not touched.** Manifest board key stays `ee02`; published
  asset names (`firmware-ee02.bin`, `md5_ee02`) stay. `FW_BUILD_NUMBER`
  stays the ordering key. Only the *on-device status-screen* version
  string changes (hash → build number); `/log` and `/debug` keep the
  hash.
- **Device-name rebrand is default-only.** `DEFAULT_DEVICE_NAME` changes
  `ee02` → `paperframe`; a device with a stored `settings.name` keeps it
  and its `<name>.local`. New/factory-reset devices get `paperframe.local`.
- **Vocabulary:** product proper noun is **Paperframe**; common nouns in
  copy stay **image / device / display** (2026-09-07 sweep). Not `docs/`,
  not identifiers, not `src/logic` comments.
- **Grid constants** (renderer, not solved): outer margin 60, window
  1080×1480 / 2px border / radius 24, inner padding 48, content box
  984×1384, 12 cols × 60px, 11 gutters × 24px, half-window split between
  col 6 and 7, zone rules 1px with 40px band padding.

---

### Task 1: Strip `ee03`/`ee04`/`ee05` from the build + CI, hard-code EE02 constants, rebrand AP/device name

**Files:**
- Modify: `platformio.ini`, `src/config.h`, `.github/workflows/ci.yml`

**Interfaces:**
- Produces: single `[env:ee02]` (with `[xiao_base]` folded in) + `[env:native]`.
- Produces: `config.h` constants `PANEL_W = 1200`, `PANEL_H = 1600`,
  `PANEL_DESC = "13.3\" Spectra 6"`, and unconditional `AP_NAME =
  "Paperframe-Setup"`, `DEFAULT_DEVICE_NAME = "paperframe"`,
  `BOARD_MODEL = "EE02"`, `DEFAULT_ROTATION = 0`. Consumed by Tasks 2–4.

- [ ] **Step 1: `platformio.ini`**

Delete `[env:ee03]`, `[env:ee04]`, `[env:ee05]`. Fold `[xiao_base]` into
`[env:ee02]` (keep `extends`-provided values inline: platform, board,
framework, `monitor_speed`, `extra_scripts = pre:tools/version.py`,
`board_upload.flash_size = 16MB`, `board_build.partitions =
default_16MB.csv`, `lib_deps`). Keep `[env:native]` exactly as is. Keep
the `[env:ee02]` `build_flags`:
```
-DBOARD_SCREEN_COMBO=510
-DUSE_XIAO_EPAPER_DISPLAY_BOARD_EE02
```
Drop `-DAP_NAME_STR`, `-DDEFAULT_DEVICE_NAME_STR`, `-DBOARD_MODEL_STR`
(now literals in `config.h`).

- [ ] **Step 2: `src/config.h`**

Remove the three `#ifndef … #define … #endif` blocks
(`DEFAULT_DEVICE_NAME_STR`, `DEFAULT_ROTATION_VALUE`, `AP_NAME_STR`) and
the `BOARD_MODEL_STR` block. Replace with:
```cpp
inline const char *DEFAULT_DEVICE_NAME = "paperframe";
constexpr uint8_t  DEFAULT_ROTATION    = 0; // portrait; panel is portrait-native
inline const char *AP_NAME             = "Paperframe-Setup";
inline const char *BOARD_MODEL         = "EE02"; // hardware model, not the brand

// Panel: XIAO EE02, 13.3" Spectra-6 colour, portrait-native.
constexpr int      PANEL_W    = 1200;
constexpr int      PANEL_H    = 1600;
inline const char *PANEL_DESC = "13.3\" Spectra 6";
```
Update the button comment: `KEY1..KEY3` — pins on the XIAO EE02 board
(drop "identical across EE02/EE03/EE04/EE05").

- [ ] **Step 3: `.github/workflows/ci.yml`**

`build` job: `matrix.env: [ee02]`. `publish` job: replace
`for e in ee02 ee03 ee04 ee05` with a single `e=ee02` (no loop), and the
`download-artifact` path comment. `gh release create` line unchanged
except it now globs only the ee02 asset + manifest.

- [ ] **Step 4: Verify**

Run: `pio run -e ee02` → expect clean.
Run: `pio test -e native` → expect all pass (no logic touched yet).
Run: `grep -rn "ee03\|ee04\|ee05\|EE03\|EE04\|EE05" platformio.ini .github/` → expect no hits.

---

### Task 2: Remove the rotation / gray-panel / combo-table board branches

**Files:**
- Modify: `src/display.h`, `src/display.cpp`, `src/main.cpp`, `src/portal.cpp`

**Interfaces:**
- Produces: `PortraitScope` kept as a no-op RAII type (call sites in
  `ui.cpp`/`net.cpp`/`display.cpp` keep compiling).
- Removes: `PANEL_NATIVE_LANDSCAPE`, `initPanelColorMode()`, the
  `USE_MUTIGRAY_EPAPER` dither branch.

- [ ] **Step 1: `src/display.h`**

Delete the `PANEL_NATIVE_LANDSCAPE` constant + its comment block. Keep
`PortraitScope` declared. Delete the gray-mode helper declaration
(`initPanelColorMode` and any `initGrayMode`-related prototype near
line 64) and its comment.

- [ ] **Step 2: `src/display.cpp`**

- `PortraitScope::PortraitScope()` / `~PortraitScope()` → empty bodies
  (leave a one-line comment: panel is portrait-native, nothing to swap;
  photo orientation is handled by `applyOrientation()`).
- Line ~19: `epaper.setRotation(PANEL_NATIVE_LANDSCAPE ? 1 : 0);` →
  `epaper.setRotation(0);`.
- Delete `void initPanelColorMode() { … }` entirely.
- In `ditherToPanel` (line ~65): keep the `USE_COLORFULL_EPAPER` body,
  delete the `#elif defined(USE_MUTIGRAY_EPAPER) …` branch and the
  `#else` plain-mono branch and the `#if/#elif/#else/#endif` scaffolding
  — the colourful path is now unconditional.

- [ ] **Step 3: `src/main.cpp`**

Delete line ~279 `initPanelColorMode();` and its trailing comment.

- [ ] **Step 4: `src/portal.cpp`**

`rotOptions()` (line ~78): drop the `PANEL_NATIVE_LANDSCAPE` branch;
the four labels are a fixed table — index 0/2 = `Portrait` /
`Portrait flipped`, index 1/3 = `Landscape` / `Landscape flipped`.
Remove the now-stale comment about EE03/EE04/EE05 native landscape.

- [ ] **Step 5: Verify**

Run: `pio run -e ee02` → clean.
Run: `grep -rn "PANEL_NATIVE_LANDSCAPE\|initPanelColorMode\|USE_MUTIGRAY\|GRAY_LEVEL16" src/` → no hits.

---

### Task 3: Pure logic — relative next-refresh, device ID, and `buildStatusData()` with native tests

**Files:**
- Modify: `src/logic/status_content.h`, `src/logic/layout_math.h`
- Rewrite: `test/test_status_content/main.cpp`, `test/test_layout_math/main.cpp`

**Interfaces:**
- Produces: `struct StatusData` — flat resolved strings:
  `title`, `versionLine` (`"Firmware build 1247"`), `nextLabel`,
  `nextValue`, `batteryPct`, `wifiHeading`, `wifiLabel`, `wifiBase`
  (icon selector), `actionHeading`, `actionLine1`, `actionLine2`,
  `urlPrimary`, `urlSecondary`, `deviceId`, `panelDesc`, `resX`, `resY`,
  `legend[3]`, `qrPayload`, `showBatteryDetail=false`.
- Produces: `formatNextRefresh(bool held, bool clockSane, bool inQuiet,
  int quietEndHour, uint32_t adjustedSleepSecs) -> small string`:
  held → `"Pinned"`; inQuiet && clockSane → `"Paused until HH:00"`;
  else → `"in Xh Ym"` / `"in Xm"` / `"< 1 min"` from `adjustedSleepSecs`.
- Produces: `deviceIdFromMac(uint64_t mac) -> "PF-XXXX"` (upper-hex of
  `mac & 0xFFFF`).
- Produces: `qrScaleForBox(int boxW, int boxH) -> int` in `layout_math.h`
  (largest module scale with a 4-module quiet zone fitting the smaller
  of `boxW`/`boxH`, floor `QR_MIN_SCALE`).
- Consumed by Task 4 (`ui.cpp`, wrappers).

- [ ] **Step 1: Write the new tests (expect compile failure)**

`test/test_status_content/main.cpp` — replace the body with cases for:
- `formatNextRefresh`: held → `"Pinned"`; `!clockSane` with 3600s →
  `"in 1h 0m"`; 5400s → `"in 1h 30m"`; 600s → `"in 10m"`; 30s →
  `"< 1 min"`; `inQuiet` with `quietEndHour=7` → `"Paused until 07:00"`.
- `deviceIdFromMac(0x0000A82Full)` → `"PF-A82F"`;
  `deviceIdFromMac(0x...00FFull)` → `"PF-00FF"`.
- `buildStatusData(ScreenState::Normal, …)`: `versionLine ==
  "Firmware build 1247"`; `urlSecondary` empty when `content.lastIp`
  empty, `"Or http://192.168.1.42"` when set; `legend` ==
  `{"Status","Refresh image","Pin image"}`; `qrPayload ==
  content.settingsUrl`.
- `buildStatusData(ScreenState::Onboarding, …)`: `qrPayload ==
  "WIFI:S:Paperframe-Setup;;"`; `legend[1] == "" && legend[2] == ""`;
  `wifiHeading == "Not connected"`.
- `buildStatusData(ScreenState::Error, …)`: `legend[1] == "Retry"`;
  `wifiHeading == "Connection failed"`; `actionLine1` contains the
  error message.

`test/test_layout_math/main.cpp` — replace with:
- `qrScaleForBox(300, 900)` returns a value ≥ `QR_MIN_SCALE` and
  `scale * (33 + 8) <= 300`.
- a column-origin helper check (see Step 3) for cols 1, 6, 7, 12.

Run: `pio test -e native -f test_status_content -f test_layout_math` →
expect FAIL to compile.

- [ ] **Step 2: Reduce `src/logic/layout_math.h`**

Delete: `TITLE_SIZES`/`STAT_SIZES`/`CHROME_SIZES` (+ `_N`), `SizeRole`,
`ladderFor`, `TextWidthFn`, `FitItem`, `fitSize`, `RoleSizes`,
`shrinkOneStep`, `ICON_COL_RESERVE_FACTOR`, `LINE_HEIGHT`,
`GRID_OUTER_MARGIN`/`GRID_SECTION_GAP`/`GRID_QR_GAP`, `ContentShape`,
`FitResult`, `computeFit`, `LegendMode`/`CaptionMode`, `ContentConfig`,
`REDUCTION_LEVELS`, `configForLevel`.

Keep / add:
```cpp
constexpr int QR_MODULES = 33;
constexpr int QR_QUIET_MODULES = 4;
constexpr int QR_MIN_SCALE = 2;

// 12-col grid inside the drawn window. See the design spec's Grid table.
constexpr int GRID_OUTER_MARGIN = 60;
constexpr int GRID_WIN_W = 1080, GRID_WIN_H = 1480;
constexpr int GRID_WIN_BORDER = 2, GRID_WIN_RADIUS = 24;
constexpr int GRID_PAD = 48;
constexpr int GRID_COLS = 12, GRID_COL_W = 60, GRID_GUTTER = 24;
constexpr int GRID_CONTENT_W = GRID_COLS * GRID_COL_W + (GRID_COLS - 1) * GRID_GUTTER; // 984
constexpr int GRID_ZONE_PAD = 40;

// x of column `c` (1-based) relative to the content box left edge.
inline int gridColX(int c) { return (c - 1) * (GRID_COL_W + GRID_GUTTER); }
// width of a span from column `from` to column `to` inclusive.
inline int gridSpanW(int from, int to) {
    return (to - from + 1) * GRID_COL_W + (to - from) * GRID_GUTTER;
}

inline int qrScaleForBox(int boxW, int boxH) {
    const int box = boxW < boxH ? boxW : boxH;
    const int total = QR_MODULES + 2 * QR_QUIET_MODULES; // 41
    int s = box / total;
    return s < QR_MIN_SCALE ? QR_MIN_SCALE : s;
}
```

- [ ] **Step 3: Rewrite `src/logic/status_content.h`**

Remove `LineKind`/`ContentLine`/`setLine`/`buildLines`/`FitLine`/
`ScreenFit`/`fitScreen`/`hasIconColumn`/`clampToLadder`. Keep
`ScreenState`. Extend `ScreenContent` with `char lastIp[24];` and
`char deviceId[12];` (renderer fills `deviceId` from the efuse MAC before
calling). Add:
```cpp
struct StatusData { /* fields listed in Interfaces */ };

inline void formatNextRefresh(char *out, size_t cap, bool held,
        bool clockSane, bool inQuiet, int quietEndHour,
        uint32_t adjustedSleepSecs) { … }

inline void deviceIdFromMac(char *out, size_t cap, uint64_t mac) {
    snprintf(out, cap, "PF-%04X", (unsigned)(mac & 0xFFFF));
}

inline StatusData buildStatusData(ScreenState state,
        const ScreenContent &c, uint32_t buildNumber, const char *apName) { … }
```
`buildStatusData` fills the per-state table from the design spec
(Normal / Onboarding / Error columns). `versionLine` =
`"Firmware build %u"`. `legend` per state. `qrPayload` = `settingsUrl`
normally, `"WIFI:S:<apName>;;"` for onboarding. `urlSecondary` =
`c.lastIp[0] ? "Or http://<ip>" : ""`.

- [ ] **Step 4: Implement to green**

Run: `pio test -e native` → expect all pass (including the untouched
`test_battery` / `test_quiet_hours` / `test_wifi_strength` / etc.).
Run: `pio run -e ee02` → **expected FAIL** (`ui.cpp` still calls the
deleted `fitScreen`). This is fixed in Task 4; note it and continue.

---

### Task 4: Fixed-grid renderer in `ui.cpp` + wrapper updates + dead-code removal

**Files:**
- Modify: `src/ui.cpp`, `src/ui.h`, `src/net.cpp`, `src/display.cpp`, `src/icons.h`

**Interfaces:**
- Produces: `drawFrameScreen(ScreenState, const ScreenContent&)`
  rewritten to the fixed grid. `drawStatusScreen` / `showProvisioningScreen`
  / `showError` signatures unchanged.
- Removes: `fontFor`/`applyFont`/`realTextWidth`/`drawFittedText` fitting
  machinery (replaced by a fixed role→font map + `drawText`), `nextPhotoValue`.

- [ ] **Step 1: `gatherLiveContent()` (`ui.cpp`)**

- Drop `batteryVoltage` and `lastFetch` population.
- Add `strncpy(content.lastIp, prefs.getString("lastIp","").c_str(), …)`.
- Add `deviceIdFromMac(content.deviceId, sizeof(content.deviceId),
  ESP.getEfuseMac())`.
- Replace the `nextPhotoValue()` call: compute
  `now = time(nullptr)`, `clockSane = now > CLOCK_SANE_EPOCH`,
  `inQuiet = settings.quietEnabled && clockSane &&
  inQuietWindow(secondsOfLocalDay(now), settings.quietStartHour,
  settings.quietEndHour)` (expose `secondsOfLocalDay` from `power.*` or
  inline it), then `formatNextRefresh(content.nextBase, …, held,
  clockSane, inQuiet, settings.quietEndHour, plannedSleepSecs())`.
  Keep storing the result in `content.nextBase`.
- Delete `nextPhotoValue()` and the now-unused `recordFetchMetadata`
  `lastEpoch` read only if nothing else needs it (it still stamps
  `lastEpoch` for `/debug` — leave the write).

- [ ] **Step 2: Rewrite `drawFrameScreen()` (`ui.cpp`)**

```
PortraitScope is a no-op now but keep the call sites.
fill white; text black on white.
data = buildStatusData(state, content, FW_BUILD_NUMBER, AP_NAME);

winX = GRID_OUTER_MARGIN; winY = (PANEL_H - GRID_WIN_H) / 2;
draw the rounded window: two concentric drawRoundRect for a 2px border.
ox = winX + GRID_PAD;  oy = winY + GRID_PAD;   // content-box origin
colX(c)  -> ox + gridColX(c)
rule(y)  -> drawFastHLine(ox, y, GRID_CONTENT_W)

Zones, top-down, advancing a running `y`:
  Header:   data.title      @ colX(1), size TITLE (FreeSansBold24pt)
            data.versionLine @ colX(1), size SMALL
  rule
  Status:   left  cols 1-6:  data.nextLabel (SMALL) + data.nextValue (BIG = FreeSansBold18pt)
            right cols 7-12: battery icon (by pct) + "NN%" (BODY);
                             data.wifiHeading (SUBHEAD bold);
                             wifi icon (by data.wifiBase) + data.wifiLabel (BODY)
  rule
  Action:   left  cols 1-5:  QR, scale = qrScaleForBox(gridSpanW(1,5), <=that>)
            right cols 6-12: data.actionHeading (SUBHEAD) + actionLine1 + actionLine2 (BODY)
                             data.urlPrimary (BODY) + data.urlSecondary (SMALL, if non-empty)
  rule
  Device:   left  cols 1-6:  "Device ID" (SMALL) + data.deviceId (BODY)
            right cols 7-12: data.panelDesc (BODY) + "1200 x 1600" (BODY)
                             + 6-cell colour swatch (see Step 3)
  rule
  Legend:   thirds cols 1-4 / 5-8 / 9-12: keycap "1"/"2"/"3" + data.legend[i]
            (skip a third whose legend string is empty)
```
Fixed role→font map (no fitting): `TITLE`=FreeSansBold24pt7b,
`BIG`=FreeSansBold18pt7b, `SUBHEAD`=FreeSansBold12pt7b, `BODY`=FreeSans9pt7b,
`SMALL`=classic Font2. Reuse `drawStatusIcon` / `drawKeycap` /
`drawQrCode` as-is.

- [ ] **Step 3: Colour swatch (`ui.cpp`, small static helper)**

`drawColorSwatch(x, y, cellW, cellH)` — six adjacent cells:
`TFT_BLACK, TFT_WHITE (1px black outline), TFT_YELLOW, TFT_RED,
TFT_BLUE, TFT_GREEN`. This is the only colour drawn on the screen.

- [ ] **Step 4: Trim `icons.h`**

Remove `ICON_NEXT` and its comment (no icon on the relative-time value).
Keep the battery ×4 and wifi ×4 sets. Leave `ICON_W`/`ICON_H`.

- [ ] **Step 5: Wrapper `ScreenContent` builders**

- `net.cpp` `showProvisioningScreen()`: still sets `batteryPct`; drop
  `nextBase`/`wifiBase` "--" writes (the builder supplies onboarding
  text). Leave `snapshotPrevious()` + `epaper.update()`.
- `display.cpp` `showError()`: keeps `errorMsg`; drop the manual
  `wifiBase = "failed"` (builder handles it). Leave the rest.

- [ ] **Step 6: Delete dead code**

Remove `fontFor` / `applyFont` / `realTextWidth` / `drawFittedText` /
`FontChoice` / `SizeRole` uses from `ui.cpp`, plus any now-unused include.
`grep -rn "fitScreen\|buildLines\|ScreenFit\|SizeRole\|fitSize" src/` →
expect no hits.

- [ ] **Step 7: Verify**

Run: `pio run -e ee02` → clean.
Run: `pio test -e native` → all pass.

---

### Task 5: Rebrand sweep (display + portal + docs)

**Files:**
- Modify: `src/logic/status_content.h` (title literal), `src/portal_html.h`,
  `src/portal.cpp`, `src/main.cpp` (header comment), `README.md`,
  `docs/versioning.md`, `docs/hardware-checklist.md`
- Delete: `docs/panel-combos.md`
- Replace: `docs/img/status-page-mockup.png`

- [ ] **Step 1: Firmware user-facing strings**

- `buildStatusData` `title` literal → `"Paperframe"`.
- `portal_html.h`: `<title>`, `<h1>`, any "Hello ePaper" / "photo frame"
  → "Paperframe" (proper noun) / "image"/"device" (common nouns).
- `portal.cpp`: `configModeCallback` log line + `%BOARD%` context —
  leave `%BOARD%` = `BOARD_MODEL` ("EE02"); reword any "Hello ePaper".
- `main.cpp` line 1 comment: `// Paperframe — EE02 e-paper photo frame…`
  → `// Paperframe — EE02 e-ink image display firmware. See README.md.`
- `showError` default strings in `main.cpp` (lines ~64/112/125/285/340):
  keep wording, they already say "Wi-Fi"; change "KEY1" → "Status
  button" only if it reads better — optional.

- [ ] **Step 2: Docs**

- `README.md`: retitle to Paperframe; drop the four-env / EE0x table;
  describe the single EE02 board + 13.3" Spectra-6 panel; keep the
  zero-cloud pitch.
- `docs/versioning.md`: add a sentence — the build number is now also the
  on-device status-screen version string (header: `Firmware build <N>`);
  the git hash remains on `/log` and `/debug`.
- `docs/hardware-checklist.md`: cut to the EE02 board.
- `git rm docs/panel-combos.md`.

- [ ] **Step 3: Mockup image**

Replace `docs/img/status-page-mockup.png` with the 2026-09-09 design
render (Paperframe branding, relative time, IP line, swatch).

- [ ] **Step 4: Verify**

Run: `pio run -e ee02` && `pio test -e native` → green.
Run: `grep -rn "Hello ePaper\|EE03\|EE04\|EE05" src/ docs/ README.md` →
expect only intentional history mentions in dated `docs/superpowers/`
specs/plans (leave those; they are records).

---

### Task 6: Hardware verification on the real EE02

Not automatable — this project's bar (workflow preference: real-hardware
verification before "done"). Flash `pio run -e ee02 -t upload`, then:

- [ ] KEY1 → new status page renders: window frame visible, header
  `Paperframe` / `Firmware build <N>`, all four zone rules aligned to the
  content box, legend thirds aligned to the grid.
- [ ] Six-colour swatch shows six visibly distinct colours (black,
  white, yellow, red, blue, green) — confirms the panel + palette.
- [ ] QR scans with a phone to `http://paperframe.local`; the primary
  URL line matches; `Or http://<ip>` matches `/debug`'s reported IP.
- [ ] `Device ID` shows `PF-XXXX` and matches the low 16 bits of the MAC
  on `/debug`.
- [ ] Relative time: note the value, deep-sleep one cycle, wake, confirm
  it counted down. Enable quiet hours spanning the next wake → confirm
  `Paused until HH:00`. Toggle KEY3 (pin) → confirm `Pinned`.
- [ ] Break the image URL (bad host) → error screen renders on the same
  grid with the failure reason in the action line; legend shows
  `Status / Retry / Pin image`.
- [ ] Factory reset → onboarding screen shows `Join "Paperframe-Setup"`;
  phone joins the AP; onboarding QR works; after setup `paperframe.local`
  resolves.
- [ ] `/debug` still shows the git hash; OTA `/ota/peek` still reports a
  build-number comparison; a forced `/ota/install` still starts.

Report EE02-verified explicitly; there are no other boards to caveat.

---

## Out of scope

- Semver / changelog / git tags (build number stays the sole ordering key).
- New button behaviours — legend is relabelled only.
- Photo fetch/display, quiet-hours math, OTA transport.
- Exact pixel spacing within the grid constants — tuned on hardware in Task 6.
