#include "photocache.h"
#include "devlog.h"
#include "display.h"
#include "screencapture.h"
#include <LittleFS.h>

namespace {
constexpr const char *CACHE_PATH = "/last.jpg";
bool mounted = false;

bool ensureMounted() {
    if (mounted) return true;
    mounted = LittleFS.begin(true); // format on first mount
    if (!mounted) devLog.println("photocache: LittleFS mount failed");
    return mounted;
}
} // namespace

bool savePhotoCache(const uint8_t *buf, size_t len) {
    if (!ensureMounted()) return false;
    fs::File f = LittleFS.open(CACHE_PATH, "w");
    if (!f) {
        devLog.println("photocache: open for write failed");
        return false;
    }
    size_t written = f.write(buf, len);
    f.close();
    if (written != len) {
        devLog.printf("photocache: short write (%u of %u)\n",
                      (unsigned)written, (unsigned)len);
        LittleFS.remove(CACHE_PATH); // don't leave a truncated cache behind
        return false;
    }
    return true;
}

bool hasCachedPhoto() {
    if (!ensureMounted()) return false;
    fs::File f = LittleFS.open(CACHE_PATH, "r");
    if (!f) return false;
    bool nonEmpty = f.size() > 0;
    f.close();
    return nonEmpty;
}

bool streamCachedPhoto(WebServer &server) {
    if (!ensureMounted()) {
        server.send(404, "text/plain", "no cached image");
        return false;
    }
    fs::File f = LittleFS.open(CACHE_PATH, "r");
    if (!f || f.size() == 0) {
        if (f) f.close();
        server.send(404, "text/plain", "no cached image");
        return false;
    }
    server.streamFile(f, "image/jpeg");
    f.close();
    return true;
}

bool renderCachedPhoto() {
    if (!ensureMounted()) return false;
    fs::File f = LittleFS.open(CACHE_PATH, "r");
    if (!f || f.size() == 0) {
        if (f) f.close();
        return false;
    }
    size_t len = f.size();
    uint8_t *buf = (uint8_t *)ps_malloc(len);
    if (!buf) {
        f.close();
        devLog.println("photocache: PSRAM alloc failed");
        return false;
    }
    size_t readLen = f.read(buf, len);
    f.close();
    if (readLen != len) {
        free(buf);
        devLog.println("photocache: short read");
        return false;
    }
    snapshotPrevious();
    epaper.fillScreen(TFT_WHITE);
    bool ok = renderJpeg(buf, len);
    free(buf);
    return ok;
}
