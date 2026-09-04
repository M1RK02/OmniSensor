/*
 * HLK-LD2420 24 GHz mmWave Presence Radar Driver
 * OmniSensor Project
 *
 * Communicates over UART0 (GPIO16 TX -> Radar RX, GPIO17 RX <- Radar TX) at 115200 baud.
 * Decodes both Energy Mode binary telemetry (45-byte frames) and Simple Mode ASCII stream.
 */

#include "ld2420.h"
#include "app_priv.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ld2420";

#define LD2420_UART_BAUD        115200
#define LD2420_UART_RX_BUF_SIZE 1024
#define LD2420_STREAM_BUF_SIZE  256

// Data frame header/footer for Energy Mode (45-byte binary frame)
#define DATA_HEADER_BYTE0       0xF4
#define DATA_HEADER_BYTE1       0xF3
#define DATA_HEADER_BYTE2       0xF2
#define DATA_HEADER_BYTE3       0xF1

#define DATA_FOOTER_BYTE0       0xF8
#define DATA_FOOTER_BYTE1       0xF7
#define DATA_FOOTER_BYTE2       0xF6
#define DATA_FOOTER_BYTE3       0xF5

#define TOTAL_GATES             16

static uint8_t  s_stream_buf[LD2420_STREAM_BUF_SIZE];
static uint16_t s_stream_len = 0;

static omni_zone_t calculate_zone(bool occupied, uint16_t distance_cm)
{
    if (!occupied) {
        return OMNI_ZONE_CLEAR;
    }
    if (distance_cm > 0 && distance_cm < 70) {
        return OMNI_ZONE_1;
    }
    if (distance_cm >= 70 && distance_cm <= 140) {
        return OMNI_ZONE_2;
    }
    return OMNI_ZONE_3;
}

esp_err_t ld2420_init(void)
{
    uart_config_t uart_config = {
        .baud_rate  = LD2420_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_is_driver_installed(OMNI_RADAR_UART_PORT)) {
        uart_driver_delete(OMNI_RADAR_UART_PORT);
    }

    esp_err_t ret = uart_driver_install(OMNI_RADAR_UART_PORT, LD2420_UART_RX_BUF_SIZE, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_param_config(OMNI_RADAR_UART_PORT, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(OMNI_RADAR_UART_PORT, OMNI_PIN_RADAR_TX, OMNI_PIN_RADAR_RX,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Enable internal pull-ups on TX/RX lines so idle lines remain high */
    gpio_set_pull_mode(OMNI_PIN_RADAR_TX, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(OMNI_PIN_RADAR_RX, GPIO_PULLUP_ONLY);

    /* Exempt pins from light-sleep GPIO isolation */
    gpio_sleep_sel_dis(OMNI_PIN_RADAR_TX);
    gpio_sleep_sel_dis(OMNI_PIN_RADAR_RX);

    uart_flush_input(OMNI_RADAR_UART_PORT);
    s_stream_len = 0;

    ESP_LOGI(TAG, "LD2420 radar UART initialized on TX=%d, RX=%d at %d baud",
             OMNI_PIN_RADAR_TX, OMNI_PIN_RADAR_RX, LD2420_UART_BAUD);
    return ESP_OK;
}

bool ld2420_poll(ld2420_data_t *data, uint32_t timeout_ms)
{
    if (data == NULL) {
        return false;
    }

    uint8_t rx_tmp[64];
    int n = uart_read_bytes(OMNI_RADAR_UART_PORT, rx_tmp, sizeof(rx_tmp), pdMS_TO_TICKS(timeout_ms));
    if (n > 0) {
        if (s_stream_len + n > sizeof(s_stream_buf)) {
            uint16_t keep = 64;
            if (s_stream_len > keep) {
                memmove(s_stream_buf, &s_stream_buf[s_stream_len - keep], keep);
                s_stream_len = keep;
            }
        }
        if (s_stream_len + n <= sizeof(s_stream_buf)) {
            memcpy(&s_stream_buf[s_stream_len], rx_tmp, n);
            s_stream_len += n;
        }
    }

    if (s_stream_len == 0) {
        return false;
    }

    /* 1. Parser: Energy Mode Binary Frames (45 bytes) */
    for (uint16_t i = 0; i + 45 <= s_stream_len; i++) {
        if (s_stream_buf[i]     == DATA_HEADER_BYTE0 &&
            s_stream_buf[i + 1] == DATA_HEADER_BYTE1 &&
            s_stream_buf[i + 2] == DATA_HEADER_BYTE2 &&
            s_stream_buf[i + 3] == DATA_HEADER_BYTE3) {

            if (s_stream_buf[i + 41] == DATA_FOOTER_BYTE0 &&
                s_stream_buf[i + 42] == DATA_FOOTER_BYTE1 &&
                s_stream_buf[i + 43] == DATA_FOOTER_BYTE2 &&
                s_stream_buf[i + 44] == DATA_FOOTER_BYTE3) {

                data->occupied = (s_stream_buf[i + 6] != 0);
                data->distance_cm = s_stream_buf[i + 7] | (s_stream_buf[i + 8] << 8);
                data->zone = calculate_zone(data->occupied, data->distance_cm);

                data->peak_gate = 0;
                data->peak_energy = 0;
                for (uint8_t g = 0; g < TOTAL_GATES; g++) {
                    uint16_t e = s_stream_buf[i + 9 + g * 2] | (s_stream_buf[i + 10 + g * 2] << 8);
                    if (e > data->peak_energy) {
                        data->peak_energy = e;
                        data->peak_gate = g;
                    }
                }

                uint16_t consumed = i + 45;
                memmove(s_stream_buf, &s_stream_buf[consumed], s_stream_len - consumed);
                s_stream_len -= consumed;
                return true;
            }
        }
    }

    /* 2. Parser: Simple Mode ASCII Lines ("ON\r\n", "OFF\r\n", "Range %d\r\n") */
    for (uint16_t i = 0; i < s_stream_len; i++) {
        if (s_stream_buf[i] == '\n' || s_stream_buf[i] == '\r') {
            s_stream_buf[i] = '\0';
            char *line = (char *)s_stream_buf;
            bool updated = false;

            if (strlen(line) > 0) {
                if (strstr(line, "ON") != NULL) {
                    data->occupied = true;
                    data->zone = calculate_zone(true, data->distance_cm);
                    updated = true;
                } else if (strstr(line, "OFF") != NULL) {
                    data->occupied = false;
                    data->zone = OMNI_ZONE_CLEAR;
                    data->distance_cm = 0;
                    updated = true;
                }

                int dist = -1;
                if (sscanf(line, "Range %d", &dist) == 1 || sscanf(line, "%d", &dist) == 1) {
                    if (dist >= 0) {
                        data->distance_cm = (uint16_t)dist;
                        data->zone = calculate_zone(data->occupied, data->distance_cm);
                        updated = true;
                    }
                }
            }

            uint16_t consumed = i + 1;
            memmove(s_stream_buf, &s_stream_buf[consumed], s_stream_len - consumed);
            s_stream_len -= consumed;

            if (updated) {
                return true;
            }
            break;
        }
    }

    return false;
}
