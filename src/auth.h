#pragma once

#include <memory>
#include <functional>
#include <atomic>
#include <string>

namespace discordpp { class Client; }
class LastfmClient;

// OAuth device authorization grant flow.
// Prints URL + code, polls Discord until user authorizes, then calls connectWithToken.
void startOAuth(
    std::shared_ptr<discordpp::Client> client,
    LastfmClient& lastfm,
    uint64_t appId,
    const std::function<void(const std::string&, const std::string&)>& connectWithToken,
    std::atomic<bool>& running);

// Try to refresh a saved refresh token. Falls through to startOAuth on failure.
void tryRefresh(
    std::shared_ptr<discordpp::Client> client,
    uint64_t appId,
    const std::string& tokenFile,
    const std::function<void()>& startOAuthFn,
    const std::function<void(const std::string&, const std::string&)>& connectWithToken);

// Save the token pair and update the SDK client with a new bearer token.
void connectWithToken(
    std::shared_ptr<discordpp::Client> client,
    const std::string& tokenFile,
    const std::string& accessToken,
    const std::string& refreshToken);