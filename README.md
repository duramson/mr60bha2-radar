# mr60bha2-radar

[![Build](https://github.com/duramson/mr60bha2-radar/actions/workflows/ci.yml/badge.svg)](https://github.com/duramson/mr60bha2-radar/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

ESP-IDF firmware for the [Seeed Studio MR60BHA2](https://wiki.seeedstudio.com/getting_started_with_mr60bha2_mmwave_kit/)
60 GHz mmWave sensor kit (XIAO ESP32C6 + MR60BHA2).

Streams all sensor data — tracked targets, raw point cloud, vital signs, Doppler
velocity, presence detection, and three undocumented frame types — to any WebSocket
client in real time. Ships with [radar-dash](https://github.com/duramson/radar-dash)
embedded into the firmware binary: flash the board, open `http://<IP>`, and the
full dashboard loads instantly. No Raspberry Pi, no daemon, no second server.

## Quick Start

> **From unboxing to live radar data in your browser — 4 steps.**

**Step 1 — Install PlatformIO**

Install the [PlatformIO VS Code extension](https://platformio.org/install/ide?install=vscode)
or the CLI (`pip install platformio`).

**Step 2 — Configure WiFi**

```bash
git clone https://github.com/duramson/mr60bha2-radar
cd mr60bha2-radar
pio run -t menuconfig
# Navigate to: MR60BHA2 Radar → WiFi SSID / WiFi Password
# Save and exit (S → Enter → Q)
```

**Step 3 — Flash**

Connect the XIAO ESP32C6 via USB-C, then:

```bash
pio run -t upload
pio device monitor
# Watch for: "WiFi connected, IP: 192.168.1.xx"
```

**Step 4 — Open in browser**

Open **`http://192.168.1.xx`** — radar-dash is served directly from the ESP,
so the full dashboard loads immediately. The vitals panel appears automatically
once someone is within 1.5 m of the sensor.

### Dashboard

The firmware embeds [radar-dash](https://github.com/duramson/radar-dash) (the
standalone HTML5 dashboard) straight into the binary and serves it at `/`.
You do **not** need to clone radar-dash, run a second server, or pass any URL
parameters — just open `http://<IP>` in any browser.

When opened from the ESP itself, radar-dash defaults to `ws://<page-host>/ws`,
which resolves to the firmware's own WebSocket endpoint.

**Advanced:** if you prefer to run radar-dash from elsewhere (your laptop,
GitHub Pages, etc.) and point it at the sensor, override the WebSocket URL:

```
http://<radar-dash-host>/?ws=ws://192.168.1.xx/ws
```

---

## Features

- Parses all known MR60BHA2 frame types (including three undocumented ones)
- Outputs everything the sensor provides — tracked targets, raw point cloud, vitals, presence, alternative position, status code
- [radar-dash](https://github.com/duramson/radar-dash) dashboard embedded into the firmware — open `http://<IP>` and it just works
- 5 Hz WebSocket broadcast (200 ms aggregation window)
- WebSocket schema is a superset of the radar-dash schema — drop-in compatible with external radar-dash instances
- Pure ESP-IDF (no Arduino, no ESPHome), ~50 KB RAM footprint
- WiFi credentials via `idf.py menuconfig` — no credentials in source
- OTA firmware updates (dual flash slots, automatic rollback)

## Hardware

| Component | Details |
|-----------|---------|
| MCU board | [XIAO ESP32C6](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/) |
| Radar module | MR60BHA2, 60 GHz FMCW |
| Connection | Integrated: plug the radar board onto the XIAO |
| Power | USB-C (5 V → 3.3 V on-board) |

**Pin assignment** (ESP32C6 ↔ MR60BHA2):

| Signal  | GPIO |
|---------|------|
| UART TX | 16   |
| UART RX | 17   |

## Build & Flash

**Requirements**: [PlatformIO](https://platformio.org/) — the ESP-IDF toolchain is
downloaded automatically on the first build (this takes a few minutes the first time).

```bash
# 1. Configure WiFi credentials
pio run -t menuconfig
# → MR60BHA2 Radar → WiFi SSID / WiFi Password

# 2. Build and flash
pio run -t upload

# 3. Find the IP address in the serial log
pio device monitor

# 4. Open in browser
# http://<IP-address>
```

If your WiFi password contains special characters (e.g. `#`), pass credentials
as build-time variables instead of menuconfig:

```bash
idf.py -DCONFIG_WIFI_SSID="myssid" -DCONFIG_WIFI_PASS='my#pass' build
```

### OTA Updates

After the initial flash, firmware can be updated over WiFi. Edit `platformio.ini`
to set `custom_ota_ip` to your device's current IP, then:

```bash
pio run -e ota -t upload
```

The firmware boots the new image automatically and rolls back if it fails to start.

## WebSocket JSON Format

Each frame broadcast to connected clients contains all data the sensor has
produced since the last broadcast. Fields gated on `has_*` flags are omitted
until the sensor emits the corresponding frame type for the first time.

```json
{
  "targets": [
    {
      "x": 0.45,
      "y": 1.10,
      "speed": -0.17,
      "dist": 1.19,
      "angle": 22.2,
      "cluster_id": 1
    }
  ],
  "point_cloud": [
    {
      "x": 0.44,
      "y": 1.09,
      "speed": -0.17,
      "dist": 1.18,
      "angle": 21.9,
      "cluster_id": 1
    }
  ],
  "vitals": {
    "heart_rate": 71.0,
    "breath_rate": 16.5,
    "heart_phase": 1.83,
    "breath_phase": 0.42,
    "total_phase": 2.15,
    "distance": 1.10,
    "range_flag": 1
  },
  "presence": true,
  "alt_pos": { "x": 0.44, "y": 1.09 },
  "status_code": 3
}
```

| Field | Unit | Source frame | Notes |
|-------|------|-------------|-------|
| `targets[].x` | metres | 0x0A04 | positive = right of sensor |
| `targets[].y` | metres | 0x0A04 | positive = forward from sensor |
| `targets[].speed` | m/s | 0x0A04 | positive = approaching, negative = receding |
| `targets[].dist` | metres | computed | Euclidean distance `√(x²+y²)` |
| `targets[].angle` | degrees | computed | from boresight: `atan2(x, y)` |
| `targets[].cluster_id` | integer | 0x0A04 | sensor's cluster assignment |
| `point_cloud` | — | 0x0A08 | raw detection cloud before clustering; omitted when empty |
| `vitals.heart_rate` | BPM | 0x0A15 | omitted until first vitals frame |
| `vitals.breath_rate` | BPM | 0x0A14 | |
| `vitals.heart_phase` | radians | 0x0A13 | |
| `vitals.breath_phase` | radians | 0x0A13 | |
| `vitals.total_phase` | radians | 0x0A13 | combined phase signal |
| `vitals.distance` | metres | 0x0A16 | sensor's effective range estimate |
| `vitals.range_flag` | integer | 0x0A16 | range zone indicator |
| `presence` | boolean | 0x0F09 | human presence detected |
| `alt_pos` | metres | 0x0A17 | alternative position output (undocumented); omitted when absent |
| `status_code` | integer | 0x0A29 | tracking phase indicator (undocumented); omitted when absent |

Up to **3 targets** tracked simultaneously.

> **Vitals vs. targets**
>
> | Capability | Range | Notes |
> |---|---|---|
> | Target tracking | up to 3 m | Position, speed, cluster ID for up to 3 people |
> | Vital signs | ≤ 1.5 m | One global measurement — not per-target |
>
> Vitals report a single heart/breath rate for whoever is closest.
> Presence-detection sensitivity may decrease when vitals mode is active.

### Compatibility with radar-dash

[radar-dash](https://github.com/duramson/radar-dash) reads the `targets`, `vitals`,
and `presence` fields. The additional fields (`point_cloud`, `alt_pos`, `status_code`)
are ignored by that dashboard but available for custom consumers connecting to
`ws://<IP>/ws`.

## Architecture

```
MR60BHA2 sensor
     │ UART 115200
     ▼
┌──────────────────────────────────────────────────────┐
│  ESP32C6 firmware                                     │
│                                                       │
│  UART RX task ──► mr60bha2 parser ──► sensor_data    │
│                         (state machine)     (shared)  │
│                                              │        │
│  WS broadcast task (200 ms) ◄───────────────┘        │
│       │                                               │
│       ▼ JSON over WebSocket                           │
│  ESP-IDF httpd                                        │
│  ├── GET /        → embedded radar-dash index.html    │
│  ├── WS  /ws      → frame stream                     │
│  └── POST /ota    → firmware update                   │
└──────────────────────────────────────────────────────┘
         │ WebSocket  ws://<IP>/ws
         ▼
    Browser (embedded radar-dash, or external radar-dash via ?ws=)
```

## Protocol Documentation

See [PROTOCOL.md](PROTOCOL.md) for a complete description of the MR60BHA2 binary
UART protocol, including three undocumented frame types discovered by passive
capture that are missing from all official documentation.

## Comparison with ESPHome Integration

The [official ESPHome component](https://github.com/limengdu/MR60BHA2_ESPHome_external_components)
exposes only a subset of the sensor's capabilities.
This firmware exposes everything:

| Data | ESPHome | This firmware |
|------|---------|--------------|
| Human detection | ✅ | ✅ |
| Breath rate | ✅ | ✅ |
| Heart rate | ✅ | ✅ |
| Distance | ✅ | ✅ |
| Target count | ✅ | ✅ |
| X/Y coordinates | ❌ | ✅ |
| Doppler velocity | ❌ | ✅ |
| Cluster ID | ❌ | ✅ |
| Heart/breath phases | ❌ | ✅ |
| Total phase | ❌ | ✅ |
| Raw point cloud | ❌ | ✅ |
| Undocumented frames (0x0A17, 0x0A29) | ❌ | ✅ |

## Related Projects

| Project | Description |
|---------|-------------|
| [radar-dash](https://github.com/duramson/radar-dash) | Standalone HTML5 radar dashboard. Already embedded in this firmware; the upstream repo lets you run it from elsewhere and point it at any sensor's `ws://<IP>/ws`. |
| [mr60bha2-rs](https://github.com/duramson/mr60bha2-rs) | Rust daemon for the same sensor connected directly to a Raspberry Pi (no XIAO needed). Same JSON schema. |
| [ld2450-rs](https://github.com/duramson/ld2450-rs) | Equivalent Rust daemon for the HLK-LD2450 24 GHz radar — position + Doppler, no vital signs. |

## Sensor Specs

| Parameter | Value |
|-----------|-------|
| Frequency | 60 GHz ISM band |
| Max detection range | 3 m |
| Vital signs range | ≤ 1.5 m |
| Detection angle | ±60° azimuth |
| Max targets | 3 simultaneous |
| Data rate | ~5 Hz (200 ms broadcast window) |
| UART interface | 115200 baud 8N1 |
| Supply voltage | 5V DC (USB-C) |

## License

MIT — see [LICENSE](LICENSE).
