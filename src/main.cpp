// EE02 e-paper photo frame — wake, act, deep-sleep. See README.md.
#include <Arduino.h>
#include <WiFiManager.h>
#include "driver/gpio.h"
#include <esp_ota_ops.h>
#include <time.h>

#include "config.h"
#include "display.h"
#include "devlog.h"
#include "logic/quiet_hours.h"
#include "logic/ota_trial.h"
#include "logic/stuck_error.h"
#include "ota_state.h"
#include "net.h"
#include "photocache.h"
#include "portal.h"
#include "power.h"
#include "settings.h"
#include "state.h"
#include "ui.h"

Preferences prefs; // NVS namespace "frame"
bool held = false;

RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR int32_t lastVbatMv = -1; // survives deep sleep, not reset/flash
RTC_DATA_ATTR uint32_t fetchFailStreak = 0; // consecutive full-cycle failures
RTC_DATA_ATTR bool stuckErrorShown = false; // one-time flag per outage
// OTA trial + cadence state is NVS-backed (ota_state.h), not RTC: the OTA
// path esp_restart()s, and on the S3 that clears RTC_DATA_ATTR.

// Read the battery and compute the wake-to-wake delta (RTC-persisted).
static int32_t readBatteryWithDelta(int32_t &deltaMv, bool &haveDelta) {
    int32_t vbatMv = readBatteryMv();
    haveDelta = lastVbatMv >= 0;
    deltaMv = haveDelta ? vbatMv - lastVbatMv : 0;
    lastVbatMv = vbatMv;
    if (haveDelta)
        devLog.printf("battery: %.2f V ~%d%% (%+d mV since last wake)\n",
                      vbatMv / 1000.0f, batteryPercent(vbatMv), (int)deltaMv);
    else
        devLog.printf("battery: %.2f V ~%d%%\n",
                      vbatMv / 1000.0f, batteryPercent(vbatMv));
    return vbatMv;
}

// Fetch a new photo, dither it, persist metadata, and show it full-bleed.
// interactive=false (scheduled timer wakes): nobody is watching — on any
// failure keep the current photo untouched, log, and let the next wake
// retry. interactive=true (button presses, power-on, portal exits): draw
// the error screen so the person standing there knows what happened.
// Unattended-wake escalation: once the streak has failed long enough
// (per settings.sleepSecs) that a transient outage can't explain it,
// draw the error screen once so a stuck frame is discoverable by looking
// at it, instead of only by noticing a stale photo days later. Never
// called for interactive wakes -- those already show an error on every
// single failure.
static void maybeShowStuckError() {
    if (!shouldShowStuckError(fetchFailStreak, settings.sleepSecs,
                              stuckErrorShown))
        return;
    stuckErrorShown = true;
    showError("Wi-Fi hasn't reconnected in over 6h - press KEY1 to check");
}

// App-level OTA rollback. Runs every wake while an image is on trial,
// BEFORE the quiet-hours/pinned fast-exit so a boot loop that only ever
// quiet-exits still trips the boot budget. ConfirmGood is decided later,
// in doFetchCycle(), the moment a photo actually renders.
static void otaBootHealthGuard() {
    OtaState os = otaStateLoad();
    if (os.pendingBuild == 0) return;
    OtaTrialState ts{os.pendingBuild, (uint32_t)FW_BUILD_NUMBER,
                     os.trialBoots, os.trialFetchFails, /*fetchSucceeded=*/false};
    switch (otaTrialVerdict(ts)) {
    case OtaTrialVerdict::Revert: {
        const esp_partition_t *prev = esp_ota_get_next_update_partition(nullptr);
        devLog.printf("ota: trial for build %u failed (boots=%u fails=%u) — reverting\n",
                      os.pendingBuild, os.trialBoots, os.trialFetchFails);
        if (prev && esp_ota_set_boot_partition(prev) == ESP_OK) {
            otaStateClearTrial(); // cleared only once the revert is committed
            delay(100);
            esp_restart();
        }
        // Revert failed (passive slot has no valid image). Leave the trial
        // state set: trialPending keeps maybeRunOtaCheck() from
        // re-downloading the same bad build, and the next boot retries the
        // revert in case the slot becomes usable.
        devLog.println("ota: revert failed — staying on current image");
        break;
    }
    case OtaTrialVerdict::Continue:
        otaStateSetTrialBoots(os.trialBoots + 1);
        devLog.printf("ota: build %u on trial (boot %u/%u)\n",
                      os.pendingBuild, os.trialBoots + 1, OTA_TRIAL_MAX_BOOTS);
        break;
    default:
        break;
    }
}

