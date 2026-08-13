# On-device debug visibility: live panel capture (`/current`, `/previous`)

## Context

The debug-visibility feature just shipped (`docs/superpowers/specs/2026-08-13-debug-visibility-design.md`,
PR #19) added `/last.jpg`, `/log`, and `/debug`, but `/last.jpg` only ever
shows the *last successfully fetched photo* — it says nothing about what's
actually on the panel right now. `drawStatusScreen()` (`ui.cpp`), the error
screen (`showError()`, `display.cpp`), and the provisioning screen
(`showProvisioningScreenOnce()`, `net.cpp`) all draw straight into the
panel sprite and are never captured anywhere. Pressing KEY1 to check the
status screen, there is currently no way to see what actually rendered
without physically looking at the panel.

This spec adds two more routes that read the panel's actual sprite
content, not a cached source file: **`/current`** (whatever's on the panel
right now, generated on demand) and **`/previous`** (whatever was on the
panel immediately before the most recent redraw).

## Why this works for every panel type

`EPaper` (`Extensions/EPaper.h` in the vendored `Seeed_GFX`) extends
`TFT_eSprite`. Its `readPixel(x, y)` already normalizes every supported
panel's internal storage format — 1bpp mono, 4bpp 16-gray, 16bpp full
color — back to a plain `uint16_t` RGB565 value
(`Extensions/Sprite.cpp:978-1060`). A capture routine built on `readPixel`
therefore works identically across EE02/03/04/05 with no per-panel branching.

The sprite (`epaper`, global in `display.h`) stays resident in RAM for the
whole time the firmware runs — `update()` pushes its content to the
physical panel but never frees it. Combined with this firmware's
single-task, cooperative structure (HTTP requests are only serviced
between draw calls via `servicePortal()`/`runPortal()`'s loop, never mid-draw),
reading the live sprite on demand is always consistent — no risk of
reading a half-drawn frame.

## `src/logic/bmp_thumbnail.h` (pure, host-testable)

Same pattern as `ring_log.h`: no Arduino deps, `pio test -e native`
coverage. BMP is chosen over JPEG/PNG because an uncompressed encoder is a
few dozen lines with no library, at the cost of being larger per byte —
acceptable since captures are downscaled thumbnails, not archival images.

```cpp
constexpr int THUMBNAIL_MAX_LONG_SIDE = 400;

// Nearest-neighbor downscale, capping the long side at maxLongSide while
// preserving aspect ratio. Never upscales (srcW/H below the cap pass
// through unchanged).
void computeThumbnailSize(int srcW, int srcH, int maxLongSide,
                          int &outW, int &outH);

// Maps a destination pixel coordinate to the nearest source coordinate.
int nearestSourceCoord(int destIdx, int destSize, int srcSize);

// RGB565 -> individual 8-bit channels.
void rgb565ToRgb888(uint16_t color, uint8_t &r, uint8_t &g, uint8_t &b);

// Per-row byte count for a 24bpp BMP, padded up to a multiple of 4.
int bmpRowStride(int width);

// Total file size: 54-byte header + bmpRowStride(width) * height.
size_t bmpFileSize(int width, int height);

// Writes the 54-byte BITMAPFILEHEADER + BITMAPINFOHEADER for an
// uncompressed 24bpp bitmap of the given dimensions into out[0..53].
void writeBmpHeader(uint8_t *out, int width, int height);

// BMP stores rows bottom-up: the fileRow'th row written to the stream is
// image row bmpSourceRowForFileRow(fileRow, height), not fileRow itself.
// Getting this backwards produces an upside-down image -- worth a named,
// tested function rather than inlining the subtraction at each call site.
int bmpSourceRowForFileRow(int fileRow, int height);
```

## `src/screencapture.h/.cpp` (Arduino glue)

The only code that calls `epaper.readPixel()`. RAM-only — no LittleFS
involved, matching `DevLog`'s reasoning: both are only ever read while the
portal is reachable, which in dev mode means the board is already staying
awake for the whole session. This also avoids a flash write on every
screen change (status views, errors, provisioning are all more frequent
than `PhotoCache`'s once-per-successful-fetch writes).

```cpp
// GET /current: no storage. Computes the thumbnail size from
// epaper.width()/height(), streams a BMP header then each row via
// server.sendContent(), sampling readPixel() live at nearest-neighbor
// source coordinates as it goes. Returns false (after sending its own
// error response) on a degenerate 0x0 panel size; same bool-return
// convention as PhotoCache::streamCachedPhoto(), callers may ignore it.
bool streamCurrentBmp(WebServer &server);

// Captures the sprite's CURRENT content (before it's overwritten) into a
// ps_malloc'd PSRAM buffer, freeing and replacing whatever was captured
// before. Called at the top of every function that's about to draw a new
// screen -- see call sites below.
void snapshotPrevious();

// GET /previous: streams the stored buffer as image/bmp, or sends a 404
// and returns false if snapshotPrevious() has never been called.
bool streamPreviousBmp(WebServer &server);
```

`snapshotPrevious()` call sites (5, each a one-line insertion at the top
of the function, before any drawing starts):

- `ui.cpp`: `drawStatusScreen()`
- `net.cpp`: `fetchImage()`, `showProvisioningScreenOnce()`
- `photocache.cpp`: `renderCachedPhoto()`
- `display.cpp`: `showError()`

## Portal routes

Added to `portal.cpp` alongside the existing routes:

- `GET /current` -> `streamCurrentBmp(server)`
- `GET /previous` -> `streamPreviousBmp(server)`

## `/debug` page update

`portal_html.h`'s `DEBUG_HTML` gets two more images alongside the existing
"Displayed now" (`/last.jpg`, which keeps its current meaning: the last
successfully *fetched photo*, independent of whatever screen is currently
showing):

```
Displayed now (last fetched photo): <img src="/last.jpg">
On the panel right now:             <img src="/current">
On the panel just before that:      <img src="/previous">
Log: <pre>...</pre>
```

## Error handling

- `/current` on a panel with `width()`/`height()` of 0 (shouldn't happen
  post-`epaper.begin()`, but defensively) sends a 500 rather than
  dividing by zero in the downscale math.
- `/previous` before any `snapshotPrevious()` call (fresh boot, nothing
  drawn yet this session) sends a 404, same convention as
  `PhotoCache::streamCachedPhoto()`'s "no cached photo yet" case.
- A failed `ps_malloc` in `snapshotPrevious()` leaves the previous
  snapshot (if any) in place rather than clearing it — a stale-but-valid
  previous capture is more useful than none.

## Implementation surface

- `src/logic/bmp_thumbnail.h` (new)
- `src/screencapture.h` / `src/screencapture.cpp` (new)
- `src/ui.cpp`: `snapshotPrevious()` call added to `drawStatusScreen()`
- `src/net.cpp`: `snapshotPrevious()` call added to `fetchImage()` and
  `showProvisioningScreenOnce()`
- `src/photocache.cpp`: `snapshotPrevious()` call added to
  `renderCachedPhoto()`
- `src/display.cpp`: `snapshotPrevious()` call added to `showError()`
- `src/portal.cpp`: two new routes
- `src/portal_html.h`: `DEBUG_HTML` gets the two new `<img>` tags

## Testing

- New native tests (`test/`, `pio test -e native`) for
  `bmp_thumbnail.h`: `computeThumbnailSize()` (including the "never
  upscale" case), `nearestSourceCoord()`, `rgb565ToRgb888()`,
  `bmpRowStride()` (padding boundary cases), `bmpFileSize()`,
  `writeBmpHeader()` (byte-exact header check against a known-good BMP
  header), and `bmpSourceRowForFileRow()`.
- `pio run` for all 4 envs — build check.
- Flash to hardware and manually verify: `/current` matches what's
  visually on the panel; press KEY1, confirm `/previous` shows the photo
  that was up before the status screen; open `/debug` and confirm all
  three images render. Needs a human with the physical panel, same
  limitation noted in the prior spec.

## Out of scope

- Full-resolution capture (sizing decision: downscaled thumbnail only,
  `THUMBNAIL_MAX_LONG_SIDE = 400`).
- Flash-persisting `/previous` across deep sleep.
- A history deeper than one prior snapshot.
- JPEG/PNG compression of captures (BMP is deliberately simple/uncompressed).
