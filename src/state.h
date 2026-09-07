#pragma once
#include <Preferences.h>

// Shared persistent state, defined in main.cpp.
// NVS namespace "frame": held / lastEpoch / wifiSsid / wifiRssi / tzOff /
// lastIp / the settings keys (see settings.cpp) / the OTA trial+cadence
// keys (see ota_state.h).
extern Preferences prefs;
extern bool held; // pin/freeze: timer wakes skip fetching
extern int32_t lastVbatMv; // most recent battery read (RTC-persisted, main.cpp)
