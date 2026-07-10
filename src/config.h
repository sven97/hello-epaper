#pragma once
#include <Arduino.h>

// ---- Buttons -------------------------------------------------------------
// Physical buttons, named as silkscreened on the EE02 (KEY1..KEY3).
constexpr uint8_t BTN_KEY1 = 2;
constexpr uint8_t BTN_KEY2 = 3;
constexpr uint8_t BTN_KEY3 = 5;

// Function assignment — the one place to remap button behavior.
constexpr uint8_t BTN_INFO    = BTN_KEY1; // toggle full-screen info page
constexpr uint8_t BTN_NEW_PIC = BTN_KEY2; // fetch new picture (+ forget-wifi gesture at power-on)
constexpr uint8_t BTN_PIN     = BTN_KEY3; // pin/freeze current picture

constexpr uint64_t BUTTON_WAKE_MASK =
    (1ULL << BTN_KEY1) | (1ULL << BTN_KEY2) | (1ULL << BTN_KEY3);

// ---- Other pins ----------------------------------------------------------
constexpr uint8_t LED_PIN = 21;          // active-LOW
constexpr uint8_t BATTERY_ADC_PIN = 1;   // A0, via /2 divider
constexpr uint8_t BATTERY_EN_PIN = 6;    // HIGH enables the divider
constexpr uint8_t EPAPER_EN_PIN = 43;    // panel power enable

// ---- Behavior ------------------------------------------------------------
constexpr uint64_t SLEEP_SECONDS = 60 * 60; // 1 hour between refreshes
// Developer mode: when a USB host is attached, sleep this long instead so
// the serial port reappears every minute and flashing needs no button
// ritual. Chargers don't send USB frames, so wall/battery use is unaffected.
constexpr uint64_t DEV_SLEEP_SECONDS = 60;

inline const char *AP_NAME = "EE02-Setup";
inline const char *TZ_API_URL =
    "http://ip-api.com/json?fields=status,timezone,offset";

// Saved-frame file (LittleFS). Native sprite buffer: 1600*1200 px at 4 bpp.
inline const char *FRAME_PATH = "/frame.bin";
constexpr size_t FRAME_BYTES = 1600UL * 1200UL / 2;

// Any epoch below this means the clock was never NTP-synced (Sep 2020).
constexpr time_t CLOCK_SANE_EPOCH = 1600000000;
