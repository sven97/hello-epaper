#pragma once
#include <Arduino.h>

// Runtime configuration, NVS-backed (namespace "frame"). Defaults in
// config.h reproduce the original fixed behavior exactly.
struct Settings {
    uint32_t sleepSecs;      // seconds between refreshes
    String   imageUrl;       // template: {seed} {width} {height}
    bool     quietEnabled;
    uint8_t  quietStartHour; // 0-23, window [start, end) local time
    uint8_t  quietEndHour;
    bool     tzAuto;         // true: ip-api detect; false: manual tzOff
    String   name;           // mDNS hostname, [a-z0-9-]{1,24}
    uint8_t  rotation;       // epaper.setRotation() arg, 0-3
    bool     otaEnabled;     // auto firmware update on scheduled wakes
    uint32_t otaCheckSecs;   // min interval between manifest checks
};

extern Settings settings;

void loadSettings();  // call after prefs.begin(); missing keys -> defaults
void saveSettings();  // persist every field
