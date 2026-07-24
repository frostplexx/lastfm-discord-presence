#include "utils.h"

#include <cstdlib>
#include <cctype>
#include <sstream>

// ── expandHome ───────────────────────────────────────────────────────────────
std::string expandHome(const std::string& path) {
    if (path.size() > 1 && path[0] == '~' && path[1] == '/') {
        const char* home = std::getenv("HOME");
        if (home)
            return std::string(home) + path.substr(1);
    }
    return path;
}

// ── parseAppId ───────────────────────────────────────────────────────────────
uint64_t parseAppId(const std::string& s) {
    return static_cast<uint64_t>(std::stoull(s));
}

// ── urlEncode ────────────────────────────────────────────────────────────────
std::string urlEncode(const std::string& raw) {
    std::ostringstream s;
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            s << c;
        else if (c == ' ')
            s << '+';
        else
            s << '%' << std::hex << std::uppercase << (int)c;
    }
    return s.str();
}

// ── artistUrlFromName ────────────────────────────────────────────────────────
std::string artistUrlFromName(const std::string& artist) {
    std::ostringstream s;
    for (unsigned char c : artist) {
        if (c <= 32 || c == 127 || c == '/' || c == '?' || c == '#' ||
            c == '&' || c == '=' || c == '%') {
            static const char hex[] = "0123456789ABCDEF";
            s << '%' << hex[c >> 4] << hex[c & 0xf];
        } else {
            s << c;
        }
    }
    return "https://www.last.fm/music/" + s.str();
}

// ── albumUrlFromNames ────────────────────────────────────────────────────────
std::string albumUrlFromNames(const std::string& artist,
                              const std::string& album) {
    if (album.empty()) return {};
    std::ostringstream s;
    auto append = [&](const std::string& raw) {
        for (unsigned char c : raw) {
            if (c <= 32 || c == 127 || c == '/' || c == '?' || c == '#' ||
                c == '&' || c == '=' || c == '%') {
                static const char hex[] = "0123456789ABCDEF";
                s << '%' << hex[c >> 4] << hex[c & 0xf];
            } else {
                s << c;
            }
        }
    };
    append(artist);
    s << '/';
    append(album);
    return "https://www.last.fm/music/" + s.str();
}

// ── TrackId ──────────────────────────────────────────────────────────────────
bool TrackId::operator==(const TrackId& o) const {
    return artist == o.artist && name == o.name;
}
bool TrackId::operator!=(const TrackId& o) const {
    return !(*this == o);
}