static void doFetchCycle(bool interactive) {
    setLed(LedMode::Heartbeat);
    // Every retry after the first failure of a new outage forces a fresh
    // radio state first -- see connectWifi()'s forceRadioReset.
    bool forceRadioReset = fetchFailStreak > 0;
    if (!connectWifi(interactive, forceRadioReset)) {
        fetchFailStreak++;
        otaStateNoteCycleFail();
        if (interactive) {
            showError("Wi-Fi connection failed");
        } else {
            devLog.println("wifi failed — keeping image, retry next wake");
            maybeShowStuckError();
        }
        setLed(LedMode::Solid);
        return;
    }
    String err;
    if (!fetchImage(err)) {
        fetchFailStreak++;
        otaStateNoteCycleFail();
        if (interactive) {
            showError(err);
        } else {
            devLog.println("fetch failed (" + err + ") — keeping image");
            maybeShowStuckError();
        }
        setLed(LedMode::Solid);
        return;
    }
    fetchFailStreak = 0;
    stuckErrorShown = false;
    syncClock();
    recordFetchMetadata();
    devLog.println("updating display (takes ~20-30 s)...");
    epaper.update();
    devLog.println("done");
    setLed(LedMode::Solid);

    // The just-flashed image rendered a photo — it works. Close the trial.
    if (otaStateNoteCycleOk())
        devLog.printf("ota: build %u confirmed good\n", (uint32_t)FW_BUILD_NUMBER);

    // Unattended wakes only: check for a newer firmware now that a fresh
    // photo is up and the JPEG framebuffer is freed. May not return.
    if (!interactive)
        maybeRunOtaCheck(batteryPercent(lastVbatMv));
}

// KEY1: status screen + settings portal. Draw first (from NVS cache, no
// network), then bring Wi-Fi + the portal up — by the time the display
// finishes its ~30 s refresh and a phone is out, the portal is live.
// Settings auto-save as they're changed; an image-source change asks for
// a fetch (takePortalFetch()) so the new source is visible on exit.
// Otherwise (KEY1 again, idle timeout) redisplay the cached image instead
// of burning a network fetch -- and, since the default image source is
// randomized, instead of silently swapping the image just because someone
// glanced at the status screen. Returns false only when Wi-Fi never came
// up (provisioning fallback already drew its own screen); the caller must
// not run a second connectWifi()/portal window in that case.
static bool runStatusMode(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    devLog.println("drawing status screen (takes ~20-30 s)...");
    setLed(LedMode::Heartbeat);
    drawStatusScreen(vbatMv, deltaMv, haveDelta); // draws + epaper.update()
    setLed(LedMode::Solid);
    devLog.println("done");
    if (!connectWifi()) return false; // provisioning fallback already drew
    if (!startPortal()) { doFetchCycle(true); return true; }
    PortalResult r = runPortal(10 * 60 * 1000UL);
    devLog.println(r == PortalResult::KeyExit ? "portal: KEY1 exit"
                                              : "portal: idle timeout");
    // Fields auto-saved as they were changed; orientation/cached re-renders
    // already happened inside runPortal(). Reapply the manual TZ offset
    // (it doesn't survive as an env across the portal loop) and, if the
    // image source changed, fetch it now so the change is visible.
    applyOrientation();
    applyUtcOffset(prefs.getLong("tzOff", 0));

    if (takePortalFetch()) {
        doFetchCycle(true); // image source changed -- show it now
        return true;
    }
    // Nothing needs a fresh fetch -- redisplay the cached image.
    setLed(LedMode::Heartbeat);
    if (renderCachedPhoto()) {
        devLog.println("updating display (takes ~20-30 s)...");
        epaper.update();
        devLog.println("done");
        setLed(LedMode::Solid);
    } else {
        setLed(LedMode::Solid);
        doFetchCycle(true); // no cache yet (e.g. first boot) -- fall back
    }
    return true;
}

// KEY3: flip pin/freeze. LED feedback only — the photo stays up.
static void togglePin() {
    held = !held;
    prefs.putBool("held", held);
    devLog.printf("held now %s\n", held ? "on" : "off");
    blinkLed(held ? 2 : 1);
}

