#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "track.h"

// Result of one source poll. Distinguishes "responded, nothing playing"
// (Idle) from "source failed" (Error), so the poll loop can apply backoff
// only to real failures — never to a quiet, healthy source.
enum class SourceResultKind { Playing, Idle, Error };

struct SourceResult {
    SourceResultKind kind = SourceResultKind::Idle;
    Track track;           // valid when kind == Playing
    std::string error;     // non-empty when kind == Error
};

// Transport-level HTTP outcome for the per-source clients. Clients build a
// human-readable `error` string instead of logging directly, so the source
// wrapper can log failures at throttled (backoff) intervals.
struct HttpResult {
    bool ok = false;
    std::string body;
    std::string error;     // non-empty when !ok
};

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

    // Query the currently playing track.
    // Playing: a track is active. Idle: source responded, nothing playing.
    // Error: source failed (timeout, HTTP error, bad response).
    virtual SourceResult NowPlaying() = 0;

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

    // ── Exponential backoff (shared by all sources) ──
    // A source that keeps failing backs off base * 2^failures, capped at
    // max. While in backoff, the source's NowPlaying() should return Idle
    // immediately, without touching the network. Any non-error response
    // (Playing or Idle) resets the counter.
    bool InBackoff() const;
    std::chrono::seconds BackoffDelay() const;
    void NoteFailure();
    void NoteSuccess();
    void SetBackoffParams(std::chrono::seconds base,
                          std::chrono::seconds max);

    // Apply backoff bookkeeping + throttled logging to a result. Call this
    // from NowPlaying() implementations: the client reports the error, the
    // wrapper decides whether and how loudly to log it.
    //   return CheckedResult(client_.NowPlaying(...), "[listenbrainz]");
    SourceResult CheckedResult(SourceResult result, const char* tag);

protected:
    int failCount_{0};
    std::chrono::steady_clock::time_point nextAttempt_{};
    std::chrono::seconds backoffBase_{10};
    std::chrono::seconds backoffMax_{300};
};

// Wraps multiple MusicSource implementations in priority order.
// The first source that returns a track from NowPlaying() "wins" for
// that poll cycle. Its FillDuration and Branding are used for the
// presence update.
class MultiSource : public MusicSource {
public:
    // Add a source. Order matters — first added = highest priority.
    void AddSource(std::unique_ptr<MusicSource> source);

    SourceResult NowPlaying() override;
    void FillDuration(Track& t) override;
    SourceBranding Branding() const override;

    bool Empty() const;

    // Forward backoff parameters to every wrapped source.
    void SetBackoffParams(std::chrono::seconds base,
                          std::chrono::seconds max);

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
