#pragma once
// Server-side validation for portal form input. Pure logic: host-testable.
#include <cstdint>
#include <string>

inline bool isValidSleepSecs(uint32_t s) {
    static const uint32_t CHOICES[] = {900,  1800,  3600,  7200,
                                       14400, 28800, 43200, 86400};
    for (uint32_t c : CHOICES)
        if (s == c) return true;
    return false;
}

inline bool isValidImageUrl(const std::string &u) {
    if (u.size() < 12 || u.size() > 512) return false;
    return u.rfind("http://", 0) == 0 || u.rfind("https://", 0) == 0;
}

inline bool isValidHour(int h) { return h >= 0 && h <= 23; }

inline bool isValidDeviceName(const std::string &n) {
    if (n.empty() || n.size() > 24) return false;
    if (n.front() == '-' || n.back() == '-') return false;
    for (char c : n)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    return true;
}

inline bool isValidTzOffsetSec(long o) {
    return o >= -14L * 3600 && o <= 14L * 3600 && o % 900 == 0;
}

inline bool isValidRotation(int r) { return r >= 0 && r <= 3; }
