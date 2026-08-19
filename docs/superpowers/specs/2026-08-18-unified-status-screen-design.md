# Unified status/onboarding/error screen — design

Status: approved for planning. Source: `docs/superpowers/specs/2026-08-13-status-page-atlas.html`
(browser-based design exploration across all 21 EE02–EE05 panel variants), refined
through direct user review over several iterations before this port began.

## Motivation

Today the firmware draws three visually unrelated screens for what is really one
job — "tell the person standing there what's going on, and how to fix it if
something's wrong":

- `ui.cpp`'s `drawStatusScreen()` — KEY1 status view (tiles + QR + legend)
- `net.cpp`'s `showProvisioningScreen()` — first-boot / stale-credential Wi-Fi setup
- `display.cpp`'s `showError()` — fetch/connect failure

They share no code, no layout logic, and no visual language. `layout_math.h`
already has two font tiers (large/small) with fixed per-role sizes chosen by the
panel's short side — reasonable for the panels it was tuned against, but with no
mechanism to size *content* to what a specific panel can hold, and no path below
"small" for anything smaller than roughly 300px.

This change replaces all three with one shared renderer and one content-fitting
engine, ported from the atlas after that design was validated (0 layout overflow,
0 clipped text, guaranteed-scannable QR) across all 21 panel variants in a real
browser.

## Scope and what "done" means here

Only an EE02 is physically available to verify against. This change still covers
all four boards' code paths (EE03/EE04/EE05 included), but verification is split:

- **Pure logic** (size-ladder selection, the shrink pass, content-line building,
  QR scale math, the reduction cascade) lives in `src/logic/` with no Arduino
  dependency, and gets real unit tests run via `pio test -e native` — this is
  genuine verification, not a hardware substitute, but it does cover the exact
  code path every board runs.
- **Rendering** (the actual `epaper.*` drawing calls) is verified by a successful
  `pio run` build on all four board environments (`ee02`/`ee03`/`ee04`/`ee05`),
  catching macro/combo-specific compile errors — but a clean build is not proof
  a panel renders correctly.
- **Only EE02 gets a real runtime check** — flashed and confirmed on the actual
  connected device. EE03/EE04/EE05 are implemented and unit-tested, not
  hardware-verified. This will be stated plainly when the work is reported done,
  not implied away.

## Architecture

One shared renderer, three thin call sites — existing entry points and their
timing are preserved, only what they draw changes.

```
main.cpp
  ├─ KEY1 press ──────────► drawStatusScreen()  [ui.cpp, existing signature]
  ├─ connectWifi() portal ─► showProvisioningScreen()  [net.cpp, existing signature]
  └─ fetch/connect fail ──► showError(msg)  [display.cpp, existing signature]
                                    │
                                    ▼
                    drawFrameScreen(ScreenState, ScreenContent)  [ui.cpp, new]
```

`drawFrameScreen()` implements the atlas's four-zone grid (header → status →
action → legend) and takes a `ScreenState` enum (`Normal`/`Onboarding`/`Error`)
plus a `ScreenContent` struct carrying whatever that state needs (battery %,
Wi-Fi info, QR payload, error message, etc.). Each of the three existing
functions becomes a thin wrapper: gather its own data (from `prefs`, `WiFi.*`,
its error string, ...), build a `ScreenContent`, call `drawFrameScreen()`.

This keeps each screen's *control flow* exactly where it already correctly is —
provisioning still draws before Wi-Fi connects (so instructions are visible
before the portal needs to be reachable), error still only draws when
`interactive` — only the drawing implementation becomes shared.

## Content model

Four zones, ported directly from the atlas:

1. **Header** — `"Hello ePaper (ver.<hash6>)"` then `"XIAO EE0X"`, one line each.
   Reuses `FW_GIT_HASH` (already shipped). Version drops from the first line
   under space pressure; the board line is always present.
2. **Status** — battery / Wi-Fi / next-fetch, each an icon + compact value
   (`"98%"` not `"battery 98%"`), with an optional `" · "`-joined detail suffix
   (exact voltage, SSID, last-fetch time) shown when there's room and dropped
   first under pressure.
3. **Action** — QR + a one-line instruction + the URL/AP name as text. The QR
   always encodes what the instruction line says in words: the settings URL
   normally, `WIFI:S:<AP_NAME>;;` during onboarding.
4. **Legend** — three `KEY1`/`KEY2`/`KEY3` rows, each a small keycap glyph + a
   short action phrase (`"Configuration"`, `"Refresh"`, `"Pin"` for Normal;
   `"Retry"` instead of `"Refresh"` for Error; `"Show Screen"` / blanked for
   Onboarding).

Per-state field values:

| Field | Normal | Onboarding | Error |
|---|---|---|---|
| Battery | real % + voltage | real % (no detail) | real % + voltage |
| Wi-Fi | strength label + SSID | `"--"` | `"failed"` + SSID |
| Next-fetch | next time + last-fetch | `"--"` | next time + last-fetch |
| Action | settings URL | join-hotspot QR | settings URL, failure reason folded into the instruction line |
| Legend | Configuration / Refresh / Pin | Show Screen / — / — | Configuration / Retry / Pin |

