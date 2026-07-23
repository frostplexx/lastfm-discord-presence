#pragma once

#include <string>
#include <optional>

namespace TokenStore {

// Load refresh token from disk. Returns nullopt if file doesn't exist.
std::optional<std::string> Load(const std::string& path);

// Save refresh token to disk (creates/overwrites file with 0600 perms).
bool Save(const std::string& refreshToken, const std::string& path);

// Remove the token file.
void Clear(const std::string& path);

} // namespace TokenStore
