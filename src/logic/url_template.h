#pragma once
// Image-URL templating. Pure logic: host-testable, no Arduino deps.
#include <string>

inline void replaceAll(std::string &s, const std::string &from,
                       const std::string &to) {
    for (size_t pos = s.find(from); pos != std::string::npos;
         pos = s.find(from, pos + to.size()))
        s.replace(pos, from.size(), to);
}

// Substitute {seed}, {width}, {height}. Templates without tokens pass
// through untouched (static image URLs are valid sources).
inline std::string renderUrlTemplate(const std::string &tmpl,
                                     unsigned long seed, int width,
                                     int height) {
    std::string out = tmpl;
    replaceAll(out, "{seed}", std::to_string(seed));
    replaceAll(out, "{width}", std::to_string(width));
    replaceAll(out, "{height}", std::to_string(height));
    return out;
}
