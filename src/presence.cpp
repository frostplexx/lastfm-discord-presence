#include "presence.h"
#include "discordpp.h"

#include <iostream>
#include <cstdlib>
#include <chrono>

// ── postPresence ─────────────────────────────────────────────────────────────
void postPresence(
    std::shared_ptr<discordpp::Client> client,
    const Track& t,
    const std::string& lastfmUser,
    bool shareUsername,
    bool showSmallImage,
    std::optional<uint64_t>& trackStart)
{
    discordpp::Activity activity;
    activity.SetType(discordpp::ActivityTypes::Listening);
    activity.SetName("Last.fm");
    activity.SetDetails(t.name);
    activity.SetState(t.artist);
    activity.SetStatusDisplayType(discordpp::StatusDisplayTypes::Details);

    // Clickable links
    if (!t.trackUrl.empty())
        activity.SetDetailsUrl(t.trackUrl);
    activity.SetStateUrl(artistUrlFromName(t.artist));

    // Album art as large image
    discordpp::ActivityAssets assets;
    if (!t.imageUrl.empty()) {
        assets.SetLargeImage(t.imageUrl);
        assets.SetLargeText(t.album.empty() ? t.artist : t.album);
        if (!t.album.empty())
            assets.SetLargeUrl(albumUrlFromNames(t.artist, t.album));
    } else {
        assets.SetLargeImage(
            "https://www.last.fm/static/images/lastfm_avatar_twitter.png");
        assets.SetLargeText("Last.fm");
    }

    // Small image: Last.fm logo overlay (optional)
    if (showSmallImage) {
        assets.SetSmallImage(
            "https://www.last.fm/static/images/lastfm_avatar_twitter.png");
        assets.SetSmallText("Last.fm");
        assets.SetSmallUrl("https://www.last.fm/user/" + lastfmUser);
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

    // "View on Last.fm" button
    if (shareUsername) {
        discordpp::ActivityButton btn;
        btn.SetLabel("View on Last.fm");
        btn.SetUrl(artistUrlFromName(t.artist) + "/_/" + t.name);
        if (!t.trackUrl.empty())
            btn.SetUrl(t.trackUrl);
        activity.AddButton(btn);
    }

    client->UpdateRichPresence(
        activity, [](discordpp::ClientResult r) {
            if (!r.Successful())
                std::cerr << "[presence] update failed: " << r.Error()
                          << std::endl;
        });

    std::cout << "[lastfm] \u266B " << t.name << " \u2014 " << t.artist;
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
    std::cout << "[lastfm] nothing playing, will disconnect in "
              << disconnectDelaySec << "s" << std::endl;
}

// ── poll ─────────────────────────────────────────────────────────────────────
void poll(
    LastfmClient& lastfm,
    const std::string& apiKey,
    const std::string& lastfmUser,
    std::shared_ptr<discordpp::Client> client,
    std::optional<TrackId>& lastTrack,
    std::optional<uint64_t>& trackStart,
    bool& pendingPost,
    bool& disconnectPending,
    std::chrono::steady_clock::time_point& disconnectTimer,
    std::atomic<bool>& ready,
    int disconnectDelaySec,
    bool shareUsername,
    bool showSmallImage)
{
    auto track = lastfm.NowPlaying(apiKey, lastfmUser);
    TrackId current;
    bool hasTrack = false;

    if (track.has_value()) {
        current  = {track->artist, track->name};
        hasTrack = true;
    }

    // Check state vs last time we acted (posted or cleared)
    bool changed = (lastTrack.has_value() != hasTrack) ||
                   (hasTrack && lastTrack.value() != current);

    if (hasTrack) {
        disconnectPending = false; // new track, cancel pending disconnect
        if (!ready.load()) {
            if (changed) {
                client->Connect();
                lastTrack = current;
                pendingPost = true;
                trackStart = time(nullptr); // capture at detection
            }
            return; // wait for Ready callback
        }
        // Post if track changed OR we reconnected and have a pending post
        if (!changed && !pendingPost)
            return;
        if (changed)
            trackStart.reset(); // reset progress bar for new track
        pendingPost = false;
        auto dur = lastfm.GetTrackDuration(apiKey, track->artist,
                                           track->name);
        if (dur.has_value())
            track->durationSec = *dur;
        postPresence(client, track.value(), lastfmUser,
                     shareUsername, showSmallImage, trackStart);
        lastTrack = current;
    } else {
        if (!changed)
            return;
        clearPresence(disconnectPending, disconnectTimer, disconnectDelaySec);
        lastTrack = std::nullopt;
    }
}