#include "music_source.h"

// ── AddSource ────────────────────────────────────────────────────────────────
void MultiSource::AddSource(std::unique_ptr<MusicSource> source) {
    sources_.push_back({std::move(source)});
}

// ── Empty ────────────────────────────────────────────────────────────────────
bool MultiSource::Empty() const {
    return sources_.empty();
}

// ── NowPlaying ───────────────────────────────────────────────────────────────
// Try each source in priority order. The first one to return a track becomes
// the "active" source for FillDuration / Branding.
// Bumps sourceGen_ when the active source changes between calls, so the
// poll loop can detect source switches even when TrackId is unchanged.
std::optional<Track> MultiSource::NowPlaying() {
    auto prev = activeSource_;
    activeSource_ = nullptr;
    for (auto& entry : sources_) {
        auto track = entry.source->NowPlaying();
        if (track.has_value()) {
            activeSource_ = entry.source.get();
            if (activeSource_ != prev)
                ++sourceGen_;
            return track;
        }
    }
    if (prev != nullptr)
        ++sourceGen_;
    return std::nullopt;
}

// ── FillDuration ─────────────────────────────────────────────────────────────
void MultiSource::FillDuration(Track& t) {
    if (activeSource_)
        activeSource_->FillDuration(t);
}

// ── Branding ─────────────────────────────────────────────────────────────────
SourceBranding MultiSource::Branding() const {
    // Only called when a track is active (activeSource_ is set).
    return activeSource_ ? activeSource_->Branding() : SourceBranding{};
}
