# On-device debug visibility: RAM log + cached-photo debug page

## Context

Two related bench-debugging pains surfaced while working with this repo's
firmware over USB:

1. `pio device monitor` needs a real interactive TTY (`termios.tcgetattr`)
   that isn't available from every environment, and any output printed
   before a monitor attaches is lost — `Serial.print*` (~37 call sites
   across `main.cpp`, `power.cpp`, `net.cpp`, `portal.cpp`, `display.cpp`)
   goes straight to the USB-CDC hardware with nothing capturing it.
2. `runStatusMode()` (`main.cpp`, KEY1 handler) unconditionally falls
   through to `doFetchCycle(true)` on every exit path. Since
   `DEFAULT_IMAGE_URL` is a randomized source (`picsum.photos/...&random={seed}`),
   every KEY1 press — even just to glance at the status screen and back
   out — replaces the currently-displayed photo with a new random one and
   makes a network round trip, regardless of whether anything was
   actually changed.

This spec adds: a small RAM ring-buffer log reachable over HTTP (no TTY
needed), a flash-cached copy of the last successfully fetched photo, and a
combined `/debug` page showing both — plus the `runStatusMode()` control-flow
fix that stops wasting fetches on a plain KEY1 view.

## `DevLog`: RAM ring-buffer log

New `src/devlog.h` / `src/devlog.cpp`. A `Print`-derived object wrapping a
fixed 8KB RAM ring buffer (`constexpr size_t DEVLOG_CAPACITY = 8192`, single
tunable constant).

- `write(uint8_t)` / `write(const uint8_t*, size_t)` forward to the real
  `Serial` unchanged (so `pio device monitor` from an actual terminal keeps
  working exactly as today), and additionally append to the ring buffer,
  overwriting the oldest bytes once full.
- `String snapshot() const` returns the buffered bytes in chronological
  (oldest→newest) order, correctly handling the wrap.
- All ~37 existing `Serial.print`/`println`/`printf` call sites are renamed
  to a shared global instance (e.g. `devLog.print(...)`) — a mechanical
  rename, no logic changes at any call site.
- RAM-only, does not survive deep sleep. This is intentional: `DevLog` is
  read via HTTP, and HTTP is only reachable when the portal is running —
  which in dev mode means a USB host is attached and `maybeSleep()` has
  already kept the board from sleeping in the first place (`power.cpp`).
  A session that ends by unplugging/sleeping starting a fresh buffer on
  the next boot is the right behavior, not data loss.
- No thread-safety concerns: the LED task (the only other FreeRTOS task in
  this firmware) never calls `Serial`/`devLog`, so there's a single writer
  and the portal's HTTP handler (same main task, called from
  `servicePortal()` in `loop()`) is the only reader.

## `PhotoCache`: flash-cached last photo

New `src/photocache.h` / `src/photocache.cpp`, wrapping `LittleFS` mounted
on the existing, currently-unused `spiffs`-labeled partition
(`default_16MB.csv`: 0x360000 bytes, ~3.4MB — no partition table change
needed). `LittleFS.begin(true)` to auto-format on first mount.

- `bool savePhoto(const uint8_t *buf, size_t len)` — writes `buf`/`len` to
  `/last.jpg`, overwriting any previous file.
- `bool hasCachedPhoto()` — existence check.
- `bool streamCachedPhoto(WebServer &server)` — serves `/last.jpg` with
  `Content-Type: image/jpeg` via `server.streamFile()`.
- `bool renderCachedPhoto()` — reads `/last.jpg` back into a buffer and
  calls the existing `renderJpeg()` (`display.h`), for the KEY1
  redisplay-without-fetch path.

Caches the **original downloaded JPEG bytes**, not a per-panel dithered
framebuffer: `fetchImage()` (`net.cpp`) already buffers the complete
downloaded image in one contiguous `PsramSink` buffer
(`sink.buf`/`sink.len`) immediately before calling `renderJpeg()` — caching
means one extra `savePhoto(sink.buf, sink.len)` call right after a
successful render, no new buffering or download logic. This also keeps the
cache format identical across every panel type (mono/gray/color/BWRY) and
lets a browser display it with zero server-side conversion.

