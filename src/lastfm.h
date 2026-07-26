#pragma once

#include <string>
#include <optional>

#include "music_source.h"
#include "track.h"

class LastfmClient {
public:
    LastfmClient();
    ~LastfmClient();

    // Query the user's currently scrobbling track.
    // Returns nullopt if nothing is playing right now or on error.
    std::optional<Track> NowPlaying(const std::string& apiKey,
                                    const std::string& user);

    // Fetch track duration from track.getInfo.
    // Returns duration in seconds, or nullopt on failure.
    std::optional<int> GetTrackDuration(const std::string& apiKey,
                                        const std::string& artist,
                                        const std::string& track);

    // HTTP POST with form-urlencoded body. Reuses the internal curl handle.
    std::optional<std::string> HttpPost(const std::string& url,
                                        const std::string& formData);

private:
    // Non-copyable
    LastfmClient(const LastfmClient&) = delete;
    LastfmClient& operator=(const LastfmClient&) = delete;

    // Shared HTTP GET helper
    std::optional<std::string> HttpGet(const std::string& url);

    // CURL easy handle (RAII via constructor/destructor)
    void* curl_{nullptr};
};

// MusicSource adapter around LastfmClient for the generic poll loop.
class LastfmSource : public MusicSource {
public:
    LastfmSource(std::string apiKey, std::string user);

    std::optional<Track> NowPlaying() override;
    void FillDuration(Track& t) override;
    SourceBranding Branding() const override;

private:
    LastfmClient client_;
    std::string apiKey_;
    std::string user_;
};
