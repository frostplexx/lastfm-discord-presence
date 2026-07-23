#include "store.h"

#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

namespace TokenStore {

std::optional<TokenPair> Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        return std::nullopt;

    std::string line1, line2;
    std::getline(f, line1);
    std::getline(f, line2);

    if (line1.empty())
        return std::nullopt;

    TokenPair tokens;
    if (line2.empty()) {
        // Old format: single line = refresh token only
        tokens.refreshToken = line1;
    } else {
        tokens.accessToken  = line1;
        tokens.refreshToken = line2;
    }
    return tokens;
}

bool Save(const TokenPair& tokens, const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) {
        std::cerr << "[store] failed to write " << path << std::endl;
        return false;
    }
    f << tokens.accessToken << "\n"
      << tokens.refreshToken << "\n";
    f.close();

    chmod(path.c_str(), 0600);
    return true;
}

void Clear(const std::string& path) {
    std::remove(path.c_str());
}

} // namespace TokenStore
