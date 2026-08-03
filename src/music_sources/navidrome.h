#pragma once

#include <optional>
#include <string>

#include "music_source.h"
#include "track.h"

// Minimal Subsonic API client, scoped to what we need: querying the
// server-wide "now playing" list via getNowPlaying.view.
//
// Navidrome's own "Now Playing" admin panel is powered by this same
// endpoint, and (as of this writing) only an admin account reliably sees
// every user's session there — a non-admin generally only sees their own.
// Since the point of this client is to watch an arbitrary NAVIDROME_USERNAME
// (which may not be the account we authenticate as), admin credentials are
// required.
class NavidromeClient {
public:
    NavidromeClient(std::string host, std::string adminUser, std::string adminPassword);
    ~NavidromeClient();

    // Query the server-wide now-playing list and return the entry matching
    // `targetUsername`, if any.
    // Playing: that user has a track active. Idle: user isn't playing
    // anything. Error: the request failed (timeout, HTTP error, bad response).
    SourceResult NowPlaying(const std::string& targetUsername);

private:
    // Non-copyable
    NavidromeClient(const NavidromeClient&) = delete;
    NavidromeClient& operator=(const NavidromeClient&) = delete;

    HttpResult HttpGet(const std::string& url);
    std::string escape(const std::string& raw) const;

    // Subsonic token auth: t = md5(password + salt), s = salt.
    // Keeps the admin password itself off the wire.
    std::string authQuery() const;

    std::string host_;           // e.g. https://music.example.com (no trailing slash)
    std::string adminUser_;
    std::string adminPassword_;
    void* curl_{nullptr};
};

// MusicSource adapter around NavidromeClient for the generic poll loop.
class NavidromeSource : public MusicSource {
public:
    NavidromeSource(std::string host, std::string adminUser,
                    std::string adminPassword, std::string targetUsername);

    SourceResult NowPlaying() override;
    // Navidrome's getNowPlaying.view already includes duration; nothing to fill.
    SourceBranding Branding() const override;

private:
    NavidromeClient client_;
    std::string host_;
    std::string targetUsername_;
};
