#include "presence.h"
#include "discordpp.h"

#include <iostream>
#include <cstdlib>
#include <chrono>

// ── postPresence ─────────────────────────────────────────────────────────────
void postPresence(
    std::shared_ptr<discordpp::Client> client,
    const Track& t,
    const SourceBranding& branding,
    bool shareUsername,
    bool showSmallImage,
    std::optional<uint64_t>& trackStart)
{
    discordpp::Activity activity;
    activity.SetType(discordpp::ActivityTypes::Listening);
    activity.SetName(t.name + " — " + t.artist);
    activity.SetDetails(t.name);
    activity.SetState(t.artist);
    activity.SetStatusDisplayType(discordpp::StatusDisplayTypes::Details);

    // Clickable links
    if (!t.trackUrl.empty())
        activity.SetDetailsUrl(t.trackUrl);
    if (!t.artistUrl.empty())
        activity.SetStateUrl(t.artistUrl);

    // Album art as large image
    discordpp::ActivityAssets assets;
    if (!t.imageUrl.empty()) {
        assets.SetLargeImage(t.imageUrl);
        assets.SetLargeText(t.album.empty() ? t.artist : t.album);
        if (!t.albumUrl.empty())
            assets.SetLargeUrl(t.albumUrl);
    } else {
        assets.SetLargeImage(
            "https://www.last.fm/static/images/lastfm_avatar_twitter.png");
        assets.SetLargeText(branding.name);
    }

    // Small image: source logo overlay (optional, skipped if the source has none)
    if (showSmallImage && !branding.smallImageUrl.empty()) {
        assets.SetSmallImage(branding.smallImageUrl);
        assets.SetSmallText(branding.name);
        if (!branding.smallImageLinkUrl.empty())
            assets.SetSmallUrl(branding.smallImageLinkUrl);
    }

    activity.SetAssets(assets);

    // Timestamps: start recorded on first detection, reused on reconnect
    uint64_t now = time(nullptr);
    if (!trackStart.has_value())
        trackStart = now;

    discordpp::ActivityTimestamps ts;
    ts.SetStart(*trackStart);
    if (t.durationSec > 0)
        ts.SetEnd(*trackStart + t.durationSec);
    activity.SetTimestamps(ts);

    // "View on <source>" button
    if (shareUsername && !t.trackUrl.empty()) {
        discordpp::ActivityButton btn;
        btn.SetLabel(branding.buttonLabel);
        btn.SetUrl(t.trackUrl);
        activity.AddButton(btn);
    }

    client->UpdateRichPresence(
        activity, [](discordpp::ClientResult r) {
            if (!r.Successful())
                std::cerr << "[presence] update failed: " << r.Error()
                          << std::endl;
        });

    std::cout << "[" << branding.name << "] ♫ " << t.name << " — " << t.artist;
    if (t.durationSec > 0)
        std::cout << " (" << t.durationSec / 60 << ":"
                  << (t.durationSec % 60 < 10 ? "0" : "")
                  << t.durationSec % 60 << ")";
    std::cout << std::endl;
}

namespace {
// How far the source-reported playback position may drift from what we'd
// expect (based on trackStart) before we treat it as a seek rather than
// normal playback progression, and resync the progress bar.
constexpr int64_t kSeekToleranceSec = 5;
} // namespace

// ── clearPresence ────────────────────────────────────────────────────────────
void clearPresence(
    bool& disconnectPending,
    std::chrono::steady_clock::time_point& disconnectTimer,
    int disconnectDelaySec)
{
    disconnectPending = true;
    disconnectTimer = std::chrono::steady_clock::now()
                    + std::chrono::seconds(disconnectDelaySec);
    std::cout << "[presence] nothing playing, will disconnect in "
              << disconnectDelaySec << "s" << std::endl;
}

// ── poll ─────────────────────────────────────────────────────────────────────
void poll(
    MusicSource& source,
    std::shared_ptr<discordpp::Client> client,
    std::optional<TrackId>& lastTrack,
    uint64_t& lastSourceGen,
    std::optional<uint64_t>& trackStart,
    bool& pendingPost,
    bool& disconnectPending,
    std::chrono::steady_clock::time_point& disconnectTimer,
    std::atomic<bool>& ready,
    int disconnectDelaySec,
    bool shareUsername,
    bool showSmallImage)
{
    auto result = source.NowPlaying();
    std::optional<Track> track;
    if (result.kind == SourceResultKind::Playing)
        track = std::move(result.track);
    TrackId current;
    bool hasTrack = track.has_value();

    if (hasTrack)
        current  = {track->artist, track->name};

    // Check state vs last time we acted (posted or cleared)
    // Also detect source switches (same TrackId from a different backend).
    uint64_t currentGen = source.ActiveSourceGen();
    bool sourceChanged = (currentGen != lastSourceGen);
    bool changed = (lastTrack.has_value() != hasTrack) ||
                   (hasTrack && lastTrack.value() != current) ||
                   sourceChanged;

    if (hasTrack) {
        disconnectPending = false; // new track, cancel pending disconnect
        if (!ready.load()) {
            if (changed) {
                client->Connect();
                lastTrack = current;
                lastSourceGen = currentGen;
                pendingPost = true;
                trackStart = time(nullptr); // capture at detection
            }
            return; // wait for Ready callback
        }
        // Detect seeks on the same track: if the source reports a playback
        // position that doesn't match what we'd expect from trackStart,
        // resync trackStart so Discord's progress bar jumps too, instead of
        // silently drifting out of sync until the track actually changes.
        bool seeked = false;
        if (!changed && track->positionSec >= 0 && trackStart.has_value()) {
            int64_t now = static_cast<int64_t>(time(nullptr));
            int64_t expectedPos = now - static_cast<int64_t>(*trackStart);
            int64_t drift = expectedPos - track->positionSec;
            if (drift < 0)
                drift = -drift;
            if (drift > kSeekToleranceSec) {
                seeked = true;
                trackStart = static_cast<uint64_t>(now - track->positionSec);
            }
        }

        // Post if track changed, source switched, we reconnected, or seeked
        if (!changed && !pendingPost && !seeked)
            return;
        if (changed)
            trackStart.reset(); // reset progress bar for new track
        pendingPost = false;
        lastSourceGen = currentGen;
        source.FillDuration(track.value());
        postPresence(client, track.value(), source.Branding(),
                     shareUsername, showSmallImage, trackStart);
        lastTrack = current;
    } else {
        if (!changed)
            return;
        clearPresence(disconnectPending, disconnectTimer, disconnectDelaySec);
        lastTrack = std::nullopt;
        lastSourceGen = currentGen;
    }
}
