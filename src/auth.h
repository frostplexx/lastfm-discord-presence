#pragma once

#include <memory>
#include <functional>
#include <atomic>
#include <string>

namespace discordpp { class Client; }
class LastfmClient;

// Schedules `fn` to run on the next main-loop tick, outside of any Discord
// SDK callback dispatch (i.e. not nested inside discordpp::RunCallbacks()).
//
// The SDK does not tolerate being re-entered synchronously from within its
// own completion callbacks (UpdateToken/RefreshToken/Connect called back-to-back
// from inside another callback reliably segfaults — see crash investigation
// history). Every callback body below that needs to trigger
// another SDK call goes through `defer` instead of calling directly.
using DeferFn = std::function<void(std::function<void()>)>;

// OAuth device authorization grant flow.
// Prints URL + code, polls Discord until user authorizes, then calls connectWithToken.
// Only ever invoked from a top-level (non-callback) context — either directly
// at startup or via `defer` — so it's safe for it to call connectWithTokenFn
// directly without deferring further.
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
    const std::function<void(const std::string&, const std::string&)>& connectWithTokenFn,
    const DeferFn& defer);

// Save the token pair and update the SDK client with a new bearer token.
// Also registers the proactive refresh callback for automatic token renewal.
void connectWithToken(
    std::shared_ptr<discordpp::Client> client,
    uint64_t appId,
    const std::string& tokenFile,
    const std::function<void()>& startOAuthFn,
    const std::function<void(const std::string&, const std::string&)>& connectWithTokenFn,
    const std::string& accessToken,
    const std::string& refreshToken,
    const DeferFn& defer);
