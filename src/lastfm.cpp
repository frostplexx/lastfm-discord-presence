#include "lastfm.h"

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ── libcurl write callback ──────────────────────────────────────────────────
static size_t WriteCb(void* data, size_t size, size_t nmemb, void* userp) {
    auto* buf = static_cast<std::string*>(userp);
    buf->append(static_cast<const char*>(data), size * nmemb);
    return size * nmemb;
}

// ── Constructor / Destructor ────────────────────────────────────────────────
LastfmClient::LastfmClient() {
    curl_ = curl_easy_init();
}

LastfmClient::~LastfmClient() {
    if (curl_)
        curl_easy_cleanup(curl_);
}

// ── Shared HTTP GET ─────────────────────────────────────────────────────────
std::optional<std::string> LastfmClient::HttpGet(const std::string& url) {
    if (!curl_) {
        std::cerr << "[lastfm] curl not initialized" << std::endl;
        return std::nullopt;
    }

    std::string response;
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        std::cerr << "[lastfm] HTTP error: " << curl_easy_strerror(res)
                  << std::endl;
        return std::nullopt;
    }

    long httpCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        std::cerr << "[lastfm] HTTP " << httpCode << ": "
                  << response.substr(0, 256) << std::endl;
        return std::nullopt;
    }

    return response;
}

// ── Helper: construct last.fm artist URL from artist name ───────────────────
static std::string artistUrl(const std::string& artist) {
    std::ostringstream s;
    // last.fm uses percent-encoded UTF-8 in URL paths
    // We do simple percent-encoding of special chars
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

// ── Helper: extract image URL for a given size from track JSON ──────────────
static std::string extractImage(const json& track, const std::string& size) {
    if (!track.contains("image") || !track["image"].is_array())
        return {};
    for (auto& img : track["image"]) {
        if (img.value("size", "") == size)
            return img.value("#text", "");
    }
    return {};
}

// ── NowPlaying ──────────────────────────────────────────────────────────────
std::optional<Track> LastfmClient::NowPlaying(const std::string& apiKey,
                                              const std::string& user) {
    std::ostringstream url;
    url << "https://ws.audioscrobbler.com/2.0/"
        << "?method=user.getrecenttracks"
        << "&user=" << curl_easy_escape(curl_, user.c_str(), user.size())
        << "&api_key=" << curl_easy_escape(curl_, apiKey.c_str(), apiKey.size())
        << "&limit=1"
        << "&format=json";

    auto body = HttpGet(url.str());
    if (!body)
        return std::nullopt;

    // Parse JSON
    json root;
    try {
        root = json::parse(*body);
    } catch (const json::parse_error& e) {
        std::cerr << "[lastfm] JSON parse error: " << e.what() << std::endl;
        return std::nullopt;
    }

    // Check for API error
    if (root.contains("error")) {
        int code = root["error"].get<int>();
        std::string msg = root.value("message", "unknown");
        std::cerr << "[lastfm] API error " << code << ": " << msg << std::endl;
        return std::nullopt;
    }

    // Navigate to first track
    auto& rt = root["recenttracks"];
    if (rt.is_null() || !rt.contains("track") || rt["track"].empty())
        return std::nullopt;

    auto& track = rt["track"][0];

    // Check if this track is currently playing
    bool nowPlaying = false;
    if (track.contains("@attr") && track["@attr"].is_object()) {
        auto& attr = track["@attr"];
        nowPlaying = (attr.value("@attr.nowplaying", "") == "true" ||
                      attr.value("nowplaying", "") == "true");
    }
    if (!nowPlaying)
        return std::nullopt;

    Track t;
    t.artist    = track["artist"].value("#text", "");
    t.name      = track.value("name", "");
    t.album     = track["album"].value("#text", "");
    t.imageUrl  = extractImage(track, "large");
    t.trackUrl  = track.value("url", "");

    if (t.artist.empty() || t.name.empty()) {
        std::cerr << "[lastfm] incomplete track data" << std::endl;
        return std::nullopt;
    }

    return t;
}

// ── GetTrackDuration ────────────────────────────────────────────────────────
std::optional<int> LastfmClient::GetTrackDuration(const std::string& apiKey,
                                                  const std::string& artist,
                                                  const std::string& trackName) {
    std::ostringstream url;
    url << "https://ws.audioscrobbler.com/2.0/"
        << "?method=track.getInfo"
        << "&artist=" << curl_easy_escape(curl_, artist.c_str(), artist.size())
        << "&track=" << curl_easy_escape(curl_, trackName.c_str(), trackName.size())
        << "&api_key=" << curl_easy_escape(curl_, apiKey.c_str(), apiKey.size())
        << "&format=json";

    auto body = HttpGet(url.str());
    if (!body)
        return std::nullopt;

    json root;
    try {
        root = json::parse(*body);
    } catch (const json::parse_error& e) {
        std::cerr << "[lastfm] duration JSON parse error: " << e.what() << std::endl;
        return std::nullopt;
    }

    if (root.contains("error")) {
        int code = root["error"].get<int>();
        // "Track not found" (6) is common — don't spam logs for it
        if (code != 6)
            std::cerr << "[lastfm] duration API error " << code << ": "
                      << root.value("message", "") << std::endl;
        return std::nullopt;
    }

    auto& ti = root["track"];
    if (ti.is_null() || !ti.contains("duration"))
        return std::nullopt;

    // Duration can be int or string in the API
    int millis = 0;
    if (ti["duration"].is_string()) {
        millis = std::stoi(ti["duration"].get<std::string>());
    } else {
        millis = ti["duration"].get<int>();
    }
    if (millis <= 0)
        return std::nullopt;

    return millis / 1000; // return seconds
}
