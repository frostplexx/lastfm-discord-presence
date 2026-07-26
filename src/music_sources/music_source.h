#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "track.h"

// Branding shown in the Discord presence for whichever backend supplied
// the track (Last.fm vs. Navidrome): small image overlay, its link target,
// and the "View on X" button label.
struct SourceBranding {
    std::string name;              // e.g. "Last.fm" / "Navidrome"
    std::string smallImageUrl;     // small icon overlay asset
    std::string smallImageLinkUrl; // where the small icon links to
    std::string buttonLabel;       // e.g. "View on Last.fm"
};

// Abstraction over "where do we get the currently-playing track from".
class MusicSource {
public:
    virtual ~MusicSource() = default;

    // Query the currently playing track. Returns nullopt if nothing is
    // playing right now or on error.
    virtual std::optional<Track> NowPlaying() = 0;

    // Enrich a track with duration info, if the NowPlaying() call didn't
    // already include it. Called only when about to post a presence update,
    // to avoid an extra API request on every poll. Default: no-op.
    virtual void FillDuration(Track&) {}

    virtual SourceBranding Branding() const = 0;

    // Optional generation counter for source-switch detection.
    // MultiSource bumps this when the active source changes between
    // NowPlaying() calls (same TrackId from a different backend).
    // Single-source implementations can leave the default (0).
    virtual uint64_t ActiveSourceGen() const { return 0; }
};

// Wraps multiple MusicSource implementations in priority order.
// The first source that returns a track from NowPlaying() "wins" for
// that poll cycle. Its FillDuration and Branding are used for the
// presence update.
class MultiSource : public MusicSource {
public:
    // Add a source. Order matters — first added = highest priority.
    void AddSource(std::unique_ptr<MusicSource> source);

    std::optional<Track> NowPlaying() override;
    void FillDuration(Track& t) override;
    SourceBranding Branding() const override;

    bool Empty() const;

    // Monotonically increasing counter that bumps every time the active
    // source changes between NowPlaying() calls (even when the TrackId
    // stays the same). Used by poll() to detect source switches.
    uint64_t ActiveSourceGen() const override { return sourceGen_; }

private:
    struct Entry {
        std::unique_ptr<MusicSource> source;
    };
    std::vector<Entry> sources_;
    MusicSource* activeSource_{nullptr};
    uint64_t sourceGen_{0};
};
