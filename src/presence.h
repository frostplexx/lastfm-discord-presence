#pragma once

#include <memory>
#include <functional>
#include <atomic>
#include <string>
#include <optional>
#include <chrono>

#include "utils.h"
#include "music_source.h"

namespace discordpp { class Client; }

// Build a Discord activity with the given track info and post it.
// trackStart is updated on first detection and reused on reconnect.
void postPresence(
    std::shared_ptr<discordpp::Client> client,
    const Track& t,
    const SourceBranding& branding,
    bool shareUsername,
    bool showSmallImage,
    std::optional<uint64_t>& trackStart);

// Start the disconnect grace timer (actual disconnect is handled by the main loop).
void clearPresence(
    bool& disconnectPending,
    std::chrono::steady_clock::time_point& disconnectTimer,
    int disconnectDelaySec);

// Poll the configured music source for the currently playing track and
// update Discord presence.
// Modifies shared state: lastTrack, trackStart, pendingPost, disconnectPending, etc.
void poll(
    MusicSource& source,
    std::shared_ptr<discordpp::Client> client,
    std::optional<TrackId>& lastTrack,
    std::optional<uint64_t>& trackStart,
    bool& pendingPost,
    bool& disconnectPending,
    std::chrono::steady_clock::time_point& disconnectTimer,
    std::atomic<bool>& ready,
    int disconnectDelaySec,
    bool shareUsername,
    bool showSmallImage);
