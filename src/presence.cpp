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
    auto track = source.NowPlaying();
    TrackId current;
    bool hasTrack = false;

    if (track.has_value()) {
        current  = {track->artist, track->name};
        hasTrack = true;
    }

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
        // Post if track changed, source switched, or we reconnected
        if (!changed && !pendingPost)
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
