#define DISCORDPP_IMPLEMENTATION
#include "discordpp.h"

#include "auth.h"
#include "lastfm.h"
#include "navidrome.h"
#include "music_source.h"
#include "presence.h"
#include "store.h"
#include "utils.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <memory>
#include <string>
#include <thread>

// ── Globals (signal handler needs them) ─────────────────────────────────────
static std::atomic<bool> running{true};

void signalHandler(int) {
    running.store(false);
}

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

} // namespace

// ── Main ────────────────────────────────────────────────────────────────────
int main() {
    // ── Config from environment ───────────────────────────────────────────
    const char* env_app_id = std::getenv("DISCORD_APP_ID");
    if (!env_app_id) {
        std::cerr << "Usage: DISCORD_APP_ID=<id> plus either:\n"
                     "  LASTFM_API_KEY=<key> LASTFM_USER=<user>              "
                     "(MUSIC_SOURCE=lastfm, default)\n"
                     "  NAVIDROME_HOST=<url> NAVIDROME_ADMIN_USERNAME=<user> "
                     "NAVIDROME_ADMIN_PASSWORD=<pass> NAVIDROME_USERNAME=<user> "
                     "(MUSIC_SOURCE=navidrome)\n"
                     "Optional: LASTFM_POLL_INTERVAL_SEC=<sec> (default 10)\n"
                     "          LASTFM_DISCONNECT_DELAY_SEC=<sec> (default 30)\n"
                     "          DISCORD_TOKEN_FILE=<path> "
                     "(default ~/.lastfm-discord-token)\n";
        return 1;
    }
    uint64_t appId = parseAppId(env_app_id);

    // MUSIC_SOURCE selects the backend: "navidrome" or "lastfm".
    // Unset or any other value falls back to lastfm.
    std::string musicSource = "lastfm";
    if (const char* v = std::getenv("MUSIC_SOURCE"))
        musicSource = toLower(v);

    std::unique_ptr<MusicSource> source;
    std::string monitoredUser; // for logging only

    if (musicSource == "navidrome") {
        const char* env_host      = std::getenv("NAVIDROME_HOST");
        const char* env_admin_u   = std::getenv("NAVIDROME_ADMIN_USERNAME");
        const char* env_admin_p   = std::getenv("NAVIDROME_ADMIN_PASSWORD");
        const char* env_nd_user   = std::getenv("NAVIDROME_USERNAME");

        if (!env_host || !env_admin_u || !env_admin_p || !env_nd_user) {
            std::cerr << "MUSIC_SOURCE=navidrome requires NAVIDROME_HOST, "
                         "NAVIDROME_ADMIN_USERNAME, NAVIDROME_ADMIN_PASSWORD "
                         "and NAVIDROME_USERNAME\n"
                         "(admin credentials are required because only an "
                         "admin account can see another user's now-playing "
                         "status)\n";
            return 1;
        }

        monitoredUser = env_nd_user;
        source = std::make_unique<NavidromeSource>(
            env_host, env_admin_u, env_admin_p, env_nd_user);
    } else {
        const char* env_api_key = std::getenv("LASTFM_API_KEY");
        const char* env_user    = std::getenv("LASTFM_USER");

        if (!env_api_key || !env_user) {
            std::cerr << "LASTFM_API_KEY and LASTFM_USER are required "
                         "(MUSIC_SOURCE=lastfm, the default)\n";
            return 1;
        }

        monitoredUser = env_user;
        source = std::make_unique<LastfmSource>(env_api_key, env_user);
    }

    int pollIntervalSec = 10;
    if (const char* v = std::getenv("LASTFM_POLL_INTERVAL_SEC"))
        pollIntervalSec = std::max(1, std::atoi(v));

    std::string tokenFile = "~/.lastfm-discord-token";
    if (const char* v = std::getenv("DISCORD_TOKEN_FILE"))
        tokenFile = v;
    tokenFile = expandHome(tokenFile);

    bool shareUsername = true;
    if (const char* v = std::getenv("LASTFM_SHOW_BUTTON"))
        shareUsername = std::string(v) != "0";
    bool showSmallImage = true;
    if (const char* v = std::getenv("LASTFM_SHOW_SMALL_IMAGE"))
        showSmallImage = std::string(v) != "0";

    int disconnectDelaySec = 30;
    if (const char* v = std::getenv("LASTFM_DISCONNECT_DELAY_SEC"))
        disconnectDelaySec = std::max(1, std::atoi(v));

    // ── Signal handlers ───────────────────────────────────────────────────
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "=== lastfm-discord-presence ===\n"
              << "  source:     " << musicSource << "\n"
              << "  user:       " << monitoredUser << "\n"
              << "  poll every: " << pollIntervalSec << "s\n"
              << "  disconnect: " << disconnectDelaySec << "s\n"
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
    std::optional<TrackId>  lastTrack;
    std::optional<uint64_t> trackStart;
    bool pendingPost = false;
    bool needsPoll = false;
    bool disconnectPending = false;
    auto disconnectTimer = std::chrono::steady_clock::time_point{};
    auto lastPollTime = std::chrono::steady_clock::now();

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
                needsPoll = true;
            } else if (error != discordpp::Client::Error::None) {
                std::cerr << "[sdk] error: "
                          << discordpp::Client::ErrorToString(error)
                          << " detail=" << errorDetail << std::endl;
            }
        });

    // ── Auth callback wiring ──────────────────────────────────────────────
    // These are captured-by-reference in the lambdas that auth functions need.
    auto doConnectWithToken =
        [&](const std::string& accessToken,
            const std::string& refreshToken) {
            connectWithToken(client, tokenFile, accessToken, refreshToken);
        };

    std::function<void()> doStartOAuth = [&]() {
        startOAuth(client, lastfm, appId, doConnectWithToken, running);
    };

    auto doTryRefresh = [&]() {
        tryRefresh(client, appId, tokenFile, doStartOAuth, doConnectWithToken);
    };

    // ── Startup: try saved access token first, then refresh, then OAuth ────
    auto savedTokens = TokenStore::Load(tokenFile);
    if (savedTokens.has_value() && !savedTokens->accessToken.empty()) {
        std::cout << "[auth] found saved access token, trying it..."
                  << std::endl;
        client->UpdateToken(
            discordpp::AuthorizationTokenType::Bearer,
            savedTokens->accessToken,
            [&](discordpp::ClientResult r) {
                if (r.Successful()) {
                    std::cout << "[auth] access token valid, connecting..."
                              << std::endl;
                    client->Connect();
                } else {
                    std::cout << "[auth] access token expired, "
                                 "trying refresh..."
                              << std::endl;
                    doTryRefresh();
                }
            });
    } else if (savedTokens.has_value() && !savedTokens->refreshToken.empty()) {
        doTryRefresh();
    } else {
        std::cout << "[auth] no saved token, starting OAuth..." << std::endl;
        doStartOAuth();
    }

    // ── Main loop ─────────────────────────────────────────────────────────
    while (running.load()) {
        discordpp::RunCallbacks();

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                           now - lastPollTime)
                           .count();
        if (needsPoll || elapsed >= pollIntervalSec) {
            needsPoll = false;
            poll(*source, client,
                 lastTrack, trackStart, pendingPost,
                 disconnectPending, disconnectTimer, ready,
                 disconnectDelaySec, shareUsername, showSmallImage);
            lastPollTime = now;
        }

        // Disconnect after grace period
        if (disconnectPending && ready.load() &&
            std::chrono::steady_clock::now() >= disconnectTimer) {
            client->Disconnect();
            ready.store(false);
            disconnectPending = false;
            std::cout << "[presence] nothing playing, disconnected" << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // ── Cleanup ───────────────────────────────────────────────────────────
    std::cout << "\nshutting down..." << std::endl;
    client->Disconnect();
    discordpp::RunCallbacks();
    std::cout << "bye." << std::endl;
    return 0;
}