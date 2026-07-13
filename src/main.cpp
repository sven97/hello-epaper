// EE02 e-paper photo frame — wake, act, deep-sleep. See README.md.
#include <Arduino.h>
#include <WiFiManager.h>
#include "driver/gpio.h"
#include <time.h>

#include "config.h"
#include "display.h"
#include "logic/quiet_hours.h"
#include "net.h"
#include "portal.h"
#include "power.h"
#include "settings.h"
#include "state.h"
#include "ui.h"

Preferences prefs; // NVS namespace "frame"
bool held = false;

RTC_DATA_ATTR uint32_t bootCount = 0;
RTC_DATA_ATTR int32_t lastVbatMv = -1; // survives deep sleep, not reset/flash

// Read the battery and compute the wake-to-wake delta (RTC-persisted).
static int32_t readBatteryWithDelta(int32_t &deltaMv, bool &haveDelta) {
    int32_t vbatMv = readBatteryMv();
    haveDelta = lastVbatMv >= 0;
    deltaMv = haveDelta ? vbatMv - lastVbatMv : 0;
    lastVbatMv = vbatMv;
    if (haveDelta)
        Serial.printf("battery: %.2f V ~%d%% (%+d mV since last wake)\n",
                      vbatMv / 1000.0f, batteryPercent(vbatMv), (int)deltaMv);
    else
        Serial.printf("battery: %.2f V ~%d%%\n",
                      vbatMv / 1000.0f, batteryPercent(vbatMv));
    return vbatMv;
}

// Fetch a new photo, dither it, persist metadata, and show it full-bleed.
// interactive=false (scheduled timer wakes): nobody is watching — on any
// failure keep the current photo untouched, log, and let the next wake
// retry. interactive=true (button presses, power-on, portal exits): draw
// the error screen so the person standing there knows what happened.
static void doFetchCycle(bool interactive) {
    setLed(LedMode::Heartbeat);
    if (!connectWifi(interactive)) {
        if (interactive) showError("Wi-Fi connection failed");
        else Serial.println("wifi failed — keeping photo, retry next wake");
        setLed(LedMode::Solid);
        return;
    }
    String err;
    if (!fetchImage(err)) {
        if (interactive) showError(err);
        else Serial.println("fetch failed (" + err + ") — keeping photo");
        setLed(LedMode::Solid);
        return;
    }
    syncClock();
    recordFetchMetadata();
    Serial.println("updating panel (takes ~20-30 s)...");
    epaper.update();
    Serial.println("done");
    setLed(LedMode::Solid);
}

// KEY1: status page + settings portal. Draw first (from NVS cache, no
// network), then bring Wi-Fi + the portal up — by the time the panel
// finishes its ~30 s refresh and a phone is out, the portal is live.
// Every exit path (KEY1 again, save, timeout, forget-wifi) falls through
// to a fetch cycle so changes take effect visibly. Returns false only when
// Wi-Fi never came up (provisioning fallback already drew its own screen);
// the caller must not run a second connectWifi()/portal window in that case.
static bool runStatusMode(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    drawStatusScreen(vbatMv, deltaMv, haveDelta);
    Serial.println("updating panel (takes ~20-30 s)...");
    setLed(LedMode::Heartbeat);
    epaper.update();
    setLed(LedMode::Solid);
    Serial.println("done");
    if (!connectWifi()) return false; // provisioning fallback already drew
    if (!startPortal()) return true;
    PortalResult r = runPortal(10 * 60 * 1000UL);
    switch (r) {
        case PortalResult::KeyExit: Serial.println("portal: KEY1 exit"); break;
        case PortalResult::Timeout: Serial.println("portal: idle timeout"); break;
        case PortalResult::Saved: break;      // logged in the handler
        case PortalResult::ForgetWifi: break; // next connect reopens provisioning
    }
    // Settings (rotation, url, ...) may have changed: reapply orientation
    // before the fetch redraws the panel.
    applyOrientation();
    applyUtcOffset(prefs.getLong("tzOff", 0)); // manual TZ applies even if the fetch fails
    return true;
}

// KEY3: flip pin/freeze. LED feedback only — the photo stays up.
static void togglePin() {
    held = !held;
    prefs.putBool("held", held);
    Serial.printf("held now %s\n", held ? "on" : "off");
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
    Serial.printf("open-xiao-epaper: boot #%u, wake: %s\n", bootCount, wakeReason());

    prefs.begin("frame", false);
    loadSettings();
    held = prefs.getBool("held", false);
    // TZ env doesn't survive deep sleep: without this, times rendered on
    // non-fetch wakes come out as UTC. Fetch wakes overwrite it with a
    // freshly detected (or manual) offset in syncClock().
    applyUtcOffset(prefs.getLong("tzOff", 0));

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
    initPanelColorMode(); // gray-capable panels only (e.g. EE03); no-op otherwise

    if (btnBits & (1ULL << BTN_PIN)) {
        togglePin(); // photo stays up; no fetch, no panel touch
    } else if (btnBits & (1ULL << BTN_INFO)) {
        if (runStatusMode(vbatMv, deltaMv, haveDelta)) doFetchCycle(true);
        else showError("Wi-Fi connection failed");
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
        Serial.println("usb host gone — leaving dev mode");
        goToSleep(); // never returns
    }

    // Dev mode: keep the settings portal up permanently — the device
    // never sleeps while a USB host is attached, so it costs nothing.
    // connectWifi() stops it around provisioning; restart when Wi-Fi is back.
    if (!portalIsRunning() && WiFi.status() == WL_CONNECTED) {
        setPortalPersistent(true);
        if (startPortal())
            Serial.println("dev mode: portal up at " + portalUrl());
    }
    servicePortal();
    if (takePortalAction()) {
        applyUtcOffset(prefs.getLong("tzOff", 0));
        applyOrientation();
        setLed(LedMode::Solid);
        doFetchCycle(true);
        setLed(LedMode::Off);
    }

    bool info = buttonPressed(BTN_INFO);
    bool pin = !info && buttonPressed(BTN_PIN);
    bool newPic = !info && !pin && buttonPressed(BTN_NEW_PIC);

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
            if (runStatusMode(vbatMv, deltaMv, haveDelta)) doFetchCycle(true);
            else showError("Wi-Fi connection failed");
        } else {
            doFetchCycle(newPic); // KEY2 is interactive; fetchDue is not
        }
        setLed(LedMode::Off);
    }

    delay(50);
}
