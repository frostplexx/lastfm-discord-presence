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
    // Returns nullopt if nothing is playing right now or on error.
    std::optional<Track> NowPlaying(const std::string& user);

private:
    // Non-copyable
    ListenbrainzClient(const ListenbrainzClient&) = delete;
    ListenbrainzClient& operator=(const ListenbrainzClient&) = delete;

    std::optional<std::string> HttpGet(const std::string& url);

    void* curl_{nullptr};
};

// MusicSource adapter around ListenbrainzClient for the generic poll loop.
class ListenbrainzSource : public MusicSource {
public:
    ListenbrainzSource(std::string user);

    std::optional<Track> NowPlaying() override;
    SourceBranding Branding() const override;

private:
    ListenbrainzClient client_;
    std::string user_;
};
