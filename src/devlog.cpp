#include "devlog.h"
#include "logic/ring_log.h"

namespace {
constexpr size_t DEVLOG_CAPACITY = 8192;
RingLog ring(DEVLOG_CAPACITY);
} // namespace

DevLog devLog;

size_t DevLog::write(uint8_t b) { return write(&b, 1); }

size_t DevLog::write(const uint8_t *buffer, size_t size) {
    size_t written = Serial.write(buffer, size);
    ring.append(buffer, size);
    return written;
}

String DevLog::snapshot() const {
    std::string s = ring.snapshot();
    String out;
    out.reserve(s.size());
    out.concat(s.data(), s.size());
    return out;
}
