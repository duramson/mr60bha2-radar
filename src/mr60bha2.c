/**
 * @file mr60bha2.c
 * @brief MR60BHA2 frame parser – state-machine based, zero-copy where possible
 */
#include "mr60bha2.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "mr60";

/* ── helpers ────────────────────────────────────────────────────────── */

static uint8_t calc_checksum(const uint8_t *data, size_t len) {
    uint8_t cs = 0;
    for (size_t i = 0; i < len; i++) cs ^= data[i];
    return ~cs;
}

static inline float extract_float(const uint8_t *b) {
    float v;
    memcpy(&v, b, sizeof(float));
    return v;
}

static inline uint32_t extract_u32(const uint8_t *b) {
    uint32_t v;
    memcpy(&v, b, sizeof(uint32_t));
    return v;
}

static inline uint16_t extract_u16_be(const uint8_t *b) {
    return (uint16_t)(b[0] << 8) | b[1];
}

/* ── frame handler ──────────────────────────────────────────────────── */

static void process_frame(mr60_data_t *d, uint16_t type,
                          const uint8_t *data, uint16_t len) {
    switch (type) {

    case MR60_TYPE_HEART_BREATH_PHASE:
        if (len >= 12) {
            d->total_phase  = extract_float(data);
            d->breath_phase = extract_float(data + 4);
            d->heart_phase  = extract_float(data + 8);
            d->has_phases = true;
        }
        break;

    case MR60_TYPE_BREATH_RATE:
        if (len >= 4) {
            d->breath_rate = extract_float(data);
            d->has_breath_rate = true;
        }
        break;

    case MR60_TYPE_HEART_RATE:
        if (len >= 4) {
            d->heart_rate = extract_float(data);
            d->has_heart_rate = true;
        }
        break;

    case MR60_TYPE_DISTANCE:
        if (len >= 8) {
            d->range_flag = extract_u32(data);
            d->distance   = extract_float(data + 4);
            d->has_distance = true;
        }
        break;

    case MR60_TYPE_HUMAN_DETECT:
        if (len >= 1) {
            d->human_detected = data[0] != 0;
            d->has_human = true;
        }
        break;

    case MR60_TYPE_POINT_CLOUD_TARGET: {
        if (len < 4) break;
        uint32_t n = extract_u32(data);
        if (n > MR60_MAX_TARGETS) n = MR60_MAX_TARGETS;
        const uint8_t *p = data + 4;
        /* each target: 4*float-sized fields = 16 bytes */
        if (len < 4 + n * 16) break;
        d->target_count = n;
        for (uint32_t i = 0; i < n; i++) {
            d->targets[i].x             = extract_float(p);      p += 4;
            d->targets[i].y             = extract_float(p);      p += 4;
            d->targets[i].dop_index     = (int32_t)extract_u32(p); p += 4;
            d->targets[i].cluster_index = (int32_t)extract_u32(p); p += 4;
        }
        d->has_targets = true;
        break;
    }

    case MR60_TYPE_POINT_CLOUD_DETECT: {
        if (len < 4) break;
        uint32_t n = extract_u32(data);
        if (n > MR60_MAX_TARGETS) n = MR60_MAX_TARGETS;
        const uint8_t *p = data + 4;
        if (len < 4 + n * 16) break;
        d->point_cloud_count = n;
        for (uint32_t i = 0; i < n; i++) {
            d->point_cloud[i].x             = extract_float(p);      p += 4;
            d->point_cloud[i].y             = extract_float(p);      p += 4;
            d->point_cloud[i].dop_index     = (int32_t)extract_u32(p); p += 4;
            d->point_cloud[i].cluster_index = (int32_t)extract_u32(p); p += 4;
        }
        d->has_point_cloud = true;
        break;
    }

    case MR60_TYPE_FIRMWARE:
        if (len >= 4) {
            d->firmware_raw = extract_u32(data);
            d->fw_project  = (d->firmware_raw >> 24) & 0xFF;
            d->fw_major    = (d->firmware_raw >> 16) & 0xFF;
            d->fw_sub      = (d->firmware_raw >> 8) & 0xFF;
            d->fw_modified = d->firmware_raw & 0xFF;
            d->has_firmware = true;
        }
        break;

    /* ── Undocumented types ─────────────────────────────────────────── */

    case MR60_TYPE_POSITION_ALT:
        if (len >= 8) {
            d->alt_x = extract_float(data);
            d->alt_y = extract_float(data + 4);
            d->has_alt_pos = true;
        }
        break;

    case MR60_TYPE_STATUS_CODE:
        if (len >= 2) {
            uint16_t v;
            memcpy(&v, data, sizeof(uint16_t));
            d->status_code = v;
            d->has_status = true;
        }
        break;

    case MR60_TYPE_DEBUG_LOG:
        if (len > 0) {
            size_t cplen = (len < sizeof(d->debug_log) - 1) ? len : sizeof(d->debug_log) - 1;
            memcpy(d->debug_log, data, cplen);
            d->debug_log[cplen] = '\0';
            /* Trim trailing whitespace */
            while (cplen > 0 && (d->debug_log[cplen-1] == ' ' || d->debug_log[cplen-1] == '\n'))
                d->debug_log[--cplen] = '\0';
            d->has_debug_log = true;
        }
        break;

    default: {
        d->frames_unknown++;
        d->last_unknown_type = type;
        /* Throttle: log at most once every 256 unknown frames */
        if ((d->frames_unknown & 0xFF) == 1) {
            char hex[97];
            int hlen = (len > 32) ? 32 : len;
            for (int i = 0; i < hlen; i++)
                snprintf(hex + i*3, 4, "%02X ", data[i]);
            hex[hlen*3] = '\0';
            ESP_LOGI(TAG, "Unknown type 0x%04X len=%u data=[%s]", type, len, hex);
        }
        break;
    }
    }
}

