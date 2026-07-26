#define DISCORDPP_IMPLEMENTATION
#include "discordpp.h"

#include "auth.h"
#include "music_sources/lastfm.h"
#include "music_sources/listenbrainz.h"
#include "music_sources/navidrome.h"
#include "music_sources/music_source.h"
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
#include <sstream>
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
    const char* kDefaultAppId = "1529873019353301063";
    if (!env_app_id)
        env_app_id = kDefaultAppId;
    uint64_t appId = parseAppId(env_app_id);

    if (std::getenv("DISCORD_APP_ID")) {
        std::cout << "  app: " << env_app_id << " (from env)\n";
    } else {
        std::cout << "  app: " << env_app_id << " (built-in default)\n";
    }

    // MUSIC_SOURCE is a comma-separated list of backends in priority
    // order, e.g. "lastfm,navidrome" or "lastfm". Default: lastfm,listenbrainz,navidrome.
    // Unconfigured sources are skipped with a warning.
    std::string musicSources = "lastfm,listenbrainz,navidrome";
    if (const char* v = std::getenv("MUSIC_SOURCE")) {
        if (v[0] != '\0')
            musicSources = toLower(v);
    }

    auto multiSource = std::make_unique<MultiSource>();

    // Parse comma-separated list
    std::istringstream ss(musicSources);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        auto start = token.find_first_not_of(" \t");
        auto end   = token.find_last_not_of(" \t");
        if (start == std::string::npos)
            continue;
        token = token.substr(start, end - start + 1);

        if (token == "lastfm") {
            const char* env_api_key = std::getenv("LASTFM_API_KEY");
            const char* env_user    = std::getenv("LASTFM_USER");
            if (!env_api_key || !env_user) {
                std::cerr << "[config] WARNING: lastfm listed in MUSIC_SOURCE "
                             "but LASTFM_API_KEY or LASTFM_USER not set, "
                             "skipping\n";
                continue;
            }
            multiSource->AddSource(
                std::make_unique<LastfmSource>(env_api_key, env_user));
            std::cout << "  source: lastfm (user: " << env_user << ")\n";
        } else if (token == "navidrome") {
            const char* env_host    = std::getenv("NAVIDROME_HOST");
            const char* env_admin_u = std::getenv("NAVIDROME_ADMIN_USERNAME");
            const char* env_admin_p = std::getenv("NAVIDROME_ADMIN_PASSWORD");
            const char* env_nd_user = std::getenv("NAVIDROME_USERNAME");
            if (!env_host || !env_admin_u || !env_admin_p || !env_nd_user) {
                std::cerr << "[config] WARNING: navidrome listed in "
                             "MUSIC_SOURCE but NAVIDROME_HOST, "
                             "NAVIDROME_ADMIN_USERNAME, "
                             "NAVIDROME_ADMIN_PASSWORD, or "
                             "NAVIDROME_USERNAME not set, skipping\n";
                continue;
            }
            multiSource->AddSource(
                std::make_unique<NavidromeSource>(
                    env_host, env_admin_u, env_admin_p, env_nd_user));
            std::cout << "  source: navidrome (user: " << env_nd_user << ")\n";
        } else if (token == "listenbrainz") {
            const char* env_user = std::getenv("LISTENBRAINZ_USER");
            if (!env_user) {
                std::cerr << "[config] WARNING: listenbrainz listed in "
                             "MUSIC_SOURCE but LISTENBRAINZ_USER not set, "
                             "skipping\n";
                continue;
            }
            multiSource->AddSource(
                std::make_unique<ListenbrainzSource>(env_user));
            std::cout << "  source: listenbrainz (user: " << env_user << ")\n";
        } else {
            std::cerr << "[config] WARNING: unknown music source '" << token
                      << "' in MUSIC_SOURCE, skipping\n";
        }
    }

    if (multiSource->Empty()) {
        std::cerr << "[config] ERROR: no valid music sources configured. "
                     "Set MUSIC_SOURCE (default: lastfm,navidrome) and "
                     "provide the required env vars for at least one source.\n";
        return 1;
    }

    // For the poll loop — MultiSource implements MusicSource, so the
    // generic poll() call below works unchanged.
    MusicSource& source = *multiSource;

    int pollIntervalSec = 10;
    if (const char* v = std::getenv("SOURCE_POLL_INTERVAL_SEC"))
        pollIntervalSec = std::max(1, std::atoi(v));

    std::string tokenFile = "~/.lastfm-discord-token";
    if (const char* v = std::getenv("DISCORD_TOKEN_FILE"))
        tokenFile = v;
    tokenFile = expandHome(tokenFile);

    bool shareUsername = true;
    if (const char* v = std::getenv("SOURCE_SHOW_BUTTON"))
        shareUsername = std::string(v) != "0";
    bool showSmallImage = true;
    if (const char* v = std::getenv("SOURCE_SHOW_SMALL_IMAGE"))
        showSmallImage = std::string(v) != "0";

    int disconnectDelaySec = 30;
    if (const char* v = std::getenv("SOURCE_DISCONNECT_DELAY_SEC"))
        disconnectDelaySec = std::max(1, std::atoi(v));

    // ── Signal handlers ───────────────────────────────────────────────────
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "=== lastfm-discord-presence ===\n"
              << "  sources:    " << musicSources << "\n"
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
    uint64_t                lastSourceGen{0};
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
            poll(source, client,
                 lastTrack, lastSourceGen, trackStart, pendingPost,
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