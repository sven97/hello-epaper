#pragma once
#include <Arduino.h>

// ---- Buttons -------------------------------------------------------------
// Physical buttons, named as silkscreened on the XIAO EE0x boards
// (KEY1..KEY3) — pins are identical across EE02/EE03/EE04/EE05.
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

// Per-board overrides, set via build_flags in platformio.ini (same idiom as
// BOARD_SCREEN_COMBO). Undefined -> EE02 defaults, unchanged from before.
#ifndef DEFAULT_DEVICE_NAME_STR
#define DEFAULT_DEVICE_NAME_STR "ee02"
#endif
inline const char *DEFAULT_DEVICE_NAME = DEFAULT_DEVICE_NAME_STR;

#ifndef DEFAULT_ROTATION_VALUE
#define DEFAULT_ROTATION_VALUE 0 // portrait
#endif
constexpr uint8_t DEFAULT_ROTATION = DEFAULT_ROTATION_VALUE;

#ifndef AP_NAME_STR
#define AP_NAME_STR "EE02-Setup"
#endif
inline const char *AP_NAME = AP_NAME_STR;

// Fixed board model for the status screen's subtitle -- distinct from
// DEFAULT_DEVICE_NAME, which is just the default mDNS hostname and can be
// renamed by the user in settings; the board model can't.
#ifndef BOARD_MODEL_STR
#define BOARD_MODEL_STR "EE02"
#endif
inline const char *BOARD_MODEL = BOARD_MODEL_STR;

inline const char *TZ_API_URL =
    "http://ip-api.com/json?fields=status,timezone,offset";

// Any epoch below this means the clock was never NTP-synced (Sep 2020).
constexpr time_t CLOCK_SANE_EPOCH = 1600000000;
