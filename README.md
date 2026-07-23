# lastfm-discord-presence

C++20 daemon that polls [Last.fm](https://www.last.fm) for currently scrobbling tracks and sets them as Discord Rich Presence via the [Discord Social SDK](https://discord.com/developers/docs/social-sdk/overview) authenticated (server-connected) path.

Works with Discord **closed** — no local RPC/IPC dependency.

## Features

- **Listening status** — shows your currently playing track as a "Listening to Last.fm" presence
- **Album art** — pulled from Last.fm's API, displayed as the large image
- **Duration + progress bar** — fetches track length via `track.getInfo`, sets both start and end timestamps so Discord renders a visual progress bar
- **Small image overlay** — Last.fm logo as the small icon (toggleable via `LASTFM_SHOW_SMALL_IMAGE=0`)
- **Clickable URLs** — track name links to Last.fm track page, artist name links to artist page, album art links to album page
- **"View on Last.fm" button** — opens the track on Last.fm (toggleable via `LASTFM_SHOW_BUTTON=0`)
- **OAuth2 token persistence** — saves both access and refresh tokens across restarts

## How it looks

```
┌─────────────────────────────────────────────┐
│ 🎧 Listening to Last.fm                     │
│                                              │
│ 🖼️ SPINEHAMMER — Chainsword                 │
│    ━━━━━━━━━━━━━━━━━━━━━━━━━ 1:32 / 3:30    │
│                                              │
│ [View on Last.fm]                            │
└─────────────────────────────────────────────┘
```

## Quick start

### 1. Prerequisites

- **macOS** (for native build) or **Docker** (for Linux/macOS)
- **C++20 compiler** (Clang 16+ or GCC 13+)
- **CMake** 3.16+
- **libcurl** (dev headers for build, runtime lib for execution)
- **Discord Social SDK** — download from [Discord Developer Portal](https://discord.com/developers/applications)
- **Last.fm API key** — get one at [last.fm/api](https://www.last.fm/api)
- **Discord Application** — create one at [Discord Developer Portal](https://discord.com/developers/applications)

### 2. Set up the Discord Social SDK

```sh
# Extract the SDK into lib/ so it's at:
# lib/discord_social_sdk/include/discordpp.h
# lib/discord_social_sdk/lib/release/libdiscord_partner_sdk.dylib  (macOS)
# lib/discord_social_sdk/lib/release/libdiscord_partner_sdk.so     (Linux)
```

### 3. Build and run (macOS)

```sh
just build
just sign              # ad-hoc sign dylib + binary (required once on macOS)
LASTFM_API_KEY=xxx LASTFM_USER=xxx DISCORD_APP_ID=xxx ./build/lastfm-discord-presence

# Or use the just run target after setting env vars:
export LASTFM_API_KEY=xxx LASTFM_USER=xxx DISCORD_APP_ID=xxx
just run
```

### 4. Build and run (Docker)

```sh
just expand-sdk     # replace SDK symlink with real files
just docker-build

# Run with env vars:
docker run --rm -it \
  -e LASTFM_API_KEY=xxx \
  -e LASTFM_USER=xxx \
  -e DISCORD_APP_ID=xxx \
  -v lastfm-presence-token:/data \
  lastfm-discord-presence
```

### 5. Deploy with docker-compose

```sh
cp .env.example .env
# Edit .env with your keys
docker compose up -d
```

## Environment variables

| Variable                   | Required | Default                    | Description                              |
| -------------------------- | -------- | -------------------------- | ---------------------------------------- |
| `LASTFM_API_KEY`           | yes      | —                          | Last.fm API key                          |
| `LASTFM_USER`              | yes      | —                          | Last.fm username to poll                 |
| `DISCORD_APP_ID`           | yes      | —                          | Discord Application ID                   |
| `LASTFM_POLL_INTERVAL_SEC` | no       | `10`                       | How often to poll Last.fm (seconds)      |
| `LASTFM_SHOW_BUTTON`       | no       | `1`                        | Show "View on Last.fm" button            |
| `LASTFM_SHOW_SMALL_IMAGE`  | no       | `1`                        | Show Last.fm logo as small image overlay |
| `DISCORD_TOKEN_FILE`       | no       | `~/.lastfm-discord-token`  | Path to saved OAuth token file           |

## Discord Developer Portal setup

1. Go to [Discord Developer Portal](https://discord.com/developers/applications) → your app
2. **OAuth2** → toggle **"Public Client"** ON (required for the SDK's token exchange)
3. **OAuth2 → Redirects** → add `http://127.0.0.1/callback`
4. **Rich Presence → Art Assets** → optionally upload assets (not required if using external URLs for album art)

## GitHub Actions CI / GitHub Container Registry

The workflow at `.github/workflows/docker.yml` builds and pushes to `ghcr.io`.

**Required secret:**

| Secret             | Description                                        |
| ------------------ | -------------------------------------------------- |
| `DISCORD_SDK_URL`  | URL to download the Linux Discord Social SDK tarball |

Push to `main` triggers the build. Images are tagged with branch name and commit SHA.

## Project structure

```
├── CMakeLists.txt              # CMake build (handles macOS/Linux/Windows + SDK paths)
├── Dockerfile                  # Multi-stage Docker build
├── docker-compose.yml          # Docker Compose deployment
├── flake.nix                   # Nix dev shell (alternative dev env)
├── justfile                    # Task runner (build, run, sign, docker, etc.)
├── lib/
│   ├── discord_social_sdk/     # → Discord Social SDK (symlink to download)
│   ├── json.hpp                # Vendored nlohmann/json single header
│   └── nlohmann/json.hpp       # Copy for <nlohmann/json.hpp> include
├── src/
│   ├── main.cpp                # Entry point, OAuth flow, poll loop, presence updates
│   ├── lastfm.h / lastfm.cpp   # Last.fm API client (NowPlaying, GetTrackDuration)
│   ├── store.h / store.cpp     # Token persistence (access + refresh tokens)
├── .github/workflows/docker.yml
└── README.md
```

## License

This project is MIT-licensed. The Discord Social SDK is subject to Discord's own license terms.
