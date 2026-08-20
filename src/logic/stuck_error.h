#pragma once
// Unattended-wake stuck-error escalation math. Pure logic: host-testable,
// no Arduino deps -- same pattern as quiet_hours.h. See
// docs/superpowers/specs/2026-08-19-wifi-reconnect-recovery-design.md.
#include <cstdint>

// 6 h: long enough that no ordinary transient outage (AP reboot, brief
// ISP blip) should trigger it, short enough that a genuinely stuck frame
// is discovered same-day rather than after a multi-day silent gap.
constexpr uint32_t STUCK_ERROR_SECS = 6UL * 3600;

// fetchFailStreak * sleepSecs is only an estimate of elapsed wall-clock
// time (it undercounts if quiet hours delayed wakes in between) -- errs
// toward waiting longer before flagging, which is the safe direction.
inline bool shouldShowStuckError(uint32_t fetchFailStreak, uint32_t sleepSecs,
                                 bool alreadyShown) {
    if (alreadyShown || fetchFailStreak == 0) return false;
    return (uint64_t)fetchFailStreak * sleepSecs >= STUCK_ERROR_SECS;
}
