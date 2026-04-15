# MR60BHA2 UART Protocol Reference

This document describes the binary UART protocol used by the Seeed Studio MR60BHA2
60 GHz mmWave sensor (firmware v1.6.12). It was reverse-engineered by passive
capture and cross-referenced with Seeed's Tiny Frame Interface datasheet.

## Physical Layer

- **Baud rate**: 115200
- **Format**: 8N1
- **Connector**: 5-pin, 1.25 mm pitch (TX, RX, GND, 3.3 V, VCC)

## Frame Structure

```
[SOF:1] [ID:2] [LEN:2] [TYPE:2] [HEAD_CKSUM:1] [DATA:LEN] [DATA_CKSUM:1]
```

| Field       | Bytes | Description                                                |
|-------------|-------|------------------------------------------------------------|
| SOF         | 1     | Start of frame, always `0x01`                             |
| ID          | 2     | Auto-incrementing frame ID (starts at `0x8000`), big-endian |
| LEN         | 2     | Length of DATA section in bytes, big-endian                |
| TYPE        | 2     | Frame type identifier, big-endian (see table below)        |
| HEAD_CKSUM  | 1     | `~(XOR of all header bytes including SOF, ID, LEN, TYPE)` |
| DATA        | LEN   | Payload, format depends on TYPE                            |
| DATA_CKSUM  | 1     | `~(XOR of all DATA bytes)`                                 |

**Checksum formula**: `result = ~(byte0 ^ byte1 ^ ... ^ byteN) & 0xFF`

## Frame Types (documented)

### `0x0A13` — Heart & Breath Phases

**Length**: 12 bytes | **Rate**: ~8 Hz | **Range**: 0–1.5 m

| Bytes | Type    | Description                         |
|-------|---------|-------------------------------------|
| 0–3   | float32 | Total phase (radians, little-endian) |
| 4–7   | float32 | Breath phase (radians)              |
| 8–11  | float32 | Heart phase (radians)               |

### `0x0A14` — Breath Rate

**Length**: 4 bytes | **Rate**: ~8 Hz | **Range**: 0–1.5 m

| Bytes | Type    | Description                              |
|-------|---------|------------------------------------------|
| 0–3   | float32 | Breath rate in breaths/minute (little-endian) |

Returns `0.0` when no subject is within vital-signs range.

### `0x0A15` — Heart Rate

**Length**: 4 bytes | **Rate**: ~8 Hz | **Range**: 0–1.5 m

| Bytes | Type    | Description                                  |
|-------|---------|----------------------------------------------|
| 0–3   | float32 | Heart rate in beats per minute (little-endian) |

Returns `0.0` when no subject is within vital-signs range.

### `0x0A16` — Distance

**Length**: 8 bytes | **Rate**: ~8 Hz | **Range**: 0–6 m

| Bytes | Type    | Description                                               |
|-------|---------|-----------------------------------------------------------|
| 0–3   | float32 | Distance to target in **centimetres** (little-endian)     |
| 4–7   | uint32  | Range flag: `1` = valid target, `0` = no target           |

### `0x0F09` — Human Detection

**Length**: 4 bytes | **Rate**: ~8 Hz | **Range**: 0–6 m

| Bytes | Type   | Description                            |
|-------|--------|----------------------------------------|
| 0–3   | uint32 | `1` = human detected, `0` = no human  |

### `0x0A04` — 3D Point Cloud Target Info

**Length**: variable (16 bytes × N targets) | **Rate**: ~8 Hz | **Max targets**: 3

Each target is 16 bytes:

| Bytes | Type    | Description                                                  |
|-------|---------|--------------------------------------------------------------|
| 0–3   | float32 | X coordinate in **metres** (little-endian, signed)           |
| 4–7   | float32 | Y coordinate in **metres** (little-endian, signed)           |
| 8–11  | int32   | Doppler index (little-endian); velocity = `dop_index × 17.28 cm/s` |
| 12–15 | int32   | Cluster ID (little-endian); stable ID for target tracking    |

