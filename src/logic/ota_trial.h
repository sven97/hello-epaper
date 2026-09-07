#pragma once
// App-level OTA boot-health verdict: does a just-flashed image get to
// stay, or do we roll back to the previous slot? Pure logic:
// host-testable, no Arduino deps. See
// docs/superpowers/specs/2026-09-06-auto-firmware-update-design.md.
#include <cstdint>

enum class OtaTrialVerdict : uint8_t {
    NotOnTrial, Continue, ConfirmGood, Revert
};

// Any wake counts toward the boot budget (incl. quiet-hours / pinned
// fast-exits), so a loop that never reaches a fetch still trips it.
constexpr uint8_t OTA_TRIAL_MAX_BOOTS       = 12;
// Consecutive failed doFetchCycle()s while the trial image is running.
constexpr uint8_t OTA_TRIAL_MAX_FETCH_FAILS = 3;

struct OtaTrialState {
    uint32_t pendingBuild;   // build we OTA'd to; 0 => not on trial
    uint32_t runningBuild;   // FW_BUILD_NUMBER now
    uint8_t  boots;          // wakes since the OTA reboot
    uint8_t  fetchFails;     // consecutive failed cycles since
    bool     fetchSucceeded; // a full cycle rendered a photo since
};

inline OtaTrialVerdict otaTrialVerdict(const OtaTrialState &s) {
    if (s.pendingBuild == 0) return OtaTrialVerdict::NotOnTrial;
    if (s.runningBuild != s.pendingBuild) return OtaTrialVerdict::Revert;
    if (s.fetchSucceeded) return OtaTrialVerdict::ConfirmGood;
    if (s.fetchFails >= OTA_TRIAL_MAX_FETCH_FAILS) return OtaTrialVerdict::Revert;
    if (s.boots >= OTA_TRIAL_MAX_BOOTS) return OtaTrialVerdict::Revert;
    return OtaTrialVerdict::Continue;
}
