#define DISCORDPP_IMPLEMENTATION
#include "discordpp.h"

#include "lastfm.h"
#include "store.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <optional>
#include <memory>
#include <string>
#include <thread>

// ── Globals (signal handler needs them) ─────────────────────────────────────
static std::atomic<bool> running{true};

void signalHandler(int) {
    running.store(false);
}

// ── Helpers ──────────────────────────────────────────────────────────────────
static std::string expandHome(const std::string& path) {
    if (path.size() > 1 && path[0] == '~' && path[1] == '/') {
        const char* home = std::getenv("HOME");
        if (home)
            return std::string(home) + path.substr(1);
    }
    return path;
}

static uint64_t parseAppId(const std::string& s) {
    return static_cast<uint64_t>(std::stoull(s));
}

static std::string artistUrlFromName(const std::string& artist) {
    std::ostringstream s;
    for (unsigned char c : artist) {
        if (c <= 32 || c == 127 || c == '/' || c == '?' || c == '#' ||
            c == '&' || c == '=' || c == '%') {
            static const char hex[] = "0123456789ABCDEF";
            s << '%' << hex[c >> 4] << hex[c & 0xf];
        } else {
            s << c;
        }
    }
    return "https://www.last.fm/music/" + s.str();
}

static std::string albumUrlFromNames(const std::string& artist,
                                     const std::string& album) {
    if (album.empty()) return {};
    std::ostringstream s;
    auto append = [&](const std::string& raw) {
        for (unsigned char c : raw) {
            if (c <= 32 || c == 127 || c == '/' || c == '?' || c == '#' ||
                c == '&' || c == '=' || c == '%') {
                static const char hex[] = "0123456789ABCDEF";
                s << '%' << hex[c >> 4] << hex[c & 0xf];
            } else {
                s << c;
            }
        }
    };
    append(artist);
    s << '/';
    append(album);
    return "https://www.last.fm/music/" + s.str();
}

// ── Track identity (for change detection) ────────────────────────────────────
struct TrackId {
    std::string artist;
    std::string name;

    bool operator==(const TrackId& o) const {
        return artist == o.artist && name == o.name;
    }
    bool operator!=(const TrackId& o) const { return !(*this == o); }
};

