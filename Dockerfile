# ── Multi-stage Docker build ─────────────────────────────────────────────────
# License compliance (Discord Social SDK EULA):
#   OK — SDK is present only in the builder stage for compilation/linking.
#   OK — Final image contains only the compiled binary with SDK dynamically
#         linked in (integrated into the Application, not standalone).
#   OK — The .so in the final image is a required runtime dependency of the
#         binary, not a redistributable SDK artifact (no headers, no zip, etc.).
#
# Build context expects the SDK at lib/discord_social_sdk/ (gitignored).
# CI: actions/cache + DISCORD_SDK_URL populates vendor/discord-sdk, then
#     symlinked into lib/ before this build runs.

# ── Stage 1: Build ───────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Vendored dependencies + SDK
COPY lib/ lib/

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
# SDK shared lib — note: for Linux the .so is used (handled by CMake)
COPY --from=builder /build/out/libdiscord_partner_sdk.so /app/

WORKDIR /app

ENTRYPOINT ["./lastfm-discord-presence"]
