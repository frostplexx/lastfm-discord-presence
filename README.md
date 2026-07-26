# lastfm-discord-presence

C++20 daemon that polls [Last.fm](https://www.last.fm) or a self-hosted [Navidrome](https://www.navidrome.org) server for currently playing tracks and sets them as Discord Rich Presence via the [Discord Social SDK](https://discord.com/developers/docs/social-sdk/overview) authenticated (server-connected) path.

## Features

- **Multiple music sources** — Last.fm and/or Navidrome, selected via `MUSIC_SOURCE` (comma-separated priority list)
- **Listening status** — shows your currently playing track as a "Listening to..." presence
- **Album art** — pulled from the source's API, displayed as the large image
- **Duration + progress bar** — fetches track length, sets both start and end timestamps so Discord renders a visual progress bar
- **Small image overlay** — source logo as the small icon (toggleable via `SOURCE_SHOW_SMALL_IMAGE=0`; Last.fm only)
- **Clickable URLs** — track name links to the source's track page, artist name links to artist page, album art links to album page
- **"View on ..." button** — opens the track on the source (toggleable via `SOURCE_SHOW_BUTTON=0`)
- **OAuth2 token persistence** — saves both access and refresh tokens across restarts

## 🐳 Docker Deployment

### Prerequisites

- **Docker** (or **Podman** with docker-compose compatibility)
- Either:
  - **Last.fm API key** — get one at [last.fm/api](https://www.last.fm/api), or
  - A **Navidrome** server plus an **admin account** on it (see [Navidrome setup](#navidrome-setup) below)
- **Discord Application** — create one at [Discord Developer Portal](https://discord.com/developers/applications)

### Discord Developer Portal setup

1. Go to [Discord Developer Portal](https://discord.com/developers/applications) → your app
2. **OAuth2**: toggle **"Public Client"** ON (required for the SDK's token exchange)
4. **Application ID**: Copy the Application ID found in the OAuth2 section

### docker-compose (recommended)

```sh
cp .env.example .env
# Edit .env with your keys — for Last.fm (default):
#   LASTFM_API_KEY=xxx
#   LASTFM_USER=xxx
#   DISCORD_APP_ID=xxx
# — or for Navidrome, set MUSIC_SOURCE=navidrome plus the NAVIDROME_* vars
#   (see Navidrome setup below).
docker compose up -d
```

### docker run

```sh
# Last.fm (default)
docker run --rm -it \
  -e LASTFM_API_KEY=xxx \
  -e LASTFM_USER=xxx \
  -e DISCORD_APP_ID=xxx \
  -v lastfm-presence-token:/data \
  ghcr.io/frostplexx/lastfm-discord-presence:main

# Navidrome
docker run --rm -it \
  -e MUSIC_SOURCE=navidrome \
  -e NAVIDROME_HOST=https://music.example.com \
  -e NAVIDROME_ADMIN_USERNAME=admin \
  -e NAVIDROME_ADMIN_PASSWORD=xxx \
  -e NAVIDROME_USERNAME=your-navidrome-username \
  -e DISCORD_APP_ID=xxx \
  -v lastfm-presence-token:/data \
  ghcr.io/frostplexx/lastfm-discord-presence:main
```

### Available Environment variables

| Variable                   | Required                   | Default                    | Description                                                          |
| --------------------------- | --------------------------- | --------------------------- | ---------------------------------------------------------------------|
| `DISCORD_APP_ID`            | yes                          | —                           | Discord Application ID                                                |
| `MUSIC_SOURCE`              | no                           | `lastfm,navidrome`          | Comma-separated priority list: `lastfm,navidrome`, `lastfm`, `navidrome`, etc. Sources missing required env vars are skipped with a warning |
| `LASTFM_API_KEY`            | for lastfm | —                          | Last.fm API key                                                       |
| `LASTFM_USER`               | for lastfm | —                          | Last.fm username to poll                                              |
| `NAVIDROME_HOST`            | for navidrome | —                       | Base URL of your Navidrome server, e.g. `https://music.example.com`   |
| `NAVIDROME_ADMIN_USERNAME`  | for navidrome | —                       | Username of a Navidrome **admin** account (see notice below)          |
| `NAVIDROME_ADMIN_PASSWORD`  | for navidrome | —                       | Password for that admin account                                       |
| `NAVIDROME_USERNAME`        | for navidrome | —                       | The Navidrome username whose playback should be watched               |
| `SOURCE_POLL_INTERVAL_SEC`  | no                           | `10`                        | How often to poll the source (seconds)                                |
| `SOURCE_SHOW_BUTTON`        | no                           | `1`                         | Show the "View on ..." button                                         |
| `SOURCE_SHOW_SMALL_IMAGE`   | no                           | `1`                         | Show the source logo as a small image overlay (Last.fm only)          |
| `SOURCE_DISCONNECT_DELAY_SEC` | no                         | `30`                        | Seconds of no track from any source before disconnecting from Discord |
| `DISCORD_TOKEN_FILE`        | no                           | `~/.lastfm-discord-token`  | Path to saved OAuth token file                                         |

### Navidrome setup

> [!IMPORTANT]
> Only an **admin** account can see another user's now-playing status.
> Supply admin credentials via `NAVIDROME_ADMIN_USERNAME` /
> `NAVIDROME_ADMIN_PASSWORD` (never sent over the wire — used locally
> for [Subsonic token auth](https://www.navidrome.org/docs/developers/subsonic-api/)).

Set `MUSIC_SOURCE=navidrome` and the four `NAVIDROME_*` vars.
`NAVIDROME_HOST` must be reachable by Discord's servers too (they fetch
album art), so `localhost` won't work for artwork unless exposed.

Polls Navidrome's `getNowPlaying` Subsonic endpoint. Compatible with
Navidrome ≥0.49.0 (pre- and post-0.62.0 `reportPlayback` changes).

> Most Navidrome scrobblers don't send "now playing" heartbeats to
> Last.fm — only final scrobbles. If you only listen via Navidrome,
> use `MUSIC_SOURCE=navidrome` and skip the Last.fm env vars.

### First-time auth

On first run the daemon prints a URL + code. Open it on any device to authorize Discord → token is saved to the
persistent volume. Subsequent runs reuse the stored token.

## 🔧 Local Development

### Prerequisites

- **macOS** (or Linux)
- **C++20 compiler** (Clang 16+ or GCC 13+)
- **CMake** 3.16+
- **libcurl** (dev headers for build, runtime lib for execution)
- **Discord Social SDK** — download from [Discord Developer Portal](https://discord.com/developers/applications)
- **Last.fm API key** — get one at [last.fm/api](https://www.last.fm/api)
- **Discord Application** — create one at [Discord Developer Portal](https://discord.com/developers/applications)

### Discord Social SDK

Place the SDK at `lib/discord_social_sdk/`. The include headers and platform libraries are expected under:

```
lib/discord_social_sdk/
├── include/
│   ├── cdiscord.h
│   └── discordpp.h
├── lib/
│   ├── x86_64-linux/
│   ├── x86_64-darwin/
│   └── arm64-darwin/
```

The SDK is gitignored — you must acquire it from Discord. The GitHub Actions workflow fetches it automatically via `DISCORD_SDK_URL`.

### Build and run (macOS)

```sh
just build
just sign              # ad-hoc sign dylib + binary (required once on macOS)

LASTFM_API_KEY=xxx LASTFM_USER=xxx DISCORD_APP_ID=xxx ./build/lastfm-discord-presence

# Or use the just run target after setting env vars:
export LASTFM_API_KEY=xxx LASTFM_USER=xxx DISCORD_APP_ID=xxx
just run
```

### Build Docker image locally

```sh
just docker-build
```

> `just docker-build` temporarily replaces the symlink at `lib/discord_social_sdk`
> with real files so Docker can include them in the build context.
> The symlink is restored when the build finishes.


## Project structure

```
├── CMakeLists.txt              # CMake build (handles macOS/Linux/Windows + SDK paths)
├── Dockerfile                  # Multi-stage Docker build
├── docker-compose.yml          # Docker Compose deployment
├── flake.nix                   # Nix dev shell (alternative dev env)
├── justfile                    # Task runner (build, run, sign, docker, etc.)
├── lib/
│   ├── discord_social_sdk/     # → Discord Social SDK (gitignored symlink/download)
│   ├── json.hpp                # Vendored nlohmann/json single header
│   └── nlohmann/json.hpp       # Copy for <nlohmann/json.hpp> include
├── src/
│   ├── main.cpp                     # Entry point, OAuth flow, poll loop, presence updates
│   ├── music_sources/
│   │   ├── music_source.h           # MusicSource interface + MultiSource + SourceBranding
│   │   ├── music_source.cpp         # MultiSource implementation
│   │   ├── track.h                  # Shared Track struct + TrackId
│   ├── lastfm.h / lastfm.cpp        # Last.fm API client + LastfmSource
│   ├── navidrome.h / navidrome.cpp  # Navidrome (Subsonic API) client + NavidromeSource
│   ├── md5.h                        # Minimal MD5 (for Subsonic token auth)
│   ├── store.h / store.cpp          # Token persistence (access + refresh tokens)
├── .github/workflows/docker.yml
└── README.md
```

## License

This project is MIT-licensed. The Discord Social SDK is subject to Discord's own license terms.
