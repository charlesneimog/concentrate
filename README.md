<p align="center">
  <img src="https://github.com/charlesneimog/Concentrate/blob/master/resources/favicon.svg" alt="Logo" width=130 height=130>
  <h1 align="center">Concentrate</h1>
</p>

Concentrate is a Linux background service that tracks focused/unfocused time based on the active window, stores activity in SQLite, and serves a local web UI. It integrates with Anytype to load task metadata and uses DBus notifications for reminders and warnings.

<details>
  <summary><strong>Preview</strong></summary>

  <p align="center">
    <img src="https://github.com/charlesneimog/Concentrate/blob/master/resources/webui.png">
  </p>
</details>



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
cmake --install build
```

## Run

```sh
concentrate
```

Optional logging flags:

- `--logdebug`
- `--loginfo`
- `--logoff` (default)

The server listens by default on http://localhost:7079. Use `--port` to change it.

## Install

```sh
cmake --install build
```


## Data and secrets

- SQLite database: `~/.local/share/concentrate/data.sqlite`
- Secrets are stored in the default libsecret keyring under the schema `io.Concentrate.Secret`

## External services

Hydration recommendations use:

- http://ip-api.com/json (location)
- https://api.open-meteo.com (weather)

## API overview

All endpoints are served from the same local server.

## Notes

- Only Niri is currently supported for focused window detection.
- The service must run in a user session with DBus access to send notifications.

## Academic Articles

* Wirzberger, Maria, Anastasia Lado, Mike Prentice, et al. “Optimal Feedback Improves Behavioral Focus during Self-Regulated Computer-Based Work.” Scientific Reports 14, no. 1 (2024): 3124. https://doi.org/10.1038/s41598-024-53388-3.
* Almoallim, Sultan, and Corina Sas. “Toward Research-Informed Design Implications for Interventions Limiting Smartphone Use: Functionalities Review of Digital Well-Being Apps.” JMIR Formative Research 6, no. 4 (2022): e31730. https://doi.org/10.2196/31730.


## TODO

- [ ] Search about WS implementation
- [ ] Implement Hyprland, Sway
