# ── Multi-stage Docker build ─────────────────────────────────────────────────
# Prerequisites:
#   1. Discord Social SDK at lib/discord_social_sdk/ (real files, not symlink)
#      macOS: cp -RL lib/discord_social_sdk /tmp/sdk && rm -rf lib/discord_social_sdk && mv /tmp/sdk lib/discord_social_sdk
#   2. nlohmann/json.hpp at lib/json.hpp (vendored, already in repo)
#
# For CI, download the SDK from Discord Developer Portal and extract into lib/.

# ── Stage 1: Build ───────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    libcurl4-openssl-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Vendored nlohmann/json
COPY lib/json.hpp lib/json.hpp
COPY lib/nlohmann/ lib/nlohmann/

# Discord Social SDK (Linux)
COPY lib/discord_social_sdk/include/ lib/discord_social_sdk/include/
COPY lib/discord_social_sdk/lib/release/libdiscord_partner_sdk.so \
     lib/discord_social_sdk/lib/release/

# Source + CMake
COPY CMakeLists.txt .
COPY src/ src/

RUN cmake -S . -B out -DCMAKE_BUILD_TYPE=Release && \
    cmake --build out

# ── Stage 2: Runtime ─────────────────────────────────────────────────────────
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    libcurl4 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/out/lastfm-discord-presence /app/
COPY --from=builder /build/out/libdiscord_partner_sdk.so /app/

WORKDIR /app

ENTRYPOINT ["./lastfm-discord-presence"]
