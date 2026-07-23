# Plan: lastfm-discord-presence

## Overview

A minimal C++ daemon that polls the Last.fm API for a user's currently scrobbling track and sets it as their Discord Rich Presence via the Discord Social SDK's authenticated (server-connected) path. Works with Discord closed — no local RPC/IPC needed.

## Files changed

| File | Purpose |
|---|---|
| `CMakeLists.txt` | CMake build; links Discord Social SDK, libcurl, nlohmann/json |
| `src/main.cpp` | Entry point: config, OAuth flow, poll loop, rich presence updates |
| `src/lastfm.h` | Header for Last.fm API client |
| `src/lastfm.cpp` | HTTP GET to `user.getRecentTracks`, returns current track or nil |
| `src/store.h` | Header for token persistence |
| `src/store.cpp` | Read/write refresh token from `/tmp/lastfm-discord-token` |

## Implementation steps

### Step 1 — Project scaffold and build system

- Create `CMakeLists.txt` targeting C++20, linking:
  - `discord_partner_sdk` (from `lib/discord_social_sdk/`)
  - `libcurl` (system or brew)
  - `nlohmann/json` (FetchContent or system)
- Copy the SDK shared lib to the build output dir (per the getting-started pattern).

### Step 2 — Config from environment

- Last.fm env vars: `LASTFM_API_KEY`, `LASTFM_USER`
- Discord env var: `DISCORD_APP_ID`
- Optional: `DISCORD_TOKEN_FILE` (default `~/.lastfm-discord-token`)
- Parse at startup; exit with usage message if required vars missing.

### Step 3 — Token persistence (`src/store.h`, `src/store.cpp`)

- `TokenStore::Load()`: read refresh token from `/tmp/lastfm-discord-token`
- `TokenStore::Save(refreshToken)`: write refresh token to same file
- `TokenStore::Clear()`: remove file (used on auth failure / revocation)

### Step 4 — Last.fm API client (`src/lastfm.h`, `src/lastfm.cpp`)

**Endpoint:** `user.getRecentTracks`

```
GET https://ws.audioscrobbler.com/2.0/
  ?method=user.getrecenttracks
  &user={lastfm_username}
  &api_key={your_api_key}
  &limit=1
  &format=json
```

**Why this endpoint?** It's the only Last.fm endpoint that tells us what a user is listening to *right now*. `track.getInfo` requires a known track, and `auth` endpoints require user sessions. This one is public (API key only), returns the current track with `@attr.nowplaying="true"` when scrobbling, and falls back to the most recent scrobble otherwise.

**Response shape:**

Scrobbling now → first track has `@attr.nowplaying: "true"`, no `date`:
```json
{
  "recenttracks": {
    "track": [{
      "artist": {"#text": "Radiohead"},
      "name": "Paranoid Android",
      "album": {"#text": "OK Computer"},
      "@attr": {"nowplaying": "true"}
    }]
  }
}
```

Nothing playing → first track has `date` (last scrobble time), no `@attr`:
```json
{
  "recenttracks": {
    "track": [{
      "artist": {"#text": "Radiohead"},
      "name": "Paranoid Android",
      "album": {"#text": "OK Computer"},
      "date": {"uts": "1719000000", "#text": "..."}
    }]
  }
}
```

Edge cases: empty `track[]` (user never scrobbled → `nullopt`), empty `album.#text` (no album tag → sent as ""), error JSON with `error`/`message` fields (logged, return `nullopt`).

**Rate limit:** ~5 req/s per IP (unauthenticated). At 10s intervals we're well under.

- `LastfmClient::NowPlaying(apiKey, user) -> std::optional<Track>`
- Libcurl wrapped in a simple RAII class.

### Step 5 — Discord Social SDK integration (`src/main.cpp`)

Follow the Getting Started pattern with these differences:

- **No RPC path.** Use full OAuth with `GetDefaultPresenceScopes` so presence updates go through Discord servers.
- **Token reuse.** On start, try `TokenStore::Load()`. If a refresh token exists, call `RefreshToken` instead of `Authorize`. If refresh fails (invalid_grant → try provisional → error 530010), fall through to fresh OAuth.
- **First-time auth.** Open browser for OAuth, exchange code for tokens, persist refresh token via `TokenStore::Save()`.
- **Presence update on Ready.** After `Connect()` reaches `Status::Ready`, start the poll loop.

### Step 6 — Poll loop and rich presence updates

- Every 10 seconds (configurable via `LASTFM_POLL_INTERVAL_SEC` env var), call `LastfmClient::NowPlaying()`.
- Track previous `{artist, track}` hash. Only call `UpdateRichPresence` when the track actually changes.
- Activity fields:
  - `type`: `Playing`
  - `details`: `"{track}" — {artist}` (e.g. `"Paranoid Android — Radiohead"`)
  - `state`: `"scrobbling on Last.fm"`
  - `timestamps`: `SetStart` to `time(nullptr)` (so timer shows elapsed listening time)
- On `nullopt` (nothing playing): clear presence by setting `details` and `state` to empty (or set a "paused" state).
- Loop runs on the main thread with `sleep_for`; `RunCallbacks()` called each iteration.

### Step 7 — Clean shutdown

- `SIGINT`/`SIGTERM` handler sets `running = false`.
- On exit, `UpdateRichPresence` with cleared activity (optional, polite).

## Testing plan

1. Unit: Last.fm JSON parsing with known-good and known-bad responses.
2. Smoke: Run with valid env vars, observe first-time OAuth flow in browser, confirm presence appears on Discord profile.
3. Idle: Stop playing music on Last.fm, confirm presence clears after next poll.
4. Restart: Kill and re-run; confirm token file is used (no browser re-auth).
5. Error: Set bad `LASTFM_USER`, confirm graceful log + `nullopt` loop, no crash.

## Rollout

Single binary. No flags, no servers, no migration. Just env vars and a token file.
