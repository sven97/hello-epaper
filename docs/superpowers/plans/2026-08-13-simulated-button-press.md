# Simulated Button Presses Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `/debug/key1`, `/debug/key2`, `/debug/key3` portal routes that trigger the exact same code paths a real KEY1/KEY2/KEY3 press does, so the whole press → observe cycle is scriptable over HTTP.

**Architecture:** A shared `simulateButtonPress()`/`consumeSimulatedPress()` flag pair in `power.h`/`power.cpp`, composed with `||` into the two existing real-GPIO poll sites (`main.cpp`'s `loop()` and `portal.cpp`'s `runPortal()`), plus three thin portal routes that just call `simulateButtonPress()` and return.

**Tech Stack:** Arduino/ESP32 (PlatformIO), no new libraries.

## Global Constraints

- Flags are keyed by the `BTN_INFO`/`BTN_NEW_PIC`/`BTN_PIN` *function* constants (`config.h`), not raw physical KEY1/2/3 GPIO numbers — follows the existing remappable function-assignment abstraction automatically.
- `consumeSimulatedPress()` never blocks (no debounce/release-wait — there's no physical bounce to filter), unlike `buttonPressed()`.
- Routes are `POST`, under `/debug/` (not `/action/`) — a testing tool, not part of the phone-facing settings-portal UI, not linked from `portal_html.h`.
- Response is always `200 text/plain "ok"` once the flag is set, regardless of when the simulated action actually runs (async, next loop pass).
- Not unit-tested — stateful Arduino/hardware-timing glue, same category as `togglePin()`.

---

### Task 1: `simulateButtonPress`/`consumeSimulatedPress` + wire into both poll sites

**Files:**
- Modify: `src/power.h` (2 new declarations)
- Modify: `src/power.cpp` (implementation)
- Modify: `src/main.cpp:237-239` (`loop()`'s `info`/`pin`/`newPic` checks)
- Modify: `src/portal.cpp:260` (`runPortal()`'s `BTN_INFO` check)

**Interfaces:**
- Produces: `void simulateButtonPress(uint8_t pin); bool consumeSimulatedPress(uint8_t pin);` — Task 2's three route handlers call `simulateButtonPress()`.

- [ ] **Step 1: Add the declarations to `src/power.h`**

Append at the end of the file, after the existing `buttonPressed()` declaration:

```cpp

// Marks pin as pressed for the next consumeSimulatedPress(pin) check.
// Call from a portal HTTP handler to simulate a physical press.
void simulateButtonPress(uint8_t pin);

// True once per simulateButtonPress(pin) call (check-and-clear). Unlike
// buttonPressed(), never blocks -- there's no physical bounce to filter.
bool consumeSimulatedPress(uint8_t pin);
```

- [ ] **Step 2: Implement in `src/power.cpp`**

This existing block:

```cpp
bool buttonPressed(uint8_t pin) {
    if (digitalRead(pin) != LOW) return false;
    delay(30);
    if (digitalRead(pin) != LOW) return false;
    while (digitalRead(pin) == LOW) delay(10);
    return true;
}
```

becomes:

```cpp
bool buttonPressed(uint8_t pin) {
    if (digitalRead(pin) != LOW) return false;
    delay(30);
    if (digitalRead(pin) != LOW) return false;
    while (digitalRead(pin) == LOW) delay(10);
    return true;
}

namespace {
volatile bool simInfo = false, simNewPic = false, simPin = false;
} // namespace

void simulateButtonPress(uint8_t pin) {
    if (pin == BTN_INFO) simInfo = true;
    else if (pin == BTN_NEW_PIC) simNewPic = true;
    else if (pin == BTN_PIN) simPin = true;
}

bool consumeSimulatedPress(uint8_t pin) {
    if (pin == BTN_INFO && simInfo) { simInfo = false; return true; }
    if (pin == BTN_NEW_PIC && simNewPic) { simNewPic = false; return true; }
    if (pin == BTN_PIN && simPin) { simPin = false; return true; }
    return false;
}
```

- [ ] **Step 3: Wire into `main.cpp`'s `loop()`**

This existing block:

```cpp
    bool info = buttonPressed(BTN_INFO);
    bool pin = !info && buttonPressed(BTN_PIN);
    bool newPic = !info && !pin && buttonPressed(BTN_NEW_PIC);
```

becomes:

```cpp
    bool info = buttonPressed(BTN_INFO) || consumeSimulatedPress(BTN_INFO);
    bool pin = !info && (buttonPressed(BTN_PIN) || consumeSimulatedPress(BTN_PIN));
    bool newPic = !info && !pin &&
                 (buttonPressed(BTN_NEW_PIC) || consumeSimulatedPress(BTN_NEW_PIC));
```

- [ ] **Step 4: Wire into `portal.cpp`'s `runPortal()`**

This existing block:

```cpp
        if (buttonPressed(BTN_INFO)) {
            result = PortalResult::KeyExit;
            break;
        }
```

becomes:

```cpp
        if (buttonPressed(BTN_INFO) || consumeSimulatedPress(BTN_INFO)) {
            result = PortalResult::KeyExit;
            break;
        }
```

- [ ] **Step 5: Build to verify it compiles**

Run: `pio run -e ee02`
Expected: SUCCESS. (Nothing calls `simulateButtonPress()` yet, so this only checks the new functions and the two call-site edits build cleanly.)

- [ ] **Step 6: Commit**

```bash
git add src/power.h src/power.cpp src/main.cpp src/portal.cpp
git commit -m "Add simulateButtonPress/consumeSimulatedPress, wire into both poll sites"
```

---

### Task 2: `/debug/key1`, `/debug/key2`, `/debug/key3` routes + docs + full verification

**Files:**
- Modify: `src/portal.cpp` (3 handlers, 3 route registrations)
- Modify: `README.md` (one-line mention)

**Interfaces:**
- Consumes: `simulateButtonPress(uint8_t)` from Task 1.

- [ ] **Step 1: Add the three handlers**

In `src/portal.cpp`, this existing block:

```cpp
static void handleLastJpg() { streamCachedPhoto(server); }
static void handleCurrent() { streamCurrentBmp(server); }
static void handlePrevious() { streamPreviousBmp(server); }

static void handleLog() {
```

becomes:

```cpp
static void handleLastJpg() { streamCachedPhoto(server); }
static void handleCurrent() { streamCurrentBmp(server); }
static void handlePrevious() { streamPreviousBmp(server); }

static void handleDebugKey1() { simulateButtonPress(BTN_INFO); server.send(200, "text/plain", "ok"); }
static void handleDebugKey2() { simulateButtonPress(BTN_NEW_PIC); server.send(200, "text/plain", "ok"); }
static void handleDebugKey3() { simulateButtonPress(BTN_PIN); server.send(200, "text/plain", "ok"); }

static void handleLog() {
```

- [ ] **Step 2: Register the routes**

This existing block:

```cpp
        server.on("/last.jpg", HTTP_GET, handleLastJpg);
        server.on("/current", HTTP_GET, handleCurrent);
        server.on("/previous", HTTP_GET, handlePrevious);
        server.on("/log", HTTP_GET, handleLog);
```

becomes:

```cpp
        server.on("/last.jpg", HTTP_GET, handleLastJpg);
        server.on("/current", HTTP_GET, handleCurrent);
        server.on("/previous", HTTP_GET, handlePrevious);
        server.on("/debug/key1", HTTP_POST, handleDebugKey1);
        server.on("/debug/key2", HTTP_POST, handleDebugKey2);
        server.on("/debug/key3", HTTP_POST, handleDebugKey3);
        server.on("/log", HTTP_GET, handleLog);
```

- [ ] **Step 3: Add the README mention**

Find the paragraph documenting `/debug`/`/log` (added in the earlier debug-visibility PR, ends "...Same unauthenticated-on-your-LAN threat model as the rest of the portal."). Add directly after it:

```markdown

`POST /debug/key1`, `/debug/key2`, `/debug/key3` simulate a physical
KEY1/KEY2/KEY3 press (status view, new photo, pin/freeze) without
touching the board — e.g. `curl -X POST http://<name>.local/debug/key2`.
Same threat model as everything else here.
```

- [ ] **Step 4: Build to verify it compiles**

Run: `pio run -e ee02`
Expected: SUCCESS.

- [ ] **Step 5: Run the native test suite**

Run: `pio test -e native`
Expected: PASS, all existing suites unaffected (no new native tests in this plan — see Global Constraints).

- [ ] **Step 6: Build every firmware env**

```bash
pio run -e ee02
pio run -e ee03
pio run -e ee04
pio run -e ee05
```

Expected: all 4 SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add src/portal.cpp README.md
git commit -m "Portal: add /debug/key1, /debug/key2, /debug/key3 routes"
```

- [ ] **Step 8: Flash to hardware and verify the feature end to end using itself**

Flash to the attached board, keep the computer attached so dev mode's
portal stays up, then from a shell:

```bash
curl -X POST http://<name>.local/debug/key1   # simulate KEY1
sleep 2
curl http://<name>.local/log                  # expect: status screen drawn, portal opened
curl -o current.bmp http://<name>.local/current  # expect: status screen, not the photo

curl -X POST http://<name>.local/debug/key1   # simulate KEY1 again -- exit
sleep 2
curl http://<name>.local/log                  # expect: "portal: KEY1 exit", then a redisplay
                                              # (dithering log lines), no new "GET <url>" line
curl -o current2.bmp http://<name>.local/current # expect: photo again, matching before KEY1

curl -X POST http://<name>.local/debug/key2   # simulate KEY2
sleep 30
curl http://<name>.local/log                  # expect: a new "GET <url>" line -- a real fetch happened

curl -X POST http://<name>.local/debug/key3   # simulate KEY3
sleep 1
curl http://<name>.local/log                  # expect: "held now on" (or "off") pin-toggle line
```

This step needs a human with the physical board attached over USB (dev
mode) — the assistant can drive and verify all of it over HTTP once
that's true, without anyone touching the buttons.

- [ ] **Step 9: Commit if Step 8 uncovered fixes**

If hardware verification required any code changes, commit them now with
a description of what was fixed. If nothing needed fixing, no commit is
needed for this task.
