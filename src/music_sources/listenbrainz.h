#pragma once

#include <string>
#include <optional>

#include "music_source.h"
#include "track.h"

// Minimal ListenBrainz API client — queries a user's currently playing track
// via the public listening-now endpoint. No API key required.
class ListenbrainzClient {
public:
    ListenbrainzClient();
    ~ListenbrainzClient();

    // Query the user's currently playing track.
    // Playing: a track is active. Idle: nothing playing.
    // Error: the request failed (timeout, HTTP error, bad response).
    SourceResult NowPlaying(const std::string& user);

private:
    // Non-copyable
    ListenbrainzClient(const ListenbrainzClient&) = delete;
    ListenbrainzClient& operator=(const ListenbrainzClient&) = delete;

    HttpResult HttpGet(const std::string& url);

    void* curl_{nullptr};
};

// MusicSource adapter around ListenbrainzClient for the generic poll loop.
class ListenbrainzSource : public MusicSource {
public:
    ListenbrainzSource(std::string user);

    SourceResult NowPlaying() override;
    SourceBranding Branding() const override;

private:
    ListenbrainzClient client_;
    std::string user_;
};
