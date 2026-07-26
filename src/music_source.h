#pragma once

#include <optional>
#include <string>

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
};
