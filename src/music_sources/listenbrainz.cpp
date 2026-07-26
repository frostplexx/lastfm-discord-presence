#include "listenbrainz.h"

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

// libcurl write callback
size_t WriteCb(void* data, size_t size, size_t nmemb, void* userp) {
    auto* buf = static_cast<std::string*>(userp);
    buf->append(static_cast<const char*>(data), size * nmemb);
    return size * nmemb;
}

// Build a ListenBrainz user profile URL.
std::string userUrl(const std::string& user) {
    return "https://listenbrainz.org/user/" + user;
}

} // namespace

// ── Constructor / Destructor ────────────────────────────────────────────────
ListenbrainzClient::ListenbrainzClient() {
    curl_ = curl_easy_init();
}

ListenbrainzClient::~ListenbrainzClient() {
    if (curl_)
        curl_easy_cleanup(curl_);
}

// ── Shared HTTP GET ─────────────────────────────────────────────────────────
std::optional<std::string> ListenbrainzClient::HttpGet(const std::string& url) {
    if (!curl_) {
        std::cerr << "[listenbrainz] curl not initialized" << std::endl;
        return std::nullopt;
    }

    std::string response;
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        std::cerr << "[listenbrainz] HTTP error: " << curl_easy_strerror(res)
                  << std::endl;
        return std::nullopt;
    }

    long httpCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        std::cerr << "[listenbrainz] HTTP " << httpCode << ": "
                  << response.substr(0, 256) << std::endl;
        return std::nullopt;
    }

    return response;
}

// ── NowPlaying ──────────────────────────────────────────────────────────────
std::optional<Track> ListenbrainzClient::NowPlaying(const std::string& user) {
    std::string url = "https://api.listenbrainz.org/1/user/" + user
                    + "/playing-now";

    auto body = HttpGet(url);
    if (!body)
        return std::nullopt;

    // Parse JSON
    json root;
    try {
        root = json::parse(*body);
    } catch (const json::parse_error& e) {
        std::cerr << "[listenbrainz] JSON parse error: " << e.what()
                  << std::endl;
        return std::nullopt;
    }

    // Navigate to payload
    if (!root.contains("payload") || !root["payload"].is_object())
        return std::nullopt;

    auto& payload = root["payload"];

    // Check if there's anything playing
    bool playingNow = payload.value("playing_now", false);
    if (!playingNow)
        return std::nullopt;

    if (!payload.contains("listens") || !payload["listens"].is_array() ||
        payload["listens"].empty())
        return std::nullopt;

    auto& listen = payload["listens"][0];
    if (!listen.contains("track_metadata") ||
        !listen["track_metadata"].is_object())
        return std::nullopt;

    auto& tm = listen["track_metadata"];

    std::string artist = tm.value("artist_name", "");
    std::string name   = tm.value("track_name", "");
    std::string album  = tm.value("release_name", "");

    if (artist.empty() || name.empty()) {
        std::cerr << "[listenbrainz] incomplete track data" << std::endl;
        return std::nullopt;
    }

    // Try album art via CoverArtArchive if we have a release MBID.
    std::string imageUrl;
    if (tm.contains("additional_info") && tm["additional_info"].is_object()) {
        auto& ai = tm["additional_info"];
        if (ai.contains("release_mbid")) {
            std::string mbid = ai["release_mbid"].get<std::string>();
            if (!mbid.empty()) {
                imageUrl = "https://coverartarchive.org/release/" + mbid
                         + "/front-250";
            }
        }
    }

    Track t;
    t.artist    = std::move(artist);
    t.name      = std::move(name);
    t.album     = std::move(album);
    t.imageUrl  = std::move(imageUrl);
    t.trackUrl  = userUrl(user);
    // No dedicated per-artist or per-album pages on ListenBrainz.
    t.artistUrl = {};
    t.albumUrl  = {};

    return t;
}

// ── ListenbrainzSource ───────────────────────────────────────────────────────
ListenbrainzSource::ListenbrainzSource(std::string user)
    : user_(std::move(user)) {}

std::optional<Track> ListenbrainzSource::NowPlaying() {
    return client_.NowPlaying(user_);
}

SourceBranding ListenbrainzSource::Branding() const {
    // No bundled ListenBrainz icon asset — leave smallImageUrl empty so the
    // small image overlay is simply skipped (see presence.cpp).
    return SourceBranding{
        "ListenBrainz",
        "",
        userUrl(user_),
        "View on ListenBrainz",
    };
}
