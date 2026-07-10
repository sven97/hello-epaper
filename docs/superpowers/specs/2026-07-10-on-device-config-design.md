# On-device configuration portal + open-source prep — design

**Date:** 2026-07-10
**Status:** approved by Sven (conversation review, both halves)

## Goal

1. Make every runtime behavior the user cares about (refresh interval, image
   source, quiet hours, timezone, device name, orientation) configurable
   **on the device itself** — no external service, ever. A long-press
   gesture enters a config mode where the ESP32 serves a settings web page
   on the local network.
2. Prepare the repo for open-sourcing (license, README, CI, dependency
   pinning, tests).

## Context and rejected alternatives

- **Hosted portal (TRMNL/BYOS model), rejected:** a self-hosted Node
  portal with device registration and server-side rendering was designed
  first, then dropped. A deep-sleeping device can't be pushed to anyway
  (servers only apply changes when the device polls), so the only real
  loss is couch-config vs walk-up-config. For a photo frame configured
  twice a year, self-contained wins on every stated goal: firmware stays
  clean, setup is "press a button," nothing to host. If a server ever
  becomes desirable, the image-URL setting already is the integration
  point.
- **Extending WiFiManager's portal with custom params, rejected:** generic
  text boxes, awkward validation, settings coupled to a third-party
  library's UI and lifecycle. WiFiManager keeps exactly one job:
  first-time Wi-Fi credential provisioning.

## Architecture

Repo stays firmware-only. Two new modules, following the existing layout:

### `src/settings.*`

- `Settings` struct + `load()`/`save()` over the existing `Preferences`
  namespace (`frame`).
- Defaults exactly match today's behavior. `config.h` keeps true constants
  (pins, panel dims) plus the setting *defaults*; all tunables move behind
  the settings module.
- NVS read failure ⇒ compiled defaults (indistinguishable from first boot).

### `src/portal.*` (config mode)

- Joins saved Wi-Fi (STA), starts mDNS (`<name>.local`) and the ESP32
  built-in `WebServer` on port 80.
- `GET /` — single embedded HTML settings page, pre-filled with current
  values. `POST /save` — server-side validation; invalid fields re-render
  the form with an error, nothing partially saved.
- Panel draws a config screen: URL, numeric IP, QR code (ricmoo/QRCode,
  MIT). mDNS failure is non-fatal — the IP is always shown.
- No saved Wi-Fi ⇒ run the normal WiFiManager provisioning first, then
  continue into the portal.

### Entering / leaving config mode

- **Long-press KEY1 (~1.5 s)** enters config mode; short press remains
  "toggle info page." Works from deep sleep (EXT1 wake, then check the pin
  is still held after boot) and in dev mode (the polled `pressed()` helper
  learns hold duration). No conflict with the KEY2-at-power-on
  forget-Wi-Fi gesture.
- **Exit:** a valid Save on the web page (the response page confirms
  "saved — device is applying settings"), another long-press, or a
  **10-minute inactivity timeout** (battery guard). A valid save always
  exits; to adjust again, re-enter config mode. On exit the device runs a
  normal fetch cycle so changes take effect visibly, then sleeps.

## Settings surface (v1)

| Setting | Form control | Validation | Default |
|---|---|---|---|
| Refresh interval | dropdown 15 min … 24 h | fixed choices | 1 h |
| Image source URL | text | http(s); `{seed}`/`{width}`/`{height}` tokens optional | current weserv/picsum URL with tokens |
| Quiet hours | two time pickers + enable | hours 0–23, may wrap midnight | off |
| Timezone | auto / manual offset dropdown | ±14 h, 15-min steps | auto (ip-api) |
| Device name | text | `[a-z0-9-]`, ≤ 24 chars; used as mDNS hostname | `ee02` |
| Orientation | dropdown: portrait / portrait-flipped / landscape / landscape-flipped | fixed choices (`setRotation(0–3)`) | portrait |

## Behavior changes

- **Image URL templating:** `{seed}` → random per fetch; `{width}`/
  `{height}` → panel dims *after* rotation, so landscape automatically
  requests 1600×1200. Contract (documented in README): any URL returning a
  baseline (non-progressive) JPEG at the requested size. Default URL:
  `https://images.weserv.nl/?url=picsum.photos/{width}/{height}%3Frandom%3D{seed}&output=jpg`.
- **Orientation:** applied from settings at `epaper.begin()` every boot.
  UI screens (info, provisioning, error, config) switch from hardcoded
  centers to `width()/2`, `height()/2`. `saveFrame`/`loadFrame` store and
  compare the orientation the frame was saved under; mismatch ⇒ treated as
  "no saved frame" (leaving config mode fetches fresh anyway).
- **Quiet hours:** at sleep entry, if the clock is NTP-sane and quiet
  hours enabled: a next timer wake landing inside the window ⇒ sleep until
  window end instead. Timer wakes that still land inside (drift) take the
  existing `quickSleep` fast path. Buttons always wake. Unsynced clock ⇒
  quiet hours silently inactive.
- **Timezone manual mode:** skips the ip-api.com call entirely (privacy +
  offline-friendly). Auto stays default.

## Open-source prep (same milestone)

1. **LICENSE** — MIT at repo root.
2. **README overhaul** — hardware/BOM with links; photo placeholders;
   supported-hardware statement (EE02 + 13.3″ Spectra 6 only); config-mode
   and settings docs; image-URL contract; security caveats (`setInsecure`
   TLS, ip-api plaintext HTTP + non-commercial tier); hobby-project
   support note.
3. **Pin `Seeed_GFX`** to a commit SHA in `platformio.ini`.
4. **CI** — GitHub Actions: `pio run` compile check + native unit tests.
5. **Repo naming** — align README title and repo name (rename to
   `ee02-frame` is Sven's call; GitHub redirects old URLs).
6. **Author email** — history exposes the hotmail address when public;
   accept or switch future commits to GitHub noreply. No history rewrite.
7. **Tag v1.0.0** once this lands.

## Testing

- **Native unit tests** (PlatformIO `native` env, in CI, no hardware):
  quiet-hours window math incl. midnight wrap, URL token substitution,
  settings validation, battery-percent curve. Pure logic is extracted into
  header-only helpers to make this possible.
- **On-hardware checklist** (manual, in the implementation plan): config
  mode entry/exit from sleep and dev mode; save → fetch cycle; orientation
  change end-to-end; quiet-hours sleep duration via logs; first-boot
  provisioning → portal continuation.
