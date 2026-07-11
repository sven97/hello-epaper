// EE02 e-paper photo frame — wake, act, deep-sleep. See README.md.
#include <Arduino.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include "driver/gpio.h"

#include "config.h"
#include "display.h"
#include "net.h"
#include "power.h"
#include "settings.h"
#include "state.h"
#include "ui.h"

Preferences prefs; // NVS namespace "frame"
bool infoVisible = false;
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

// Fetch a new photo, dither it, persist it, and show it full-bleed.
// Called for the "real" fetch wakes (power-on / btn-new-pic / timer) and
// as the fallback when a toggle wake finds no saved frame yet.
static void doFetchCycle(int32_t vbatMv, int32_t deltaMv, bool haveDelta) {
    if (!connectWifi()) {
        showError("wifi setup failed or timed out");
    } else if (fetchImage()) {
        syncClock();
        saveFrame();
        recordFetchMetadata();
        // A new photo always shows the photo — leave the info page.
        if (infoVisible) {
            infoVisible = false;
            prefs.putBool("info", false);
        }
        Serial.println("updating panel (takes ~20-30 s)...");
        epaper.update();
        Serial.println("done");
    }
}

// BTN_INFO / BTN_PIN wakes: flip the state, then draw whatever the new
// state calls for (info page, restored photo, or nothing).
static void handleToggleWake(bool isToggleInfo, int32_t vbatMv,
                             int32_t deltaMv, bool haveDelta) {
    if (isToggleInfo) {
        infoVisible = !infoVisible;
        prefs.putBool("info", infoVisible);
        Serial.printf("info screen now %s\n", infoVisible ? "on" : "off");
    } else {
        held = !held;
        prefs.putBool("held", held);
        Serial.printf("held now %s\n", held ? "on" : "off");
        blinkLed(held ? 2 : 1);
    }
    if (infoVisible) {
        // The info page is full-screen — no saved frame needed.
        drawInfoScreen(vbatMv, deltaMv, haveDelta);
        Serial.println("updating panel (takes ~20-30 s)...");
        epaper.update();
        Serial.println("done");
    } else if (isToggleInfo) {
        // Leaving the info page: restore the photo.
        if (loadFrame()) {
            Serial.println("updating panel (takes ~20-30 s)...");
            epaper.update();
            Serial.println("done");
        } else {
            doFetchCycle(vbatMv, deltaMv, haveDelta);
        }
    }
    // Hold toggled while the photo is showing: LED feedback only.
}

void setup() {
    // Instant acknowledgment: blink before anything else so a button press
    // gets feedback in ~0.5 s (the panel itself takes ~30 s to change).
    // 1 blink = new picture, 2 = info page, 3 = pin/freeze.
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
    infoVisible = prefs.getBool("info", false);
    held = prefs.getBool("held", false);
    // TZ env doesn't survive deep sleep: without this, times rendered on
    // non-fetch wakes (info/pin toggles) come out as UTC. Fetch wakes
    // overwrite it with a freshly detected offset in syncClock().
    applyUtcOffset(prefs.getLong("tzOff", 0));

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    uint64_t btnBits = (cause == ESP_SLEEP_WAKEUP_EXT1)
                           ? esp_sleep_get_ext1_wakeup_status() : 0;

    // Held timer wake: skip everything, don't even touch the panel. The
    // GPIO holds from the previous sleep are still latched, so this must
    // run before the hold-release below.
    if (cause == ESP_SLEEP_WAKEUP_TIMER && held) quickSleep(); // no return


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

    if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");

    epaper.begin();
    applyOrientation(); // settings.rotation; UI + dither target follow

    // Hold BTN_NEW_PIC through power-on (still held after the 2 s boot
    // delay) to forget saved wifi. Gated on the power-on wake cause:
    // button wakes only pay a 200 ms boot delay, so a moderately long
    // press could still be down here — cause gating (not just release
    // timing) keeps this a power-on-only gesture.
    if (cause == ESP_SLEEP_WAKEUP_UNDEFINED &&
        digitalRead(BTN_NEW_PIC) == LOW) {
        Serial.println("KEY2 held at boot — forgetting saved wifi");
        WiFiManager wm;
        wm.resetSettings();
    }

    bool isToggleInfo = btnBits & (1ULL << BTN_INFO);
    bool isToggleHold = btnBits & (1ULL << BTN_PIN);

    if (isToggleInfo || isToggleHold)
        handleToggleWake(isToggleInfo, vbatMv, deltaMv, haveDelta);
    else
        doFetchCycle(vbatMv, deltaMv, haveDelta);

    digitalWrite(LED_PIN, HIGH);
    maybeSleep(); // deep sleep — or return, in dev mode, and run loop()
}

// Debounced falling-edge press: returns true once per physical press.
static bool pressed(uint8_t pin) {
    if (digitalRead(pin) != LOW) return false;
    delay(30);
    if (digitalRead(pin) != LOW) return false;
    while (digitalRead(pin) == LOW) delay(10);
    return true;
}

// Only runs in dev mode (USB host attached): the port stays up for
// instant flashing, buttons are polled instead of EXT1-woken, and the
// hourly photo cadence still applies. Host gone -> normal deep sleep.
void loop() {
    if (!usbHostPresent()) {
        Serial.println("usb host gone — leaving dev mode");
        goToSleep(); // never returns
    }

    bool info = pressed(BTN_INFO);
    bool pin = !info && pressed(BTN_PIN);
    bool newPic = !info && !pin && pressed(BTN_NEW_PIC);

    bool fetchDue = false;
    if (!held) {
        time_t lastFetch = (time_t)prefs.getULong("lastEpoch", 0);
        fetchDue = time(nullptr) - lastFetch >= (time_t)settings.sleepSecs;
    }

    if (info || pin || newPic || fetchDue) {
        digitalWrite(LED_PIN, LOW);
        int32_t deltaMv;
        bool haveDelta;
        int32_t vbatMv = readBatteryWithDelta(deltaMv, haveDelta);
        if (info || pin)
            handleToggleWake(info, vbatMv, deltaMv, haveDelta);
        else
            doFetchCycle(vbatMv, deltaMv, haveDelta);
        digitalWrite(LED_PIN, HIGH);
    }

    delay(50);
}
