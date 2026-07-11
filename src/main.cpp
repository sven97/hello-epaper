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

// Fetch a new photo, dither it, and show it full-bleed.
static void doFetchCycle() {
    if (!connectWifi()) {
        showError("wifi setup failed or timed out");
    } else if (fetchImage()) {
        syncClock();
        recordFetchMetadata();
        Serial.println("updating panel (takes ~20-30 s)...");
        epaper.update();
        Serial.println("done");
    }
}

// KEY1: status page + settings portal. Draw first (from NVS cache, no
// network), then bring Wi-Fi + the portal up — by the time the panel
// finishes its ~30 s refresh and a phone is out, the portal is live.
// Every exit path (KEY1 again, save, timeout, forget-wifi) falls through
// to a fetch cycle so changes take effect visibly.
static void runStatusMode(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    drawStatusScreen(vbatMv, deltaMv, haveDelta);
    Serial.println("updating panel (takes ~20-30 s)...");
    epaper.update();
    Serial.println("done");
    if (!connectWifi()) return; // provisioning fallback already drew
    if (!startPortal()) return;
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
    Serial.printf("ee02-frame: boot #%u, wake: %s\n", bootCount, wakeReason());

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
    digitalWrite(LED_PIN, LOW); // LED on while awake

    int32_t deltaMv;
    bool haveDelta;
    int32_t vbatMv = readBatteryWithDelta(deltaMv, haveDelta);

    epaper.begin();
    applyOrientation(); // settings.rotation; UI + dither target follow

    if (btnBits & (1ULL << BTN_PIN)) {
        togglePin(); // photo stays up; no fetch, no panel touch
    } else if (btnBits & (1ULL << BTN_INFO)) {
        runStatusMode(vbatMv, deltaMv, haveDelta);
        doFetchCycle(); // every portal exit shows a fresh photo
    } else {
        doFetchCycle(); // power-on / btn-new-pic / timer
    }

    digitalWrite(LED_PIN, HIGH);
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
        digitalWrite(LED_PIN, LOW);
        int32_t deltaMv;
        bool haveDelta;
        int32_t vbatMv = readBatteryWithDelta(deltaMv, haveDelta);
        if (info) runStatusMode(vbatMv, deltaMv, haveDelta);
        doFetchCycle();
        digitalWrite(LED_PIN, HIGH);
    }

    delay(50);
}
