/**
 * @file radar_json.h
 * @brief JSON serialization of MR60BHA2 sensor data for WebSocket streaming
 */
#pragma once

#include "mr60bha2.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Serialize current sensor data to a JSON string.
 * @param d    Sensor data
 * @param buf  Output buffer
 * @param size Buffer size
 * @return Number of bytes written (excluding null terminator), or -1 on error
 */
int mr60_data_to_json(const mr60_data_t *d, char *buf, size_t size);

#ifdef __cplusplus
}
#endif
