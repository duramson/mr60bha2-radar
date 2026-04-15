/**
 * @file main.c
 * @brief MR60BHA2 Radar Telemetry – ESP-IDF main application
 *
 * - Connects to WiFi (hardcoded credentials)
 * - Reads UART from MR60BHA2 sensor (GPIO16 TX, GPIO17 RX)
 * - Serves radar web UI with WebSocket streaming
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "mr60bha2.h"
#include "web_server.h"

static const char *TAG = "main";

/* ── Configuration ──────────────────────────────────────────────────── */
/* WiFi credentials – set via `idf.py menuconfig` or environment:       */
/*   idf.py -DCONFIG_WIFI_SSID="myssid" -DCONFIG_WIFI_PASS="mypass" build */
#define WIFI_SSID      CONFIG_WIFI_SSID
#define WIFI_PASS      CONFIG_WIFI_PASS

#define UART_NUM       UART_NUM_1
#define UART_TX_PIN    GPIO_NUM_16
#define UART_RX_PIN    GPIO_NUM_17
#define UART_BAUD      115200
#define UART_BUF_SIZE  1024

#define WS_BROADCAST_INTERVAL_MS  200  /* 5 Hz WebSocket updates */

/* ── Shared state ───────────────────────────────────────────────────── */
static mr60_data_t   sensor_data;
static mr60_parser_t parser;

/* ── WiFi ───────────────────────────────────────────────────────────── */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *event_data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "╔══════════════════════════════════════╗");
        ESP_LOGI(TAG, "║  Radar UI: http://" IPSTR "  ║", IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "╚══════════════════════════════════════╝");
    }
}

static void wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi connecting to [%s]...", WIFI_SSID);
}

/* ── UART ───────────────────────────────────────────────────────────── */
static void uart_init(void) {
    uart_config_t uart_cfg = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "UART%d init: TX=%d RX=%d baud=%d", UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_BAUD);
}

/* ── UART read task ─────────────────────────────────────────────────── */
static void uart_task(void *arg) {
    uint8_t buf[256];
    while (1) {
        int len = uart_read_bytes(UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(20));
        if (len > 0) {
            mr60_parser_feed(&parser, buf, len);
        }
    }
}

/* ── Periodic WebSocket broadcast task ──────────────────────────────── */
static void ws_broadcast_task(void *arg) {
    while (1) {
        web_server_broadcast();
        vTaskDelay(pdMS_TO_TICKS(WS_BROADCAST_INTERVAL_MS));
    }
}

/* ── Serial logging task (debug) ────────────────────────────────────── */
static void log_task(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        ESP_LOGI(TAG, "frames=%u err=%u human=%d dist=%.2f HR=%.0f BR=%.1f targets=%u",
                 sensor_data.frames_ok, sensor_data.frames_err,
                 sensor_data.human_detected, sensor_data.distance,
                 sensor_data.heart_rate, sensor_data.breath_rate,
                 sensor_data.target_count);
        for (uint32_t i = 0; i < sensor_data.target_count; i++) {
            ESP_LOGI(TAG, "  T%d: x=%.2f y=%.2f dop=%d speed=%.1fcm/s cid=%d",
                     i, sensor_data.targets[i].x, sensor_data.targets[i].y,
                     (int)sensor_data.targets[i].dop_index,
                     sensor_data.targets[i].dop_index * MR60_RANGE_STEP,
                     (int)sensor_data.targets[i].cluster_index);
        }
    }
}

/* ── Entry ──────────────────────────────────────────────────────────── */
void app_main(void) {
    ESP_LOGI(TAG, "MR60BHA2 Radar Telemetry starting...");

    /* NVS (required for WiFi) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Init parser */
    mr60_parser_init(&parser, &sensor_data);

    /* Init peripherals */
    uart_init();
    wifi_init();

    /* Wait a beat for WiFi before starting server */
    vTaskDelay(pdMS_TO_TICKS(2000));

    /* Start web server */
    web_server_start(&sensor_data);

    /* Start tasks */
    xTaskCreate(uart_task, "uart_rx", 4096, NULL, 10, NULL);
    xTaskCreate(ws_broadcast_task, "ws_bcast", 4096, NULL, 5, NULL);
    xTaskCreate(log_task, "log", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "All tasks started.");
}
