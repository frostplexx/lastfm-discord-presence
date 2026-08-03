#include "music_source.h"

#include <algorithm>
#include <iostream>

// ── Backoff (base class) ─────────────────────────────────────────────────────
bool MusicSource::InBackoff() const {
    return std::chrono::steady_clock::now() < nextAttempt_;
}

std::chrono::seconds MusicSource::BackoffDelay() const {
    if (failCount_ == 0)
        return std::chrono::seconds(0);
    auto delay = backoffBase_;
    for (int i = 1; i < failCount_; ++i)
        delay = std::min(delay * 2, backoffMax_);
    return delay;
}

void MusicSource::NoteFailure() {
    ++failCount_;
    nextAttempt_ = std::chrono::steady_clock::now() + BackoffDelay();
}

void MusicSource::NoteSuccess() {
    failCount_ = 0;
    nextAttempt_ = {};
}

void MusicSource::SetBackoffParams(std::chrono::seconds base,
                                   std::chrono::seconds max) {
    backoffBase_ = base;
    backoffMax_ = max;
}

SourceResult MusicSource::CheckedResult(SourceResult result, const char* tag) {
    if (result.kind == SourceResultKind::Error) {
        // Log only when the retry delay escalates (10s, 20s, 40s, ...).
        // At the cap, failures stay silent — the outage is already known.
        auto prevDelay = BackoffDelay();
        NoteFailure();
        auto newDelay = BackoffDelay();
        if (newDelay != prevDelay) {
            std::cerr << tag << " " << result.error
                      << " (retrying in " << newDelay.count() << "s)"
                      << std::endl;
        }
    } else {
        bool wasInBackoff = failCount_ > 0;
        NoteSuccess();
        if (wasInBackoff)
            std::cerr << tag << " recovered" << std::endl;
    }
    return result;
}

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
SourceResult MultiSource::NowPlaying() {
    auto prev = activeSource_;
    activeSource_ = nullptr;
    SourceResult firstError;
    bool sawError = false;
    for (auto& entry : sources_) {
        auto result = entry.source->NowPlaying();
        if (result.kind == SourceResultKind::Playing) {
            activeSource_ = entry.source.get();
            if (activeSource_ != prev)
                ++sourceGen_;
            return result;
        }
        if (result.kind == SourceResultKind::Error && !sawError) {
            sawError = true;
            firstError = std::move(result);
        }
    }
    if (prev != nullptr)
        ++sourceGen_;
    if (sawError)
        return firstError;
    return SourceResult{};
}

void MultiSource::SetBackoffParams(std::chrono::seconds base,
                                   std::chrono::seconds max) {
    for (auto& entry : sources_)
        entry.source->SetBackoffParams(base, max);
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
