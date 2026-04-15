/**
 * @file web_server.h
 * @brief HTTP + WebSocket server for radar visualization
 */
#pragma once

#include "mr60bha2.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the web server.
 * @param sensor_data Pointer to the shared sensor data (updated by UART task)
 */
void web_server_start(mr60_data_t *sensor_data);

/**
 * Broadcast current sensor data to all connected WebSocket clients.
 * Call this periodically from a timer or task.
 */
void web_server_broadcast(void);

#ifdef __cplusplus
}
#endif
