#include "listenbrainz.h"
#include "../utils.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

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

// Percent-encode a value for use in a query string.
std::string escapeUrl(CURL* curl, const std::string& value) {
    char* escaped = curl_easy_escape(curl, value.c_str(),
                                     static_cast<int>(value.size()));
    if (!escaped)
        return {};
    std::string result(escaped);
    curl_free(escaped);
    return result;
}

} // namespace

// ── Constructor / Destructor ────────────────────────────────────────────────
ListenbrainzClient::ListenbrainzClient() {
    curl_ = curl_easy_init();
    // MusicBrainz rejects requests without a User-Agent.
    if (curl_)
        curl_easy_setopt(curl_, CURLOPT_USERAGENT,
                         "lastfm-discord-presence/0.1 ( "
                         "https://github.com/frostplexx/lastfm-discord-presence )");
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
    r.status = httpCode;
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

    // Album art via Cover Art Archive. The release MBID can arrive from the
    // submitting client (additional_info) or from ListenBrainz's own mapping
    // (mbid_mapping, which also carries the CAA image id).
    std::string releaseMbid;
    std::string caaReleaseMbid;
    long long caaId = 0;
    for (const char* section : {"mbid_mapping", "additional_info"}) {
        if (!tm.contains(section) || !tm[section].is_object())
            continue;
        const auto& obj = tm[section];
        if (releaseMbid.empty() && obj.contains("release_mbid") &&
            obj["release_mbid"].is_string())
            releaseMbid = obj["release_mbid"].get<std::string>();
        if (caaReleaseMbid.empty() && obj.contains("caa_release_mbid") &&
            obj["caa_release_mbid"].is_string())
            caaReleaseMbid = obj["caa_release_mbid"].get<std::string>();
        if (caaId == 0 && obj.contains("caa_id") &&
            obj["caa_id"].is_number_integer())
            caaId = obj["caa_id"].get<long long>();
    }

    // A CAA id gives a direct archive.org URL and skips the 307 redirect that
    // coverartarchive.org issues.
    std::string imageUrl;
    if (!caaReleaseMbid.empty() && caaId > 0) {
        imageUrl = "https://archive.org/download/mbid-" + caaReleaseMbid
                 + "/mbid-" + caaReleaseMbid + "-"
                 + std::to_string(caaId) + "_thumb250.jpg";
    } else if (!releaseMbid.empty()) {
        imageUrl = "https://coverartarchive.org/release/" + releaseMbid
                 + "/front-250";
    } else if (!album.empty()) {
        imageUrl = LookupCoverArt(artist, album);
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

// ── MusicBrainz album art lookup ────────────────────────────────────────────
std::string ListenbrainzClient::LookupCoverArt(const std::string& artist,
                                               const std::string& album) {
    const std::string cacheKey = artist + "\n" + album;
    auto cached = artCache_.find(cacheKey);
    if (cached != artCache_.end())
        return cached->second;

    // MusicBrainz allows one request per second. Space these lookups out; one
    // happens per previously unseen album.
    auto waitForRateLimit = [this] {
        auto now = std::chrono::steady_clock::now();
        auto nextAllowed = lastMbRequest_ + std::chrono::seconds(1);
        if (now < nextAllowed)
            std::this_thread::sleep_until(nextAllowed);
        lastMbRequest_ = std::chrono::steady_clock::now();
    };

    std::string query = "release:\"" + album + "\" AND artist:\"" + artist + "\"";
    waitForRateLimit();
    auto search = HttpGet("https://musicbrainz.org/ws/2/release?query="
                          + escapeUrl(curl_, query) + "&fmt=json&limit=1");
    if (!search.ok)
        return {}; // transient, retry on the next poll

    json root;
    try {
        root = json::parse(search.body);
    } catch (const json::parse_error&) {
        return {};
    }

    if (!root.contains("releases") || !root["releases"].is_array())
        return {};
    if (root["releases"].empty()) {
        artCache_[cacheKey] = {}; // definitive: no matching release
        return {};
    }

    std::string releaseMbid = root["releases"][0].value("id", "");
    if (releaseMbid.empty())
        return {};

    // Cover Art Archive answers with a 307 to archive.org, which Discord's
    // proxy does not follow. Resolve the chain here and pass the final URL.
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
    auto art = HttpGet("https://coverartarchive.org/release/" + releaseMbid
                       + "/front-250");
    curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 0L);

    std::string imageUrl;
    if (art.ok) {
        char* effectiveUrl = nullptr;
        curl_easy_getinfo(curl_, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
        if (effectiveUrl)
            imageUrl = effectiveUrl;
    } else if (art.status != 404) {
        return {}; // transient, retry on the next poll
    }

    artCache_[cacheKey] = imageUrl; // 404 means no art exists: cache that too
    return imageUrl;
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
