#include "navidrome.h"
#include "md5.h"

#include <chrono>
#include <iostream>
#include <random>
#include <sstream>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {

const char* kClientName = "lastfm-discord-presence";
const char* kApiVersion = "1.16.1"; // oldest version that supports getNowPlaying comfortably

size_t WriteCb(void* data, size_t size, size_t nmemb, void* userp) {
    auto* buf = static_cast<std::string*>(userp);
    buf->append(static_cast<const char*>(data), size * nmemb);
    return size * nmemb;
}

std::string randomSalt() {
    static thread_local std::mt19937_64 rng(
        std::chrono::steady_clock::now().time_since_epoch().count() ^
        std::random_device{}());
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uniform_int_distribution<int> dist(0, 61);

    std::string s;
    s.reserve(16);
    for (int i = 0; i < 16; ++i)
        s.push_back(alphabet[dist(rng)]);
    return s;
}

// Strip a trailing slash so we can consistently do host_ + "/rest/...".
std::string normalizeHost(std::string host) {
    while (!host.empty() && host.back() == '/')
        host.pop_back();
    return host;
}

} // namespace

// ── Constructor / Destructor ────────────────────────────────────────────────
NavidromeClient::NavidromeClient(std::string host, std::string adminUser,
                                 std::string adminPassword)
    : host_(normalizeHost(std::move(host))),
      adminUser_(std::move(adminUser)),
      adminPassword_(std::move(adminPassword)) {
    curl_ = curl_easy_init();
}

NavidromeClient::~NavidromeClient() {
    if (curl_)
        curl_easy_cleanup(curl_);
}

std::string NavidromeClient::escape(const std::string& raw) const {
    if (!curl_)
        return raw;
    char* escaped = curl_easy_escape(static_cast<CURL*>(curl_), raw.c_str(),
                                      static_cast<int>(raw.size()));
    std::string result = escaped ? escaped : raw;
    if (escaped)
        curl_free(escaped);
    return result;
}

std::string NavidromeClient::authQuery() const {
    std::string salt = randomSalt();
    std::string token = Md5Hex(adminPassword_ + salt);

    std::ostringstream q;
    q << "u=" << escape(adminUser_)
      << "&t=" << token
      << "&s=" << salt
      << "&v=" << kApiVersion
      << "&c=" << kClientName
      << "&f=json";
    return q.str();
}

// ── Shared HTTP GET ─────────────────────────────────────────────────────────
std::optional<std::string> NavidromeClient::HttpGet(const std::string& url) {
    if (!curl_) {
        std::cerr << "[navidrome] curl not initialized" << std::endl;
        return std::nullopt;
    }

    std::string response;
    curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCb);
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 10L);

    CURLcode res = curl_easy_perform(curl_);
    if (res != CURLE_OK) {
        std::cerr << "[navidrome] HTTP error: " << curl_easy_strerror(res)
                  << std::endl;
        return std::nullopt;
    }

    long httpCode = 0;
    curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        std::cerr << "[navidrome] HTTP " << httpCode << ": "
                  << response.substr(0, 256) << std::endl;
        return std::nullopt;
    }

    return response;
}

// ── NowPlaying ──────────────────────────────────────────────────────────────
std::optional<Track> NavidromeClient::NowPlaying(const std::string& targetUsername) {
    std::string url = host_ + "/rest/getNowPlaying.view?" + authQuery();

    auto body = HttpGet(url);
    if (!body)
        return std::nullopt;

    json root;
    try {
        root = json::parse(*body);
    } catch (const json::parse_error& e) {
        std::cerr << "[navidrome] JSON parse error: " << e.what() << std::endl;
        return std::nullopt;
    }

    if (!root.contains("subsonic-response"))
        return std::nullopt;
    auto& resp = root["subsonic-response"];

    if (resp.value("status", "") != "ok") {
        if (resp.contains("error") && resp["error"].is_object()) {
            auto& err = resp["error"];
            std::cerr << "[navidrome] API error " << err.value("code", 0)
                       << ": " << err.value("message", "unknown") << std::endl;
        } else {
            std::cerr << "[navidrome] API error: unexpected response"
                      << std::endl;
        }
        return std::nullopt;
    }

    if (!resp.contains("nowPlaying") || !resp["nowPlaying"].contains("entry"))
        return std::nullopt;

    for (auto& entry : resp["nowPlaying"]["entry"]) {
        if (entry.value("username", "") != targetUsername)
            continue;

        Track t;
        t.artist = entry.value("artist", "");
        t.name = entry.value("title", "");
        t.album = entry.value("album", "");
        // Navidrome reports duration in whole seconds (unlike Last.fm's ms).
        t.durationSec = entry.value("duration", 0);

        std::string coverArt = entry.value("coverArt", "");
        if (!coverArt.empty()) {
            t.imageUrl = host_ + "/rest/getCoverArt.view?id=" +
                         escape(coverArt) + "&size=500&" + authQuery();
        }

        std::string albumId = entry.value("albumId", "");
        std::string artistId = entry.value("artistId", "");
        if (!albumId.empty())
            t.albumUrl = host_ + "/app/#/album/" + albumId + "/show";
        if (!artistId.empty())
            t.artistUrl = host_ + "/app/#/artist/" + artistId + "/show";
        // No dedicated per-track page in the Navidrome web app; link the
        // album instead, falling back to the server root.
        t.trackUrl = !t.albumUrl.empty() ? t.albumUrl : host_;

        if (t.artist.empty() || t.name.empty())
            return std::nullopt;

        return t;
    }

    return std::nullopt; // target user isn't in the now-playing list
}

// ── NavidromeSource ──────────────────────────────────────────────────────────
NavidromeSource::NavidromeSource(std::string host, std::string adminUser,
                                 std::string adminPassword,
                                 std::string targetUsername)
    : client_(host, std::move(adminUser), std::move(adminPassword)),
      host_(normalizeHost(std::move(host))),
      targetUsername_(std::move(targetUsername)) {}

std::optional<Track> NavidromeSource::NowPlaying() {
    return client_.NowPlaying(targetUsername_);
}

SourceBranding NavidromeSource::Branding() const {
    // No bundled Navidrome icon asset — leave smallImageUrl empty so the
    // small image overlay is simply skipped (see presence.cpp).
    return SourceBranding{
        "Navidrome",
        "",
        host_,
        "View on Navidrome",
    };
}