## `runStatusMode()` control-flow change

Today (`main.cpp`):

```cpp
if (!connectWifi()) return false;
if (!startPortal()) return true;
PortalResult r = runPortal(10 * 60 * 1000UL);
// ... switch on r only for logging ...
return true; // caller always does doFetchCycle(true) next
```

Becomes: the `PortalResult` decides what happens next, replacing the
caller's unconditional fetch:

- **`Saved` / `ForgetWifi`** → real fetch cycle, unchanged from today (a
  changed image URL, rotation, or Wi-Fi network takes effect immediately;
  `ForgetWifi` needs `connectWifi()` to run anyway to trigger
  reprovisioning).
- **`KeyExit` / `Timeout`** → `renderCachedPhoto()` + `epaper.update()`.
  No network touched, no `connectWifi()` call, the scheduled-fetch
  interval is completely undisturbed.
- **No cached photo yet** (very first boot, nothing successfully fetched
  yet) or **`renderCachedPhoto()` fails** (missing/corrupt file) → falls
  back to a real fetch cycle in both cases, same as `Saved` — there's
  nothing valid to redisplay, so this degrades to today's behavior rather
  than showing an error screen over something this low-stakes.

## Portal routes

Added to the existing `WebServer` registration in `portal.cpp` (alongside
`/`, `/save`, `/action/newpic`, `/action/forgetwifi`):

- `GET /last.jpg` — `PhotoCache::streamCachedPhoto()`.
- `GET /log` — plain-text `devLog.snapshot()` (`text/plain`), for `curl`.
- `GET /debug` — small HTML page, no JS/frameworks (matches
  `portal_html.h`'s existing plain style): `<img src="/last.jpg">`
  labeled "Displayed now", followed by `devLog.snapshot()` in a `<pre>`
  block.

All three are reachable whenever the portal is running — dev mode's
persistent portal, or the 10-minute KEY1 status-mode window — no separate
gating logic needed. Same unauthenticated-on-your-LAN threat model the
README already documents for the rest of the portal.

## Implementation surface

- `src/devlog.h` / `src/devlog.cpp` (new)
- `src/photocache.h` / `src/photocache.cpp` (new)
- `src/net.cpp`: `fetchImage()` calls `PhotoCache::savePhoto()` after a
  successful `renderJpeg()`.
- `src/main.cpp`: `runStatusMode()`'s return/control-flow reworked per
  above; the ~15 `Serial.*` call sites in this file renamed to `devLog.*`.
- `src/power.cpp`, `src/net.cpp`, `src/portal.cpp`, `src/display.cpp`:
  remaining `Serial.*` call sites renamed to `devLog.*`.
- `src/portal.cpp`: three new routes registered; `portal_html.h` gets the
  small `/debug` page template.
- `platformio.ini`: no changes — `LittleFS` ships with the
  `framework-arduinoespressif32` core already in use; no new `lib_deps`.

## Testing

- New native tests (`test/`, `pio test -e native`) for `DevLog`'s
  ring-buffer wraparound/ordering — the one piece of new logic worth
  testing in isolation.
- `pio run -e ee02` (and a spot build of one other env) — build check.
- Flash to hardware and manually verify: `/debug` shows the currently
  displayed photo and recent log output; KEY1 press/exit without changing
  settings does not trigger a network fetch (check `/log` for the absence
  of a new `GET <url>` line); saving a settings change still does.
  On-device/visual verification needs a human with the physical panel —
  the assistant can't screenshot the panel itself.

## Out of scope

- Persisting the log itself across deep sleep (RTC memory or flash) — the
  chosen scope is live bench debugging only, where dev mode already keeps
  the board awake for the whole session.
- A "postmortem" log of unattended scheduled-wake cycles.
- Any auth/access control on the new routes — same threat model as the
  rest of the portal.
- Raw per-panel dithered-framebuffer capture (only the source JPEG is
  cached).
