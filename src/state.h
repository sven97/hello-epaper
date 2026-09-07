#pragma once
#include <Preferences.h>

// Shared persistent state, defined in main.cpp.
// NVS namespace "frame": held / lastEpoch / wifiSsid / wifiRssi / tzOff /
// lastIp,
// plus the settings keys (see settings.cpp).
extern Preferences prefs;
extern bool held; // pin/freeze: timer wakes skip fetching
extern int32_t lastVbatMv; // most recent battery read (RTC-persisted, main.cpp)

// Auto firmware update state (RTC-persisted, defined in main.cpp). Read by
// the /debug page; written by the OTA check + boot-health guard.
extern uint32_t lastOtaCheckEpoch;
extern uint32_t otaPendingBuild;
extern uint8_t otaTrialBoots;
