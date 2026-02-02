# Concentrate

Concentrate is a Linux background service that tracks focused/unfocused time based on the active window, stores activity in SQLite, and serves a local web UI. It integrates with Anytype to load task metadata and uses DBus notifications for reminders and warnings.

## Features

- Tracks focused vs unfocused time per application window
- Daily activities and recurring tasks with categories
- Anytype integration for tasks and allowed apps/titles
- Local web UI served from the binary
- Hydration reminders with basic weather-based recommendations
- SQLite storage and JSON API endpoints

## Requirements

- Linux with Niri window manager (uses NIRI IPC via NIRI_SOCKET)
- CMake 3.21+
- C++20 compiler
- pkg-config
- SQLite3
- libsecret-1
- dbus-1

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Run

```sh
./build/Concentrate
```

Optional logging flags:

- `--logdebug`
- `--loginfo`
- `--logoff` (default)

The server listens on http://localhost:7079.

## Install

```sh
cmake --install build
```

This installs the binary to `/usr/local/bin` and web assets to `/usr/local/share/Concentrate`.

## Data and secrets

- SQLite database: `~/.local/Concentrate/data.sqlite`
- Secrets are stored in the default libsecret keyring under the schema `io.Concentrate.Secret`

## External services

Hydration recommendations use:

- http://ip-api.com/json (location)
- https://api.open-meteo.com (weather)

## API overview

All endpoints are served from the same local server.

### Anytype

- POST /api/v1/anytype/auth/challenges
- POST /api/v1/anytype/auth/api_keys
- GET  /api/v1/anytype/spaces
- POST /api/v1/anytype/space
- GET  /api/v1/anytype/tasks
- GET  /api/v1/anytype/tasks_categories

### Focus and monitoring

- GET  /api/v1/current
- GET  /api/v1/focus/today?days=1 # FIX THIS
- GET  /api/v1/focus/today/categories
- GET  /api/v1/focus/category-percentages?days=1
- GET  /api/v1/focus/app-usage?days=1
- POST /api/v1/focus/rules
- GET  /api/v1/monitoring
- POST /api/v1/monitoring

### History

- GET  /api/v1/history
- GET  /api/v1/history/category-time?days=30
- GET  /api/v1/history/category-focus?days=30

### Recurring tasks

- GET    /api/v1/task/recurring_tasks
- POST   /api/v1/task/recurring_tasks
- DELETE /api/v1/task/recurring_tasks?name=TaskName

### Task selection

- POST /api/v1/task/set_current

### Special project override

- POST /api/v1/special_project (used for neovim plugin)

## Notes

- Only Niri is currently supported for focused window detection.
- The service must run in a user session with DBus access to send notifications.

## TODO

- [ ] Decrease memory use
- [ ] Search about WS implementation
- [ ] Implement Hyprland, Sway
