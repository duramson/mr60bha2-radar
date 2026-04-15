/**
 * @file web_server.c
 * @brief Async HTTP server serving the embedded radar-dash dashboard and a
 *        WebSocket endpoint for real-time radar data.
 */
#include "web_server.h"
#include "radar_json.h"
#include "radar_page.h"   /* generated from radar-dash/index.html: radar_page_html[], radar_page_html_len */
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include <string.h>

static const char *TAG = "web";

static httpd_handle_t s_server = NULL;
static mr60_data_t *s_data = NULL;

/* ── WebSocket client tracking ──────────────────────────────────────── */
#define MAX_WS_CLIENTS 4

static int s_ws_fds[MAX_WS_CLIENTS];
static int s_ws_count = 0;

static void ws_add_client(int fd) {
    if (s_ws_count < MAX_WS_CLIENTS) {
        s_ws_fds[s_ws_count++] = fd;
        ESP_LOGI(TAG, "WS client connected (fd=%d, total=%d)", fd, s_ws_count);
    }
}

static void ws_remove_client(int fd) {
    for (int i = 0; i < s_ws_count; i++) {
        if (s_ws_fds[i] == fd) {
            s_ws_fds[i] = s_ws_fds[--s_ws_count];
            ESP_LOGI(TAG, "WS client disconnected (fd=%d, total=%d)", fd, s_ws_count);
            return;
        }
    }
}

/* ── HTTP handler: serve the embedded radar-dash dashboard ──────────── */

static esp_err_t root_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Content-Encoding", "identity");
    return httpd_resp_send(req, (const char *)radar_page_html, radar_page_html_len);
}

/* ── WebSocket handler ──────────────────────────────────────────────── */

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        /* Handshake – client just connected */
        ws_add_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    /* Receive frame (we don't expect data from client, but handle close) */
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ws_remove_client(httpd_req_to_sockfd(req));
        return ret;
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
        ws_remove_client(httpd_req_to_sockfd(req));
    }

    return ESP_OK;
}

/* ── OTA firmware upload handler ─────────────────────────────────────── */

static esp_err_t ota_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "OTA update started, content_length=%d", req->content_len);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        ESP_LOGE(TAG, "OTA: no update partition found");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "OTA: writing to partition '%s' at 0x%lx",
             update_partition->label, (unsigned long)update_partition->address);

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char buf[1024];
    int received = 0, total = req->content_len;
    int remaining = total;

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, sizeof(buf) < remaining ? sizeof(buf) : remaining);
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "OTA: recv error");
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive error");
            return ESP_FAIL;
        }

        err = esp_ota_write(ota_handle, buf, recv_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        received += recv_len;
        remaining -= recv_len;
        if ((received % (100 * 1024)) < 1024) {
            ESP_LOGI(TAG, "OTA: %d / %d bytes (%.0f%%)", received, total, 100.0f * received / total);
        }
    }

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA validation failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA update successful (%d bytes). Rebooting...", received);
    httpd_resp_sendstr(req, "OK: OTA update successful, rebooting...\n");

    /* Delay reboot so HTTP response can flush */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK; /* unreachable */
}

/* ── Server start ───────────────────────────────────────────────────── */

void web_server_start(mr60_data_t *sensor_data) {
    s_data = sensor_data;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.close_fn = NULL;
    config.recv_wait_timeout = 30;  /* OTA uploads can be slow */

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    /* Root page */
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
    };
    httpd_register_uri_handler(s_server, &root_uri);

    /* WebSocket endpoint */
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };
    httpd_register_uri_handler(s_server, &ws_uri);

    /* OTA firmware upload endpoint */
    httpd_uri_t ota_uri = {
        .uri = "/ota",
        .method = HTTP_POST,
        .handler = ota_handler,
    };
    httpd_register_uri_handler(s_server, &ota_uri);

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
}

/* ── Broadcast to all WS clients ────────────────────────────────────── */

void web_server_broadcast(void) {
    if (!s_server || !s_data || s_ws_count == 0) return;

    static char json_buf[2048];
    int len = mr60_data_to_json(s_data, json_buf, sizeof(json_buf));
    if (len <= 0) return;

    httpd_ws_frame_t pkt = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json_buf,
        .len = (size_t)len,
        .final = true,
    };

    for (int i = s_ws_count - 1; i >= 0; i--) {
        esp_err_t ret = httpd_ws_send_frame_async(s_server, s_ws_fds[i], &pkt);
        if (ret != ESP_OK) {
            ESP_LOGD(TAG, "WS send failed (fd=%d), removing", s_ws_fds[i]);
            ws_remove_client(s_ws_fds[i]);
        }
    }
}
