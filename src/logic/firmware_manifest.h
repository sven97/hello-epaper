#pragma once
// Parser for the auto-update manifest (key=value lines). Pure logic:
// host-testable, no Arduino deps -- same pattern as url_template.h. See
// docs/superpowers/specs/2026-09-06-auto-firmware-update-design.md.
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cstdio>

struct ManifestInfo {
    uint32_t build = 0;
    char     hash[16] = {0};
    char     md5[33]  = {0};   // for the requested board key
};

namespace fwmanifest_detail {

// Copy the value for an exact `key` from line-based `body` into
// out[0..outCap). Match is anchored at the first non-space char of a
// line; the key must be followed (after optional spaces) by '='. The
// value runs to the next whitespace / EOL. Returns true on a non-empty
// value.
inline bool findValue(const char *body, const char *key,
                      char *out, size_t outCap) {
    const size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        const char *s = p;
        while (*s == ' ' || *s == '\t') s++;
        if (strncmp(s, key, klen) == 0) {
            const char *q = s + klen;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '=') {
                q++;
                while (*q == ' ' || *q == '\t') q++;
                size_t n = 0;
                while (q[n] && q[n] != '\n' && q[n] != '\r' &&
                       q[n] != ' ' && q[n] != '\t' && n + 1 < outCap) n++;
                memcpy(out, q, n);
                out[n] = '\0';
                return n > 0;
            }
        }
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return false;
}

inline bool isLowerHex32(const char *s) {
    for (int i = 0; i < 32; i++) {
        const char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return s[32] == '\0';
}

} // namespace fwmanifest_detail

// boardKey is the lower-case model, e.g. "ee02".
inline bool parseManifest(const char *body, const char *boardKey,
                          ManifestInfo &out) {
    using namespace fwmanifest_detail;
    char buf[64];

    if (!findValue(body, "build", buf, sizeof(buf))) return false;
    char *end = nullptr;
    const unsigned long b = strtoul(buf, &end, 10);
    if (end == buf || *end != '\0' || b == 0) return false;
    out.build = (uint32_t)b;

    if (findValue(body, "hash", buf, sizeof(buf)))
        snprintf(out.hash, sizeof(out.hash), "%s", buf);

    char key[24];
    snprintf(key, sizeof(key), "md5_%s", boardKey);
    if (!findValue(body, key, buf, sizeof(buf))) return false;
    for (char *c = buf; *c; c++) *c = (char)tolower((unsigned char)*c);
    if (!isLowerHex32(buf)) return false;
    memcpy(out.md5, buf, 33);
    return true;
}
