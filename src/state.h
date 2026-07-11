#pragma once
#include <Preferences.h>

// Shared persistent state, defined in main.cpp.
// NVS namespace "frame": held / lastEpoch / wifiDesc / tzOff / lastIp,
// plus the settings keys (see settings.cpp).
extern Preferences prefs;
extern bool held; // pin/freeze: timer wakes skip fetching