**Coordinate system**: Y-axis points away from the sensor face. X-axis is left/right.

### `0x0A08` — 3D Point Cloud Detection

Same structure as `0x0A04`. Used during the detection phase; often contains fewer
or no targets. Exact semantic difference is not fully understood.

### `0xFFFF` — Firmware Version

**Length**: 12 bytes | **Triggered by**: command only (not spontaneous)

| Bytes | Type   | Description                |
|-------|--------|----------------------------|
| 0–3   | uint32 | Project number             |
| 4–7   | uint32 | Major version              |
| 8–9   | uint16 | Sub version                |
| 10–11 | uint16 | Modified version           |

Example: project=1, major=1, sub=6, modified=12 → firmware v1.6.12

## Frame Types (undocumented, observed on v1.6.12)

These types were discovered by logging all unknown incoming frames. They are not
mentioned in any Seeed documentation.

### `0x0100` — Debug Log

**Length**: 23–24 bytes | **Rate**: ~8 Hz

ASCII string from the sensor's internal firmware, null-terminated:

```
"breath rate = 23.000000 "
"heart rate = 94.000000 "
```

These appear to mirror the values from `0x0A14`/`0x0A15`, formatted as `printf`
output from inside the sensor DSP. Useful for debugging without a host parser.

### `0x0A17` — Alternative Position

**Length**: 8 bytes | **Rate**: ~8 Hz

| Bytes | Type    | Description                                     |
|-------|---------|--------------------------------------------------|
| 0–3   | float32 | X coordinate in metres (little-endian)           |
| 4–7   | float32 | Y coordinate in metres (little-endian)           |

Values correlate roughly with the first target in `0x0A04`. Hypothesis: this is
the raw, unfiltered position before the internal tracking algorithm runs.

### `0x0A29` — Status Code

**Length**: 2 bytes | **Rate**: ~8 Hz

| Bytes | Type   | Description                           |
|-------|--------|---------------------------------------|
| 0–1   | uint16 | Alternates between `1` and `2`        |

Hypothesis: `1` = idle/scanning, `2` = actively tracking a target. Not confirmed.

## Behaviour Notes

- **Warm-up**: The sensor requires approximately 60 seconds after power-on before
  vital-signs readings stabilise. Presence detection works immediately.
- **Simultaneous output**: All frame types are sent continuously at ~8 Hz. There
  is no mode switch to select "vital signs" vs "presence" — both are always active.
- **Vital-signs range limit**: `0x0A14` and `0x0A15` return `0.0` when the subject
  is beyond ~1.5 m. `0x0F09` and `0x0A04` continue to work up to ~6 m.
- **Multiple targets**: Up to 3 targets in `0x0A04`. The Cluster ID is stable
  across frames and can be used for trajectory tracking.

## ESPHome Gap

The official
[ESPHome external component](https://github.com/limengdu/MR60BHA2_ESPHome_external_components)
exposes `has_target`, `breath_rate`, `heart_rate`, `distance`, and `num_targets`.

**Not exposed** (but available in the protocol):

- `0x0A13` — heart/breath phases
- `0x0A04` — per-target X/Y coordinates and Doppler velocity
- `0x0A04` — Cluster IDs (target tracking identity)
- `0x0A08` — point cloud detection phase
- `0xFFFF` — firmware version

## References

- [Seeed Studio mmWave Kit Wiki](https://wiki.seeedstudio.com/getting_started_with_mr60bha2_mmwave_kit/)
- [Tiny Frame Interface datasheet (PDF)](https://files.seeedstudio.com/wiki/mmwave-for-xiao/mr60/datasheet/Seeed_Studio_Tiny_Frame_Interface_Breathing_and_Heartbeat.pdf)
- [MR60BHA2 hardware datasheet (PDF)](https://files.seeedstudio.com/wiki/mmwave-for-xiao/mr60/datasheet/MR60BHA2_Breathing_and_Heartbeat_Module.pdf)
