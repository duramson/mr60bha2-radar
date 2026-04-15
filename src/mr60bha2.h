/**
 * @file mr60bha2.h
 * @brief MR60BHA2 60GHz mmWave Sensor - Full telemetry parser (ESP-IDF)
 *
 * Frame protocol:
 *   [SOF:1] [ID:2] [LEN:2] [TYPE:2] [HEAD_CKSUM:1] [DATA:LEN] [DATA_CKSUM:1]
 *   SOF = 0x01
 *   Checksum = ~(XOR of all bytes)
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Frame constants ────────────────────────────────────────────────── */
#define MR60_SOF                  0x01
#define MR60_HEADER_SIZE          8   /* SOF+ID+LEN+TYPE+HEAD_CKSUM */
#define MR60_MAX_DATA_LEN         256
#define MR60_MAX_FRAME_LEN        (MR60_HEADER_SIZE + MR60_MAX_DATA_LEN + 1)
#define MR60_MAX_TARGETS          3
#define MR60_RANGE_STEP           17.28f  /* cm/s per dop_index unit */

/* ── Frame type IDs ─────────────────────────────────────────────────── */
#define MR60_TYPE_HEART_BREATH_PHASE    0x0A13
#define MR60_TYPE_BREATH_RATE           0x0A14
#define MR60_TYPE_HEART_RATE            0x0A15
#define MR60_TYPE_DISTANCE              0x0A16
#define MR60_TYPE_POINT_CLOUD_DETECT    0x0A08
#define MR60_TYPE_POINT_CLOUD_TARGET    0x0A04
#define MR60_TYPE_HUMAN_DETECT          0x0F09
#define MR60_TYPE_FIRMWARE              0xFFFF
/* Undocumented types discovered via protocol scan */
#define MR60_TYPE_POSITION_ALT          0x0A17  /* 2 floats: x, y (alternative/raw position) */
#define MR60_TYPE_STATUS_CODE           0x0A29  /* uint16: status/tracking phase indicator */
#define MR60_TYPE_DEBUG_LOG             0x0100  /* ASCII debug string from firmware */

/* ── Data structures ────────────────────────────────────────────────── */
typedef struct {
    float x;
    float y;
    int32_t dop_index;
    int32_t cluster_index;
} mr60_target_t;

typedef struct {
    /* Vital signs (valid < 1.5m) */
    float total_phase;
    float breath_phase;
    float heart_phase;
    float breath_rate;
    float heart_rate;
    float distance;
    uint32_t range_flag;

    /* Presence */
    bool human_detected;

    /* Targets */
    mr60_target_t targets[MR60_MAX_TARGETS];
    uint32_t target_count;

    /* Point cloud detection */
    mr60_target_t point_cloud[MR60_MAX_TARGETS];
    uint32_t point_cloud_count;

    /* Undocumented: alternative position (0x0A17) */
    float alt_x;
    float alt_y;
    bool has_alt_pos;

    /* Undocumented: status code (0x0A29) */
    uint16_t status_code;
    bool has_status;

    /* Undocumented: debug log (0x0100) */
    char debug_log[64];
    bool has_debug_log;

    /* Firmware */
    uint32_t firmware_raw;
    uint8_t fw_project;
    uint8_t fw_major;
    uint8_t fw_sub;
    uint8_t fw_modified;

    /* Validity flags – set when a frame of that type arrives */
    bool has_phases;
    bool has_breath_rate;
    bool has_heart_rate;
    bool has_distance;
    bool has_human;
    bool has_targets;
    bool has_point_cloud;
    bool has_firmware;

    /* Stats */
    uint32_t frames_ok;
    uint32_t frames_err;
    uint32_t frames_unknown;
    uint16_t last_unknown_type;
} mr60_data_t;

/* ── Parser state machine ───────────────────────────────────────────── */
typedef enum {
    MR60_PARSE_SOF,
    MR60_PARSE_HEADER,
    MR60_PARSE_DATA,
    MR60_PARSE_DATA_CKSUM,
} mr60_parse_state_t;

typedef struct {
    mr60_parse_state_t state;
    uint8_t buf[MR60_MAX_FRAME_LEN];
    uint16_t pos;
    uint16_t data_len;       /* extracted from header bytes 3-4 */
    mr60_data_t *data;       /* pointer to shared data store */
} mr60_parser_t;

/* ── API ────────────────────────────────────────────────────────────── */

/**
 * Initialize the parser.
 * @param p   Parser context
 * @param out Pointer to data structure that will be updated on each valid frame
 */
void mr60_parser_init(mr60_parser_t *p, mr60_data_t *out);

/**
 * Feed raw UART bytes into the parser.  Call from the UART rx task.
 * Parsed frames update the mr60_data_t pointed to during init.
 *
 * @param p    Parser context
 * @param data Raw bytes from UART
 * @param len  Number of bytes
 */
void mr60_parser_feed(mr60_parser_t *p, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
