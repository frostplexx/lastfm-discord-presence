#include "store.h"

#include <fstream>
#include <iostream>
#include <sys/stat.h>

namespace TokenStore {

std::optional<std::string> Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        return std::nullopt;

    std::string token;
    std::getline(f, token);

    if (token.empty())
        return std::nullopt;

    return token;
}

bool Save(const std::string& refreshToken, const std::string& path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) {
        std::cerr << "[store] failed to write " << path << std::endl;
        return false;
    }
    f << refreshToken << std::endl;
    f.close();

    // Restrict permissions on Unix-like systems
    chmod(path.c_str(), 0600);

    return true;
}

void Clear(const std::string& path) {
    std::remove(path.c_str());
}

} // namespace TokenStore
