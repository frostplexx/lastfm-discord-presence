#pragma once

#include <string>
#include <optional>

struct TokenPair {
    std::string accessToken;
    std::string refreshToken;
};

namespace TokenStore {

// Load both tokens from disk. Returns nullopt if file doesn't exist.
std::optional<TokenPair> Load(const std::string& path);

// Save both tokens to disk (overwrites file with 0600 perms).
bool Save(const TokenPair& tokens, const std::string& path);

// Remove the token file.
void Clear(const std::string& path);

} // namespace TokenStore
