#pragma once
#include <Preferences.h>

// Shared persistent state, defined in main.cpp.
// NVS namespace "frame": held / lastEpoch / wifiDesc / tzOff / lastIp
// plus the settings keys (see settings.cpp).
extern Preferences prefs;
extern bool infoVisible; // full-screen info page vs the photo
extern bool held;        // pin/freeze: timer wakes skip fetching
