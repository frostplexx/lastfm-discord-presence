#pragma once

#include <string>
#include <optional>

struct Track {
    std::string artist;
    std::string name;
    std::string album;       // may be empty
    std::string imageUrl;    // album art URL ("large" size)
    std::string trackUrl;    // track page on last.fm
    int durationSec{0};      // track duration in seconds (0 = unknown)
};

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

private:
    // Non-copyable
    LastfmClient(const LastfmClient&) = delete;
    LastfmClient& operator=(const LastfmClient&) = delete;

    // Shared HTTP GET helper
    std::optional<std::string> HttpGet(const std::string& url);

    // CURL easy handle (RAII via constructor/destructor)
    void* curl_{nullptr};
};