void setup() {
    // Instant acknowledgment: blink before anything else so a button press
    // gets feedback in ~0.5 s (the panel itself takes ~30 s to change).
    // 1 blink = new picture, 2 = status page, 3 = pin/freeze.
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
        uint64_t ackBits = esp_sleep_get_ext1_wakeup_status();
        int n = (ackBits & (1ULL << BTN_NEW_PIC)) ? 1
              : (ackBits & (1ULL << BTN_INFO))    ? 2
              : (ackBits & (1ULL << BTN_PIN))     ? 3 : 0;
        blinkLed(n, 80); // fast ack: even 3 blinks finish in ~480 ms
    }

    Serial.begin(115200);
    // USB-CDC needs ~2 s before prints are visible — only worth paying on a
    // cold start (bench/debug). Button and timer wakes get a token delay.
    delay(esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED ? 2000
                                                                     : 200);
    bootCount++;
    devLog.printf("open-xiao-epaper: boot #%u, wake: %s, firmware: %s\n",
                  bootCount, wakeReason(), FW_GIT_HASH);

    prefs.begin("frame", false);
    loadSettings();
    held = prefs.getBool("held", false);
    // TZ env doesn't survive deep sleep: without this, times rendered on
    // non-fetch wakes come out as UTC. Fetch wakes overwrite it with a
    // freshly detected (or manual) offset in syncClock().
    applyUtcOffset(prefs.getLong("tzOff", 0));

    otaBootHealthGuard(); // may esp_restart() (rollback) — never returns then

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    uint64_t btnBits = (cause == ESP_SLEEP_WAKEUP_EXT1)
                           ? esp_sleep_get_ext1_wakeup_status() : 0;

    // Fast paths for timer wakes that shouldn't touch the panel: pinned,
    // or the wake landed inside the quiet window. GPIO holds from the
    // previous sleep stay latched, so these must run before hold-release.
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        if (held) quickSleep(plannedSleepSecs()); // no return
        time_t now = time(nullptr);
        if (settings.quietEnabled && now > CLOCK_SANE_EPOCH) {
            struct tm lt;
            localtime_r(&now, &lt);
            int sod = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
            if (inQuietWindow(sod, settings.quietStartHour,
                              settings.quietEndHour))
                quickSleep(secondsUntilQuietEnd(
                    sod, settings.quietStartHour,
                    settings.quietEndHour)); // no return
        }
    }

    // Release the pin holds from the previous deep sleep (no-op on first
    // boot) so the panel and battery divider can be driven again.
    gpio_hold_dis((gpio_num_t)EPAPER_EN_PIN);
    gpio_hold_dis((gpio_num_t)BATTERY_EN_PIN);

    pinMode(BTN_NEW_PIC, INPUT); // external pull-ups on board
    pinMode(BTN_INFO, INPUT);    // polled by loop() in dev mode
    pinMode(BTN_PIN, INPUT);
    startLedTask();
    setLed(LedMode::Solid);

    int32_t deltaMv;
    bool haveDelta;
    int32_t vbatMv = readBatteryWithDelta(deltaMv, haveDelta);

    epaper.begin();
    applyOrientation(); // settings.rotation; UI + dither target follow

    if (btnBits & (1ULL << BTN_PIN)) {
        togglePin(); // photo stays up; no fetch, no panel touch
    } else if (btnBits & (1ULL << BTN_INFO)) {
        if (!runStatusMode(vbatMv, deltaMv, haveDelta))
            showError("Wi-Fi connection failed");
    } else {
        doFetchCycle(cause != ESP_SLEEP_WAKEUP_TIMER); // power-on / btn-new-pic / timer
    }

    setLed(LedMode::Off);
    maybeSleep(); // deep sleep — or return, in dev mode, and run loop()
}

// Only runs in dev mode (USB host attached): the port stays up for
// instant flashing, buttons are polled instead of EXT1-woken, and the
// configured photo cadence still applies. Host gone -> normal deep sleep.
void loop() {
    if (!usbHostPresent()) {
        devLog.println("usb host gone — leaving dev mode");
        goToSleep(); // never returns
    }

    // Dev mode: keep the settings portal up permanently — the device
    // never sleeps while a USB host is attached, so it costs nothing.
    // connectWifi() stops it around provisioning; restart when Wi-Fi is back.
    if (!portalIsRunning() && WiFi.status() == WL_CONNECTED) {
        setPortalPersistent(true);
        if (startPortal())
            devLog.println("dev mode: portal up at " + portalUrl());
    }
    servicePortal(); // pumps HTTP + any pending cached-image re-render
    if (takePortalFetch()) { // image source changed via /set
        applyUtcOffset(prefs.getLong("tzOff", 0));
        applyOrientation();
        setLed(LedMode::Solid);
        doFetchCycle(true);
        setLed(LedMode::Off);
    }

    bool info = buttonPressed(BTN_INFO) || consumeSimulatedPress(BTN_INFO);
    bool pin = !info && (buttonPressed(BTN_PIN) || consumeSimulatedPress(BTN_PIN));
    bool newPic = !info && !pin &&
                 (buttonPressed(BTN_NEW_PIC) || consumeSimulatedPress(BTN_NEW_PIC));

    bool fetchDue = false;
    if (!held) {
        time_t lastFetch = (time_t)prefs.getULong("lastEpoch", 0);
        fetchDue = time(nullptr) - lastFetch >= (time_t)settings.sleepSecs;
    }

    if (pin) {
        togglePin();
    } else if (info || newPic || fetchDue) {
        setLed(LedMode::Solid);
        int32_t deltaMv;
        bool haveDelta;
        int32_t vbatMv = readBatteryWithDelta(deltaMv, haveDelta);
        if (info) {
            if (!runStatusMode(vbatMv, deltaMv, haveDelta))
                showError("Wi-Fi connection failed");
        } else {
            doFetchCycle(newPic); // KEY2 is interactive; fetchDue is not
        }
        setLed(LedMode::Off);
    }

    delay(50);
}