// ── Main ────────────────────────────────────────────────────────────────────
int main() {
    // ── Config from environment ───────────────────────────────────────────
    const char* env_api_key  = std::getenv("LASTFM_API_KEY");
    const char* env_user     = std::getenv("LASTFM_USER");
    const char* env_app_id   = std::getenv("DISCORD_APP_ID");

    if (!env_api_key || !env_user || !env_app_id) {
        std::cerr << "Usage: LASTFM_API_KEY=<key> LASTFM_USER=<user> "
                     "DISCORD_APP_ID=<id>\n"
                     "Optional: LASTFM_POLL_INTERVAL_SEC=<sec> (default 10)\n"
                     "          DISCORD_TOKEN_FILE=<path> "
                     "(default ~/.lastfm-discord-token)\n";
        return 1;
    }

    std::string apiKey(env_api_key);
    std::string lastfmUser(env_user);
    uint64_t    appId = parseAppId(env_app_id);

    int pollIntervalSec = 10;
    if (const char* v = std::getenv("LASTFM_POLL_INTERVAL_SEC"))
        pollIntervalSec = std::max(1, std::atoi(v));

    std::string tokenFile = "~/.lastfm-discord-token";
    if (const char* v = std::getenv("DISCORD_TOKEN_FILE"))
        tokenFile = v;
    tokenFile = expandHome(tokenFile);

    bool shareUsername = true; // show "View on Last.fm" button
    if (const char* v = std::getenv("LASTFM_SHOW_BUTTON"))
        shareUsername = std::string(v) != "0";
    bool showSmallImage = true;
    if (const char* v = std::getenv("LASTFM_SHOW_SMALL_IMAGE"))
        showSmallImage = std::string(v) != "0";

    // ── Signal handlers ───────────────────────────────────────────────────
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "=== lastfm-discord-presence ===\n"
              << "  user:       " << lastfmUser << "\n"
              << "  poll every: " << pollIntervalSec << "s\n"
              << "  token:      " << tokenFile << "\n"
              << std::endl;

    // ── Create SDK client ─────────────────────────────────────────────────
    auto client = std::make_shared<discordpp::Client>();

    client->AddLogCallback(
        [](auto message, auto severity) {
            std::cout << "[sdk] " << EnumToString(severity) << " "
                      << message << std::endl;
        },
        discordpp::LoggingSeverity::Info);

    // ── State ─────────────────────────────────────────────────────────────
    std::atomic<bool>       ready{false};
    LastfmClient            lastfm;
    std::optional<TrackId>  lastTrack; // track we last posted to Discord

    // ── Rich presence helpers ─────────────────────────────────────────────
    auto postPresence = [&](const Track& t) {
        discordpp::Activity activity;
        activity.SetType(discordpp::ActivityTypes::Listening);
        activity.SetName("Last.fm");
        activity.SetDetails(t.name);       // line 2: track name
        activity.SetState(t.artist);       // line 3: artist name
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
            // No album art — use a last.fm logo URL as fallback
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

        // Timestamps: both start + end for progress bar when we have duration
        uint64_t now = time(nullptr);
        discordpp::ActivityTimestamps ts;
        ts.SetStart(now);
        if (t.durationSec > 0)
            ts.SetEnd(now + t.durationSec);
        activity.SetTimestamps(ts);

        // "View on Last.fm" button
        if (shareUsername) {
            discordpp::ActivityButton btn;
            btn.SetLabel("View on Last.fm");
            btn.SetUrl(artistUrlFromName(t.artist) + "/_/" +
                       t.name); // direct track URL as fallback
            // Prefer the actual track URL if we have it
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
    };

    auto clearPresence = [&]() {
        discordpp::Activity activity;
        activity.SetType(discordpp::ActivityTypes::Listening);
        activity.SetName("Last.fm");
        activity.SetDetails("");
        activity.SetState("");
        activity.SetStatusDisplayType(discordpp::StatusDisplayTypes::Name);

        client->UpdateRichPresence(
            activity, [](discordpp::ClientResult r) {
                if (!r.Successful())
                    std::cerr << "[presence] clear failed: " << r.Error()
                              << std::endl;
            });

        std::cout << "[lastfm] nothing playing, cleared presence" << std::endl;
    };

    // ── Poll loop ─────────────────────────────────────────────────────────
    auto poll = [&]() {
        auto track = lastfm.NowPlaying(apiKey, lastfmUser);
        TrackId current;
        bool hasTrack = false;

        if (track.has_value()) {
            current  = {track->artist, track->name};
            hasTrack = true;
        }

        // Skip if nothing changed
        if (lastTrack.has_value() == hasTrack &&
            (!hasTrack || lastTrack.value() == current))
            return;

        // State changed — fetch duration before posting
        if (hasTrack) {
            auto dur = lastfm.GetTrackDuration(apiKey, track->artist,
                                               track->name);
            if (dur.has_value())
                track->durationSec = *dur;
            postPresence(track.value());
            lastTrack = current;
        } else {
            clearPresence();
            lastTrack = std::nullopt;
        }
    };

    // ── Status callback ───────────────────────────────────────────────────
    client->SetStatusChangedCallback(
        [&](discordpp::Client::Status status,
            discordpp::Client::Error   error,
            int32_t                    errorDetail) {
            std::cout << "[sdk] status: "
                      << discordpp::Client::StatusToString(status) << std::endl;

            if (status == discordpp::Client::Status::Ready) {
                std::cout << "[sdk] connected to Discord!" << std::endl;
                ready.store(true);
            } else if (error != discordpp::Client::Error::None) {
                std::cerr << "[sdk] error: "
                          << discordpp::Client::ErrorToString(error)
                          << " detail=" << errorDetail << std::endl;
            }
        });

    // ── Auth helpers ──────────────────────────────────────────────────────
    auto connectWithToken =
        [&](const std::string& accessToken,
            const std::string& refreshToken) {
            TokenStore::Save(refreshToken, tokenFile);
            client->UpdateToken(
                discordpp::AuthorizationTokenType::Bearer, accessToken,
                [client](discordpp::ClientResult r) {
                    if (r.Successful()) {
                        std::cout << "[auth] token updated, connecting..."
                                  << std::endl;
                        client->Connect();
                    } else {
                        std::cerr << "[auth] UpdateToken failed: "
                                  << r.Error() << std::endl;
                    }
                });
        };

    // Full OAuth browser flow
    std::function<void()> startOAuth = [&, client]() {
        auto codeVerifier = client->CreateAuthorizationCodeVerifier();

        discordpp::AuthorizationArgs args{};
        args.SetClientId(appId);
        args.SetScopes(discordpp::Client::GetDefaultPresenceScopes());
        args.SetCodeChallenge(codeVerifier.Challenge());

        client->Authorize(
            args, [&, client, codeVerifier](auto result, auto code,
                                            auto redirectUri) mutable {
                if (!result.Successful()) {
                    std::cerr << "[auth] Authorize error: " << result.Error()
                              << std::endl;
                    return;
                }

                std::cout << "[auth] authorized, getting token..." << std::endl;

                client->GetToken(
                    appId, code, codeVerifier.Verifier(), redirectUri,
                    [&](discordpp::ClientResult result2,
                        std::string            accessToken,
                        std::string            refreshToken,
                        discordpp::AuthorizationTokenType,
                        int32_t,
                        std::string) {
                        if (!result2.Successful()) {
                            std::cerr << "[auth] GetToken error: "
                                      << result2.Error() << std::endl;
                            return;
                        }

                        std::cout << "[auth] got access token!" << std::endl;
                        connectWithToken(accessToken, refreshToken);
                    });
            });
    };

    // Try to reuse a saved refresh token.
    auto savedToken = TokenStore::Load(tokenFile);
    if (savedToken.has_value()) {
        std::cout << "[auth] found saved refresh token, attempting refresh..."
                  << std::endl;
        client->RefreshToken(
            appId, savedToken.value(),
            [&, savedToken](discordpp::ClientResult result,
                            std::string            accessToken,
                            std::string            refreshToken,
                            discordpp::AuthorizationTokenType,
                            int32_t,
                            std::string) {
                if (result.Successful()) {
                    std::cout << "[auth] token refreshed!" << std::endl;
                    connectWithToken(accessToken, refreshToken);
                } else {
                    std::cerr << "[auth] refresh failed ("
                              << result.Error()
                              << "), starting fresh OAuth..." << std::endl;
                    TokenStore::Clear(tokenFile);
                    startOAuth();
                }
            });
    } else {
        std::cout << "[auth] no saved token, starting OAuth..." << std::endl;
        startOAuth();
    }

    // ── Main loop ─────────────────────────────────────────────────────────
    auto lastPollTime = std::chrono::steady_clock::now();

    while (running.load()) {
        discordpp::RunCallbacks();

        if (ready.load()) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                               now - lastPollTime)
                               .count();
            if (elapsed >= pollIntervalSec) {
                poll();
                lastPollTime = now;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // ── Cleanup ───────────────────────────────────────────────────────────
    std::cout << "\nshutting down..." << std::endl;

    {
        discordpp::Activity activity;
        activity.SetType(discordpp::ActivityTypes::Listening);
        activity.SetDetails("");
        activity.SetState("");
        client->UpdateRichPresence(activity, [](auto) {});
    }

    discordpp::RunCallbacks();
    std::cout << "bye." << std::endl;
    return 0;
}
