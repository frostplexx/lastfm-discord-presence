#pragma once

#include <string>
#include <cstdint>

// Expand ~/ to $HOME in a path.
std::string expandHome(const std::string& path);

// Parse a uint64 from a decimal string.
uint64_t parseAppId(const std::string& s);

// URL-encode a string for form data (application/x-www-form-urlencoded).
std::string urlEncode(const std::string& raw);

// Build a last.fm artist page URL from an artist name.
std::string artistUrlFromName(const std::string& artist);

// Build a last.fm album page URL from artist and album names.
std::string albumUrlFromNames(const std::string& artist,
                              const std::string& album);

// Collapse a string to a single log line: strip CR/LF, cap length.
std::string oneLine(const std::string& s, size_t maxLen = 160);

// Track identity — used for change detection between polls.
struct TrackId {
    std::string artist;
    std::string name;

    bool operator==(const TrackId& o) const;
    bool operator!=(const TrackId& o) const;
};