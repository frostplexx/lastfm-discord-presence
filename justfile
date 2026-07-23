# lastfm-discord-presence — task runner

cmake     := "/nix/store/xh1ghr3zaif0dh0nhd5hifcb8shwfsi7-cmake-3.29.2/bin/cmake"
curl_inc  := "/nix/store/qs41r1lsy2v4n9cags5ac0g1amg07sh8-curl-8.7.1-dev/include"
curl_lib  := "/nix/store/50zbz9zrpxx2c4a9w2gs6cbbfb23mavv-curl-8.7.1/lib/libcurl.dylib"
cxx       := "/nix/store/ykbj4vy7p4gzhfm59say7q4pmddv8ja7-clang-wrapper-16.0.6/bin/clang++"
cc        := "/nix/store/ykbj4vy7p4gzhfm59say7q4pmddv8ja7-clang-wrapper-16.0.6/bin/clang"
builddir  := "build"

# Configure CMake (passes nix-store tool paths explicitly)
configure:
    {{cmake}} -S . -B {{builddir}} -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER={{cc}} \
      -DCMAKE_CXX_COMPILER={{cxx}} \
      -DCURL_INCLUDE_DIR={{curl_inc}} \
      -DCURL_LIBRARY={{curl_lib}}

# Build (configure first if needed)
build: configure
    {{cmake}} --build {{builddir}}

# Clean build artefacts
clean:
    rm -rf {{builddir}} lib/nlohmann

# Ad-hoc sign the dylib + binary (strip quarantine, resign)
sign:
    xattr -cr {{builddir}}/libdiscord_partner_sdk.dylib
    codesign --force --sign - {{builddir}}/libdiscord_partner_sdk.dylib
    xattr -cr {{builddir}}/lastfm-discord-presence
    codesign --force --sign - {{builddir}}/lastfm-discord-presence

# Full rebuild
rebuild: clean build

# Build and run
run: build sign
    cd {{builddir}} && ./lastfm-discord-presence

# Build and run with env from shell variables
#   export LASTFM_API_KEY=xxx LASTFM_USER=xxx DISCORD_APP_ID=xxx
run-demo: build sign
    cd {{builddir}} && \
    LASTFM_API_KEY="${LASTFM_API_KEY}" \
    LASTFM_USER="${LASTFM_USER}" \
    DISCORD_APP_ID="${DISCORD_APP_ID}" \
    ./lastfm-discord-presence

# Fetch nlohmann/json single header (already vendored)
fetch-json:
    curl -sL -o lib/json.hpp \
      https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp

# Show available commands
default:
    @echo "targets:"
    @echo "  configure  — cmake configure (with nix clang + curl)"
    @echo "  build      — configure + compile"
    @echo "  sign       — ad-hoc sign dylib + binary (macOS quarantine bypass)"
    @echo "  clean      — remove build/ and vendored json.hpp"
    @echo "  rebuild    — clean + build"
    @echo "  run        — build + run (set env vars)"
    @echo "  run-demo   — build + run with env from shell vars"
