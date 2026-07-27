#include "auth.h"
#include "discordpp.h"
#include "music_sources/lastfm.h"
#include "store.h"
#include "utils.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <chrono>
#include <thread>

using json = nlohmann::json;

// ── connectWithToken ─────────────────────────────────────────────────────────
void connectWithToken(
    std::shared_ptr<discordpp::Client> client,
    uint64_t appId,
    const std::string& tokenFile,
    const std::function<void()>& startOAuthFn,
    const std::function<void(const std::string&, const std::string&)>& connectWithTokenFn,
    const std::string& accessToken,
    const std::string& refreshToken)
{
    TokenStore::Save({accessToken, refreshToken}, tokenFile);
    client->UpdateToken(
        discordpp::AuthorizationTokenType::Bearer, accessToken,
        [client, appId, tokenFile, startOAuthFn, connectWithTokenFn](
            discordpp::ClientResult r) {
            if (r.Successful()) {
                std::cout << "[auth] token updated, connecting..."
                          << std::endl;
                client->Connect();

                // Proactive token refresh: SDK fires this before expiry.
                client->SetTokenExpirationCallback(
                    [client, appId, tokenFile,
                     startOAuthFn, connectWithTokenFn]() {
                        std::cout
                            << "[auth] token expiring soon, refreshing..."
                            << std::endl;
                        auto saved = TokenStore::Load(tokenFile);
                        if (saved && !saved->refreshToken.empty()) {
                            client->RefreshToken(
                                appId, saved->refreshToken,
                                [client, tokenFile,
                                 startOAuthFn, connectWithTokenFn](
                                    discordpp::ClientResult result,
                                    std::string accessToken_,
                                    std::string refreshToken_,
                                    discordpp::AuthorizationTokenType,
                                    int32_t,
                                    std::string) {
                                    if (result.Successful()) {
                                        std::cout
                                            << "[auth] token refreshed!"
                                            << std::endl;
                                        connectWithTokenFn(
                                            accessToken_, refreshToken_);
                                    } else {
                                        std::cerr
                                            << "[auth] refresh failed ("
                                            << result.Error()
                                            << "), starting OAuth..."
                                            << std::endl;
                                        TokenStore::Clear(tokenFile);
                                        startOAuthFn();
                                    }
                                });
                        } else {
                            std::cout
                                << "[auth] no refresh token, starting OAuth..."
                                << std::endl;
                            startOAuthFn();
                        }
                    });
            } else {
                std::cerr << "[auth] UpdateToken failed: "
                          << r.Error() << std::endl;
            }
        });
}

// ── startOAuth ───────────────────────────────────────────────────────────────
void startOAuth(
    std::shared_ptr<discordpp::Client> client,
    LastfmClient& lastfm,
    uint64_t appId,
    const std::function<void(const std::string&, const std::string&)>& connectWithTokenFn,
    std::atomic<bool>& running)
{
    std::string scopes = discordpp::Client::GetDefaultPresenceScopes();

    // Step 1: Request device + user code from Discord
    std::cout << "[auth] requesting device code..." << std::endl;
    std::cout << "[auth] scopes=\"" << scopes << "\" appId=" << appId << std::endl;

    std::string deviceData =
        "client_id=" + std::to_string(appId) +
        "&scope=" + urlEncode(scopes);

    auto deviceResp = lastfm.HttpPost(
        "https://discord.com/api/oauth2/device/authorize", deviceData);
    if (!deviceResp) {
        std::cerr << "[auth] device request failed" << std::endl;
        return;
    }

    std::cout << "[auth] device response: " << deviceResp->substr(0, 512) << std::endl;

    json deviceJson;
    try { deviceJson = json::parse(*deviceResp); }
    catch (...) {
        std::cerr << "[auth] failed to parse device response: "
                  << deviceResp->substr(0, 512) << std::endl;
        return;
    }

    std::string deviceCode = deviceJson.value("device_code", "");
    std::string userCode = deviceJson.value("user_code", "");
    std::string verifyUrl = deviceJson.value("verification_uri_complete", "");
    int expiresIn = deviceJson.value("expires_in", 300);
    int interval = deviceJson.value("interval", 5);

    if (deviceCode.empty()) {
        std::string errMsg = deviceJson.value("error", "");
        std::string errDesc = deviceJson.value("error_description", "");
        std::cerr << "[auth] device auth error";
        if (!errMsg.empty()) std::cerr << ": " << errMsg;
        if (!errDesc.empty()) std::cerr << " (" << errDesc << ")";
        std::cerr << std::endl;
        return;
    }

    std::cout << "\n======================================================\n"
              << "  Authorize on any device:\n"
              << "  URL:  " << verifyUrl << "\n"
              << "  Code: " << userCode << "\n"
              << "======================================================\n"
              << std::endl;

    // Step 2: Poll for access token
    std::string tokenData =
        "client_id=" + std::to_string(appId) +
        "&device_code=" + urlEncode(deviceCode) +
        "&grant_type=urn:ietf:params:oauth:grant-type:device_code";

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(expiresIn);

    while (std::chrono::steady_clock::now() < deadline && running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));

        auto tokenResp = lastfm.HttpPost(
            "https://discord.com/api/oauth2/token", tokenData);
        if (!tokenResp) continue;

        json tokenJson;
        try { tokenJson = json::parse(*tokenResp); } catch (...) { continue; }

        if (tokenJson.contains("access_token")) {
            std::string accessToken = tokenJson["access_token"];
            std::string refreshToken = tokenJson.value("refresh_token", "");
            std::cout << "[auth] got access token via device auth!" << std::endl;
            connectWithTokenFn(accessToken, refreshToken);
            return;
        }

        std::string err = tokenJson.value("error", "");
        if (err == "access_denied") {
            std::cerr << "[auth] authorization denied" << std::endl;
            return;
        }
        if (err == "expired_token") {
            std::cerr << "[auth] device code expired, restart required" << std::endl;
            return;
        }
        if (err == "slow_down") {
            interval += 5;
            std::cout << "[auth] server asked to slow down, new interval=" << interval << "s" << std::endl;
        }
    }

    std::cerr << "[auth] device auth timed out after " << expiresIn << "s" << std::endl;
}

// ── tryRefresh ───────────────────────────────────────────────────────────────
void tryRefresh(
    std::shared_ptr<discordpp::Client> client,
    uint64_t appId,
    const std::string& tokenFile,
    const std::function<void()>& startOAuthFn,
    const std::function<void(const std::string&, const std::string&)>& connectWithTokenFn)
{
    auto savedTokens = TokenStore::Load(tokenFile);
    if (!savedTokens.has_value() || savedTokens->refreshToken.empty()) {
        std::cout << "[auth] no refresh token saved, starting OAuth..."
                  << std::endl;
        startOAuthFn();
        return;
    }

    std::cout << "[auth] found saved refresh token, attempting refresh..."
              << std::endl;
    client->RefreshToken(
        appId, savedTokens->refreshToken,
        [&](discordpp::ClientResult result,
            std::string            accessToken,
            std::string            refreshToken,
            discordpp::AuthorizationTokenType,
            int32_t,
            std::string) {
            if (result.Successful()) {
                std::cout << "[auth] token refreshed!" << std::endl;
                connectWithTokenFn(accessToken, refreshToken);
            } else {
                std::cerr << "[auth] refresh failed ("
                          << result.Error()
                          << "), starting fresh OAuth..." << std::endl;
                TokenStore::Clear(tokenFile);
                startOAuthFn();
            }
        });
}