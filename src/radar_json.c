/**
 * @file radar_json.c
 * @brief JSON serialization – full MR60BHA2 sensor output (SI units: metres, m/s, degrees)
 *
 * Output format – all fields present in the sensor data are emitted.
 * Fields that have not yet been received (has_* == false) are omitted rather
 * than emitting stale or zero values.
 *
 * {
 *   "targets": [
 *     { "x": m, "y": m, "speed": m/s, "dist": m, "angle": deg, "cluster_id": int }
 *   ],
 *   "point_cloud": [
 *     { "x": m, "y": m, "speed": m/s, "dist": m, "angle": deg, "cluster_id": int }
 *   ],                                      // omitted when count == 0
 *   "vitals": {
 *     "heart_rate": BPM, "breath_rate": BPM,
 *     "heart_phase": rad, "breath_phase": rad, "total_phase": rad,
 *     "distance": m, "range_flag": uint
 *   },                                      // omitted until first vitals frame
 *   "presence": true | false,
 *   "alt_pos": { "x": m, "y": m },         // omitted when absent (undocumented 0x0A17)
 *   "status_code": uint                     // omitted when absent (undocumented 0x0A29)
 * }
 *
 * radar-dash (https://github.com/duramson/radar-dash) consumes the targets /
 * vitals / presence subset; the additional fields are ignored by that dashboard
 * but available for custom consumers.
 */
#include "radar_json.h"
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

static int js_append(char *buf, size_t remaining, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, remaining, fmt, ap);
    va_end(ap);
    return (n > 0 && (size_t)n < remaining) ? n : 0;
}

static float safe_float(float v) {
    if (isnan(v) || isinf(v)) return 0.0f;
    return v;
}

/**
 * Serialize a single mr60_target_t into the buffer.
 * Returns number of bytes written (0 on overflow).
 */
static int serialize_target(char *p, size_t rem, const mr60_target_t *t) {
    float x    = safe_float(t->x);
    float y    = safe_float(t->y);
    /* speed: dop_index * MR60_RANGE_STEP = cm/s → divide by 100 = m/s */
    float spd  = safe_float(t->dop_index * MR60_RANGE_STEP / 100.0f);
    /* dist: Euclidean distance from x/y position */
    float dist = safe_float(sqrtf(x * x + y * y));
    /* angle: degrees from boresight (forward axis = +Y) */
    float ang  = (y != 0.0f || x != 0.0f)
                 ? safe_float(atan2f(x, y) * (180.0f / (float)M_PI))
                 : 0.0f;
    return js_append(p, rem,
        "{\"x\":%.3f,\"y\":%.3f,\"speed\":%.3f,\"dist\":%.3f,\"angle\":%.1f,\"cluster_id\":%ld}",
        x, y, spd, dist, ang, (long)t->cluster_index);
}

int mr60_data_to_json(const mr60_data_t *d, char *buf, size_t size) {
    char *p = buf;
    size_t rem = size;
    int n;

#define APPEND(...) do { n = js_append(p, rem, __VA_ARGS__); p += n; rem -= n; } while(0)
#define APPEND_TARGET(t) do { n = serialize_target(p, rem, (t)); p += n; rem -= n; } while(0)

    APPEND("{");

    /* ── targets[] – tracked targets from frame type 0x0A04 ─────────── */
    APPEND("\"targets\":[");
    uint32_t count = d->target_count < MR60_MAX_TARGETS ? d->target_count : MR60_MAX_TARGETS;
    for (uint32_t i = 0; i < count; i++) {
        if (i > 0) APPEND(",");
        APPEND_TARGET(&d->targets[i]);
    }
    APPEND("],");

    /* ── point_cloud[] – raw detection cloud from frame type 0x0A08 ────
     * Distinct from tracked targets: these are raw sensor returns before
     * clustering. Omitted when the sensor hasn't produced any. */
    if (d->has_point_cloud && d->point_cloud_count > 0) {
        APPEND("\"point_cloud\":[");
        uint32_t pc = d->point_cloud_count < MR60_MAX_TARGETS
                      ? d->point_cloud_count : MR60_MAX_TARGETS;
        for (uint32_t i = 0; i < pc; i++) {
            if (i > 0) APPEND(",");
            APPEND_TARGET(&d->point_cloud[i]);
        }
        APPEND("],");
    }

    /* ── vitals – frames 0x0A13 / 0x0A14 / 0x0A15 / 0x0A16 ────────────
     * Omit the whole block until the sensor has produced at least one
     * vital-sign or distance frame. */
    if (d->has_phases || d->has_breath_rate || d->has_heart_rate || d->has_distance) {
        APPEND("\"vitals\":{");
        APPEND("\"heart_rate\":%.1f,",   safe_float(d->heart_rate));
        APPEND("\"breath_rate\":%.1f,",  safe_float(d->breath_rate));
        APPEND("\"heart_phase\":%.3f,",  safe_float(d->heart_phase));
        APPEND("\"breath_phase\":%.3f,", safe_float(d->breath_phase));
        APPEND("\"total_phase\":%.3f",   safe_float(d->total_phase));
        if (d->has_distance) {
            APPEND(",\"distance\":%.3f",    safe_float(d->distance));
            APPEND(",\"range_flag\":%lu",   (unsigned long)d->range_flag);
        }
        APPEND("},");
    }

    /* ── presence – frame type 0x0F09 ──────────────────────────────── */
    APPEND("\"presence\":%s", (d->has_human && d->human_detected) ? "true" : "false");

    /* ── alt_pos – undocumented frame 0x0A17 (alternative position) ─── */
    if (d->has_alt_pos) {
        APPEND(",\"alt_pos\":{\"x\":%.3f,\"y\":%.3f}",
               safe_float(d->alt_x), safe_float(d->alt_y));
    }

    /* ── status_code – undocumented frame 0x0A29 ───────────────────── */
    if (d->has_status) {
        APPEND(",\"status_code\":%u", (unsigned)d->status_code);
    }

    APPEND("}");

#undef APPEND
#undef APPEND_TARGET

    return (int)(p - buf);
}
