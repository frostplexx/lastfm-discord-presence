#pragma once

#include <string>

struct Track {
    std::string artist;
    std::string name;
    std::string album;       // may be empty
    std::string imageUrl;    // album art URL ("large" size)
    std::string trackUrl;    // clickable track page/link (source-specific)
    std::string artistUrl;   // clickable artist page/link (source-specific)
    std::string albumUrl;    // clickable album page/link (source-specific)
    int durationSec{0};      // track duration in seconds (0 = unknown)
    int positionSec{-1};     // current playback position in seconds, if the
                             // source reports one (-1 = unsupported/unknown,
                             // e.g. Last.fm, ListenBrainz, or Navidrome <0.62.0).
                             // Used to detect seeks and resync the progress bar.
};