## Sizing engine (`src/logic/layout_math.h`)

Replaces the current two-tier `computeLayout()` with per-role, per-panel fitting:

- **Font ladders**, largest first, every rung a real compiled-in asset:
  - title: `FreeSansBold24pt7b`(56) → `18pt7b`(42) → `12pt7b`(29) → `9pt7b`(22) → classic Font2(16) → classic Font1(8)
  - stat: `FreeSans18pt7b`(42) → `12pt7b`(29) → `9pt7b`(22) → classic Font2(16) → classic Font1(8)
  - chrome: `FreeSans12pt7b`(29) → classic Font2(16) → classic Font1(8)
- **`fitSize()`**: for a role, walk its ladder and return the largest size at
  which every line sharing that role fits its own available width — measured
  with `epaper.textWidth()` against the real font object, not estimated.
- **Icon-column reserve**: stat and legend lines sit behind a shared icon/keycap
  column; their available width is the row width *minus* that column, not the
  full row (the atlas's "strong" → "stro" bug came from missing exactly this).
- **Height-aware shrink pass**: `fitSize()` is width-only, so a short string
  like `"98%"` can pick an oversized rung that doesn't fit vertically once
  stacked. After the width-only pick, shrink one role at a time — **chrome
  first, then stat, title last** — until the stack fits. Fixed priority, not
  "whichever role is currently biggest": the atlas found that heuristic let the
  board line render bigger than the title above it.
- **QR sizing**: same scale-selection loop as today's `computeLayout()` (largest
  scale that fits the remaining vertical budget), now also width-checked
  against the row (today's code doesn't check width at all — the atlas found
  this breaks on narrow panels: the QR sized off leftover height alone and
  rendered wider than the panel). A **2px/module floor** is enforced as a
  guaranteed-scannable minimum — a small QR is worse than none, since the URL
  is already shown as text. Below the floor, the reduction cascade keeps
  cutting other content or, as an absolute last resort, drops the QR.
- **Content reduction cascade**: unchanged priority order from the atlas —
  legend detail → legend dropped entirely → stat detail suffix → caption
  detail → URL text → "next" row → version number → caption dropped → scan
  instruction → QR (last resort). Applied only when the fitted sizes still
  don't stack within the panel's height.

## Icons (`src/icons.h`, new)

Hand-picked from [pixelarticons](https://github.com/halfmage/pixelarticons)
(MIT licensed): `battery-full`/`battery-medium`/`battery-low` (selected by real
percent), `wifi`, `refresh`. Rasterized once to one fixed bitmap size (not one
per font tier — icons are small enough that a fixed size next to scaling text
isn't expected to look wrong), stored as 1-bit `PROGMEM` byte arrays, drawn with
`epaper.drawBitmap()`.

**Cleanup**: `drawBatteryIcon()`, `drawWifiIcon()`, and `drawNextPhotoIcon()` in
`display.cpp`/`display.h` have no callers once `ui.cpp`'s old
`drawStatusScreen()` is replaced — removed rather than left as dead code.
`drawQrCode()` is unaffected (already correct, reused as-is with a
firmware-side scale-selection update to match the new algorithm above).

## Orientation

The new screen always renders portrait, regardless of `settings.rotation` — a
deliberate product decision (not just a demo artifact): call whatever rotation
value makes `epaper.width() < epaper.height()` before drawing this screen, then
restore the configured rotation (`applyOrientation()`) afterward so photo
display is unaffected. `PANEL_NATIVE_LANDSCAPE` (already in `display.h`) is
enough to determine which rotation value that is per board.

## Files touched

- `src/logic/layout_math.h` — replace `computeLayout()`; new pure-logic helpers
  (`fitSize`, shrink pass, QR scale, reduction cascade)
- `src/logic/status_content.h` (new) — pure-logic content-line building per
  state (mirrors the atlas's `buildLines`/`statLines`/`legendLines`)
- `src/icons.h` (new) — icon bitmap data
- `src/display.h` / `display.cpp` — remove the three vector icon functions; add
  a bitmap-icon draw helper; QR scale selection updated to the new algorithm
- `src/ui.cpp` — new `drawFrameScreen()`; `drawStatusScreen()` becomes a wrapper
- `src/net.cpp` — `showProvisioningScreen()` becomes a wrapper
- `src/display.cpp` — `showError()` becomes a wrapper
- `test/test_layout_math/` (existing, extended) and new `test/test_*` targets
  for the content-line building and QR scale logic

## Open questions carried over from the atlas (not yet resolved)

- 2px/module as the QR scannability floor is a starting assumption, not
  measured against a real phone camera at each panel's actual DPI.
- EE03 is driven as 16-level gray in the firmware; the product description
  calls it monochrome. Worth confirming against the panel's real spec.
- Onboarding's legend wording (`"KEY1 Show Screen"`, KEY2/KEY3 blanked) is a
  first guess with no settled real behavior for buttons before Wi-Fi is set up.
