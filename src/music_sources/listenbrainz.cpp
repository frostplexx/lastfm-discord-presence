#include "listenbrainz.h"
#include "../utils.h"

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
HttpResult ListenbrainzClient::HttpGet(const std::string& url) {
    HttpResult r;
    if (!curl_) {
        r.error = "curl not initialized";
        return r;
    }

    std::string response;
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        r.error = "HTTP error: " + std::string(curl_easy_strerror(res));
        return r;
    }

    long httpCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        r.error = "HTTP " + std::to_string(httpCode) + ": "
                + oneLine(response);
        return r;
    }

    r.ok = true;
    r.body = std::move(response);
    return r;
}

// ── NowPlaying ──────────────────────────────────────────────────────────────
SourceResult ListenbrainzClient::NowPlaying(const std::string& user) {
    std::string url = "https://api.listenbrainz.org/1/user/" + user
                    + "/playing-now";

    auto r = HttpGet(url);
    if (!r.ok)
        return {SourceResultKind::Error, {}, std::move(r.error)};

    // Parse JSON
    json root;
    try {
        root = json::parse(r.body);
    } catch (const json::parse_error& e) {
        return {SourceResultKind::Error, {},
                "JSON parse error: " + std::string(e.what())};
    }

    // Navigate to payload
    if (!root.contains("payload") || !root["payload"].is_object())
        return {}; // Idle

    auto& payload = root["payload"];

    // Check if there's anything playing
    bool playingNow = payload.value("playing_now", false);
    if (!playingNow)
        return {}; // Idle

    if (!payload.contains("listens") || !payload["listens"].is_array() ||
        payload["listens"].empty())
        return {}; // Idle

    auto& listen = payload["listens"][0];
    if (!listen.contains("track_metadata") ||
        !listen["track_metadata"].is_object())
        return {}; // Idle

    auto& tm = listen["track_metadata"];

    std::string artist = tm.value("artist_name", "");
    std::string name   = tm.value("track_name", "");
    std::string album  = tm.value("release_name", "");

    if (artist.empty() || name.empty())
        return {SourceResultKind::Error, {}, "incomplete track data"};

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

    return {SourceResultKind::Playing, std::move(t), {}};
}

// ── ListenbrainzSource ───────────────────────────────────────────────────────
ListenbrainzSource::ListenbrainzSource(std::string user)
    : user_(std::move(user)) {}

SourceResult ListenbrainzSource::NowPlaying() {
    if (InBackoff())
        return {}; // quiet skip while backing off
    return CheckedResult(client_.NowPlaying(user_), "[listenbrainz]");
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