/* ── state machine ──────────────────────────────────────────────────── */

void mr60_parser_init(mr60_parser_t *p, mr60_data_t *out) {
    memset(p, 0, sizeof(*p));
    memset(out, 0, sizeof(*out));
    p->state = MR60_PARSE_SOF;
    p->data  = out;
}

void mr60_parser_feed(mr60_parser_t *p, const uint8_t *in, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t b = in[i];

        switch (p->state) {

        case MR60_PARSE_SOF:
            if (b == MR60_SOF) {
                p->buf[0] = b;
                p->pos = 1;
                p->state = MR60_PARSE_HEADER;
            }
            break;

        case MR60_PARSE_HEADER:
            p->buf[p->pos++] = b;
            if (p->pos == MR60_HEADER_SIZE) {
                /* validate header checksum (bytes 0..6 vs byte 7) */
                uint8_t hcs = calc_checksum(p->buf, 7);
                if (hcs != p->buf[7]) {
                    ESP_LOGD(TAG, "Header checksum fail: got 0x%02X expected 0x%02X", p->buf[7], hcs);
                    p->data->frames_err++;
                    p->state = MR60_PARSE_SOF;
                    break;
                }
                p->data_len = extract_u16_be(p->buf + 3);
                if (p->data_len > MR60_MAX_DATA_LEN) {
                    ESP_LOGW(TAG, "Frame data_len %u exceeds max %d", p->data_len, MR60_MAX_DATA_LEN);
                    p->data->frames_err++;
                    p->state = MR60_PARSE_SOF;
                    break;
                }
                if (p->data_len == 0) {
                    /* no data section, but we still expect a data checksum byte per protocol */
                    p->state = MR60_PARSE_DATA_CKSUM;
                } else {
                    p->state = MR60_PARSE_DATA;
                }
            }
            break;

        case MR60_PARSE_DATA:
            p->buf[p->pos++] = b;
            if (p->pos == MR60_HEADER_SIZE + p->data_len) {
                p->state = MR60_PARSE_DATA_CKSUM;
            }
            break;

        case MR60_PARSE_DATA_CKSUM: {
            /* b is the data checksum byte */
            uint8_t dcs = calc_checksum(p->buf + MR60_HEADER_SIZE, p->data_len);
            if (dcs != b) {
                ESP_LOGD(TAG, "Data checksum fail: got 0x%02X expected 0x%02X", b, dcs);
                p->data->frames_err++;
            } else {
                uint16_t type = extract_u16_be(p->buf + 5);
                process_frame(p->data, type,
                              p->buf + MR60_HEADER_SIZE, p->data_len);
                p->data->frames_ok++;
            }
            p->state = MR60_PARSE_SOF;
            break;
        }
        } /* switch */
    } /* for */
}
