#pragma once
#include <Arduino.h>

// Debug log reachable over HTTP without an interactive serial-monitor
// TTY (see docs/superpowers/specs/2026-08-13-debug-visibility-design.md).
// Every Serial.print/println/printf call site in this firmware goes
// through devLog instead: it forwards to the real Serial unchanged (so
// `pio device monitor` from a real terminal is unaffected), and
// additionally keeps a rolling RAM copy that /log and /debug read back
// over HTTP. RAM-only -- does not survive deep sleep. That's intentional:
// it's only ever read while the portal is reachable, which in dev mode
// means the board is already staying awake for the whole session.
class DevLog : public Print {
public:
    size_t write(uint8_t b) override;
    size_t write(const uint8_t *buffer, size_t size) override;

    // Buffered output so far, oldest -> newest.
    String snapshot() const;
};

extern DevLog devLog;
