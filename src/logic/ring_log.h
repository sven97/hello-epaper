#pragma once
// Fixed-capacity byte ring buffer for a rolling debug log: appends
// overwrite the oldest bytes once full; snapshot() returns bytes in
// chronological (oldest -> newest) order. Pure logic: host-testable, no
// Arduino deps -- same pattern as quiet_hours.h / battery_curve.h.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

class RingLog {
public:
    explicit RingLog(size_t capacity)
        : cap_(capacity), buf_(capacity ? new uint8_t[capacity] : nullptr) {}
    ~RingLog() { delete[] buf_; }
    RingLog(const RingLog &) = delete;
    RingLog &operator=(const RingLog &) = delete;

    void append(const uint8_t *data, size_t len) {
        if (cap_ == 0 || len == 0) return;
        if (len >= cap_) {
            // Only the tail of this one write fits -- start the ring
            // fresh from offset 0 with exactly the last cap_ bytes.
            memcpy(buf_, data + (len - cap_), cap_);
            head_ = 0;
            filled_ = true;
            return;
        }
        size_t firstPart = cap_ - head_;
        if (firstPart >= len) {
            memcpy(buf_ + head_, data, len);
        } else {
            memcpy(buf_ + head_, data, firstPart);
            memcpy(buf_, data + firstPart, len - firstPart);
        }
        size_t newHead = head_ + len;
        if (newHead >= cap_) filled_ = true;
        head_ = newHead % cap_;
    }

    std::string snapshot() const {
        if (!filled_)
            return std::string(reinterpret_cast<const char *>(buf_), head_);
        std::string out;
        out.reserve(cap_);
        out.append(reinterpret_cast<const char *>(buf_ + head_), cap_ - head_);
        out.append(reinterpret_cast<const char *>(buf_), head_);
        return out;
    }

    size_t capacity() const { return cap_; }

private:
    size_t cap_;
    uint8_t *buf_;
    size_t head_ = 0;
    bool filled_ = false;
};
