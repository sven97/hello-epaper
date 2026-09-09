#pragma once
#include <Arduino.h>

// ---- Buttons -------------------------------------------------------------
// Physical buttons, named as silkscreened on the XIAO EE02 board (KEY1..KEY3).
constexpr uint8_t BTN_KEY1 = 2;
constexpr uint8_t BTN_KEY2 = 3;
constexpr uint8_t BTN_KEY3 = 5;

// Function assignment — the one place to remap button behavior.
constexpr uint8_t BTN_INFO    = BTN_KEY1; // toggle full-screen info page
constexpr uint8_t BTN_NEW_PIC = BTN_KEY2; // fetch new picture
constexpr uint8_t BTN_PIN     = BTN_KEY3; // pin/freeze current picture

constexpr uint64_t BUTTON_WAKE_MASK =
    (1ULL << BTN_KEY1) | (1ULL << BTN_KEY2) | (1ULL << BTN_KEY3);

// ---- Other pins ----------------------------------------------------------
constexpr uint8_t LED_PIN = 21;          // active-LOW
constexpr uint8_t BATTERY_ADC_PIN = 1;   // A0, via /2 divider
constexpr uint8_t BATTERY_EN_PIN = 6;    // HIGH enables the divider
constexpr uint8_t EPAPER_EN_PIN = 43;    // panel power enable

// ---- Behavior defaults (runtime values live in settings.h / NVS) --------
constexpr uint32_t DEFAULT_SLEEP_SECONDS = 60 * 60; // 1 hour
inline const char *DEFAULT_IMAGE_URL =
    "https://images.weserv.nl/?url=picsum.photos/{width}/{height}"
    "%3Frandom%3D{seed}&output=jpg";

// ---- Auto firmware update ----------------------------------------------
// Fixed rolling release published by .github/workflows/ci.yml on every
// push to main; assets are clobbered in place so these URLs are stable.
inline const char *OTA_RELEASE_BASE_URL =
    "https://github.com/sven97/hello-epaper/releases/download/firmware-latest/";
inline const char *OTA_MANIFEST_URL =
    "https://github.com/sven97/hello-epaper/releases/download/firmware-latest/manifest.txt";

constexpr bool     DEFAULT_OTA_ENABLED     = true;         // opt-out
constexpr uint32_t OTA_CHECK_INTERVAL_SECS = 24 * 60 * 60; // fixed: daily

// Default mDNS hostname -- the user can rename the device in settings, so
// this only applies on fresh boot / factory reset.
inline const char *DEFAULT_DEVICE_NAME = "paperframe";

// Photo-display orientation default. The panel is portrait-native, so
// rotation 0 is portrait.
constexpr uint8_t DEFAULT_ROTATION = 0;

// Soft-AP SSID shown during onboarding.
inline const char *AP_NAME = "Paperframe-Setup";

// Hardware model, shown in the status screen's device zone -- distinct
// from DEFAULT_DEVICE_NAME (a renamable hostname) and from the "Paperframe"
// product name.
inline const char *BOARD_MODEL = "EE02";

// The one supported panel: XIAO EE02, 13.3" Spectra-6 colour, portrait-native.
constexpr int      PANEL_W    = 1200;
constexpr int      PANEL_H    = 1600;
inline const char *PANEL_DESC = "13.3\" Spectra 6";

inline const char *TZ_API_URL =
    "http://ip-api.com/json?fields=status,timezone,offset";

// Any epoch below this means the clock was never NTP-synced (Sep 2020).
constexpr time_t CLOCK_SANE_EPOCH = 1600000000;
