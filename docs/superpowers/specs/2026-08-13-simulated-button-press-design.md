# Simulated button presses: /debug/key1, /debug/key2, /debug/key3

## Context

The debug-visibility routes shipped in the last two PRs (`/log`, `/debug`,
`/last.jpg`, `/current`, `/previous`) let a remote client (Claude, or
anyone with `curl`) *observe* the device's state over HTTP. There is no
equivalent for *driving* it: exercising the KEY1 (status/portal),
KEY2 (new photo), and KEY3 (pin/freeze) flows requires a human physically
pressing the board's buttons. This spec adds three routes that trigger
the exact same code paths a real press does, so the whole
press → observe cycle is scriptable end to end.

`POST /action/newpic` already happens to produce KEY2's effect (it sets
the same `exitRequested`/`PortalResult::Saved` flags a portal Save does,
which `loop()`'s `takePortalAction()` picks up and turns into a real
`doFetchCycle(true)`), but that's a coincidence of how the settings
portal's "fetch new picture" button works, not a deliberate "simulate
KEY2" primitive — and there's no equivalent at all for KEY1 or KEY3. This
spec routes all three buttons through one new, uniform mechanism instead.

## Where real button presses are read today

Two real-GPIO polling call sites, both via `power.cpp`'s
`buttonPressed(pin)` (falling-edge debounce, blocks until release):

- `main.cpp`'s `loop()` (dev mode only — the only place any button is
  polled while awake and not physically asleep):
  ```cpp
  bool info = buttonPressed(BTN_INFO);
  bool pin = !info && buttonPressed(BTN_PIN);
  bool newPic = !info && !pin && buttonPressed(BTN_NEW_PIC);
  ```
- `portal.cpp`'s `runPortal()` — while a KEY1-triggered status/portal
  session is active, this is the *only* button checked, so a second KEY1
  press can exit the session:
  ```cpp
  if (buttonPressed(BTN_INFO)) {
      result = PortalResult::KeyExit;
      break;
  }
  ```

## Mechanism: shared simulated-press flags

New functions in `power.h`/`power.cpp`, right alongside `buttonPressed()`
(which already documents itself as "shared by the dev-mode loop and the
portal loop" — these have the identical sharing shape):

```cpp
// Marks pin as pressed for the next consumeSimulatedPress(pin) check.
// Call from a portal HTTP handler to simulate a physical press.
void simulateButtonPress(uint8_t pin);

// True once per simulateButtonPress(pin) call (check-and-clear, no
// debounce/release-wait needed since there's no physical bounce to
// filter -- unlike buttonPressed(), this never blocks).
bool consumeSimulatedPress(uint8_t pin);
```

Storage: three named `volatile bool` flags (not an array indexed by raw
GPIO number), matched against `BTN_INFO`/`BTN_NEW_PIC`/`BTN_PIN` inside
both functions. Keying by these *function* constants rather than physical
KEY1/2/3 numbers means the simulated-press mechanism automatically
follows `config.h`'s existing remappable "function assignment" — if
someone ever reassigns which physical key means "status", the simulated
version follows it without changes here.

All three real-GPIO call sites become `buttonPressed(X) ||
consumeSimulatedPress(X)`:

- `loop()`'s `info`, `pin`, **and now also `newPic`** checks (dropping the
  `/action/newpic` coincidence in favor of routing KEY2 through this same
  uniform mechanism, so `/debug/key2` faithfully simulates *only* what a
  real KEY2 press does, not incidentally also reapplying orientation/TZ
  the way `takePortalAction()`'s path does).
- `runPortal()`'s inner `BTN_INFO` check, so a simulated second KEY1 can
  exit an active status/portal session exactly like a real one.

**No re-entrancy risk:** an HTTP handler that calls `simulateButtonPress()`
sets a flag and returns immediately — its response is already sent by the
time anything acts on the flag. The actual action always runs on the
*next* pass of whichever loop is polling (`loop()` or `runPortal()`'s
inner loop), which is the same timing a real press already has via its
own debounce delay. Nothing here calls back into `server.handleClient()`
synchronously from within a handler.

## Routes

Three new routes in `portal.cpp`, alongside the existing ones:

```cpp
static void handleDebugKey1() { simulateButtonPress(BTN_INFO); server.send(200, "text/plain", "ok"); }
static void handleDebugKey2() { simulateButtonPress(BTN_NEW_PIC); server.send(200, "text/plain", "ok"); }
static void handleDebugKey3() { simulateButtonPress(BTN_PIN); server.send(200, "text/plain", "ok"); }
```

```cpp
server.on("/debug/key1", HTTP_POST, handleDebugKey1);
server.on("/debug/key2", HTTP_POST, handleDebugKey2);
server.on("/debug/key3", HTTP_POST, handleDebugKey3);
```

`POST`, matching the existing `/action/*` convention (side-effecting
routes shouldn't be `GET`-able by accident, e.g. a browser prefetch or
crawler). `/debug/` prefix (not `/action/`) to keep these visually and
namespace-distinct from the phone-facing settings-portal action buttons
— these are a testing/debug tool, not part of the human-facing UI, and
aren't linked from `portal_html.h`. Response is always `200 text/plain
"ok"` once the flag is set, regardless of when the simulated action
actually runs (async, next loop pass) — same "fire and forget" contract
`/action/newpic` already has today.

Same unauthenticated-on-your-LAN threat model as every other portal
route.

## Error handling

None needed beyond what already exists: `simulateButtonPress()` can't
fail (it's an unconditional flag set), and there's no invalid input to
reject (the three routes are fixed, parameter-free).

## Implementation surface

- `src/power.h`: two new declarations.
- `src/power.cpp`: `simulateButtonPress()`/`consumeSimulatedPress()`
  implementation.
- `src/main.cpp`: `loop()`'s `info`/`pin`/`newPic` checks extended.
- `src/portal.cpp`: `runPortal()`'s `BTN_INFO` check extended; three new
  handlers + route registrations.
- `README.md`: one-line mention alongside the existing `/debug` route
  documentation.

## Testing

Not unit-tested — this is stateful Arduino/hardware-timing glue tied to
real GPIO polling loops, the same category as `togglePin()`, which isn't
tested either. Verification is on hardware: build, flash, then use the
feature to test itself — `curl -X POST http://<name>.local/debug/key1`,
confirm via `/log` that `runStatusMode()` ran and `/current` now shows
the status screen; `curl -X POST .../debug/key1` again, confirm `/log`
shows `portal: KEY1 exit` and `/current` shows the photo again;
`/debug/key2`, confirm a real fetch happened (`GET <url>` in `/log`);
`/debug/key3`, confirm `/log` shows the pin toggle message.

## Out of scope

- Simulating presses while the device is asleep (deep sleep) — these
  routes only have any effect while the portal is reachable, which
  already requires the device to be awake (dev mode or an active KEY1
  window), same precondition every other portal route already has.
- Any response richer than a fixed `"ok"` (e.g. reporting the actual
  post-action state) — the existing `/log`/`/current`/`/previous`/
  `/last.jpg` routes already cover observing the result.
