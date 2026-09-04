/*
 * HLK-LD2420 24 GHz mmWave Radar Test Application
 * OmniSensor Project
 *
 * Implements:
 * - Automatic hardware power-cycling via SI2301 load switch (RAIL_EN, GPIO 21 / D3)
 * - LD2420 UART communication on UART_NUM_0 (GPIO 16 TX -> Radar RX, GPIO 17 RX <- Radar TX)
 * - Configuration handshake (Read Version, Range, Gate Thresholds, Mode)
 * - Real-time stream decoding for both Energy Mode (binary) and Simple Mode (text)
 * - Zone Classification:
 *     - Zone 1: < 0.7 m (At device)
 *     - Zone 2: 0.7 - 1.4 m (Near field)
 *     - Zone 3: > 1.4 m (Room presence)
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"

static const char *TAG = "LD2420";

// =============================================================================
// Hardware Pins & UART Configuration
// =============================================================================
#define LD2420_UART_PORT        UART_NUM_0
#define LD2420_TX_PIN           16                  // D6 -> Radar RX
#define LD2420_RX_PIN           17                  // D7 <- Radar OT1/TX
#define LD2420_RAIL_EN_PIN      GPIO_NUM_21         // D3 -> SI2301 P-MOSFET gate (Active LOW)
#define LD2420_BUF_SIZE         1024

// =============================================================================
// Protocol Framing & Commands (HLK-LD2420 Protocol Document)
// =============================================================================
#define CMD_HEADER_BYTE0        0xFD
#define CMD_HEADER_BYTE1        0xFC
#define CMD_HEADER_BYTE2        0xFB
#define CMD_HEADER_BYTE3        0xFA

#define CMD_FOOTER_BYTE0        0x04
#define CMD_FOOTER_BYTE1        0x03
#define CMD_FOOTER_BYTE2        0x02
#define CMD_FOOTER_BYTE3        0x01

// Data frame header/footer for Energy Mode
#define DATA_HEADER_BYTE0       0xF4
#define DATA_HEADER_BYTE1       0xF3
#define DATA_HEADER_BYTE2       0xF2
#define DATA_HEADER_BYTE3       0xF1

#define DATA_FOOTER_BYTE0       0xF8
#define DATA_FOOTER_BYTE1       0xF7
#define DATA_FOOTER_BYTE2       0xF6
#define DATA_FOOTER_BYTE3       0xF5

#define CMD_READ_VERSION        0x0000
#define CMD_WRITE_PARAM         0x0007
#define CMD_READ_PARAM          0x0008
#define CMD_WRITE_SYS_PARAM     0x0012
#define CMD_RESTART             0x0068
#define CMD_CLOSE_CONFIG        0x00FE
#define CMD_OPEN_CONFIG         0x00FF

#define REG_MIN_GATE            0x0000
#define REG_MAX_GATE            0x0001
#define REG_DELAY_TIME          0x0004
#define REG_TRIGGER_GATE0       0x0010
#define REG_HOLD_GATE0          0x0020

#define SYS_MODE_ENERGY         0x0004
#define SYS_MODE_SIMPLE         0x0064

#define TOTAL_GATES             16
#define GATE_DISTANCE_M         0.7f

// =============================================================================
// Data Types
// =============================================================================
typedef enum {
    RADAR_ZONE_CLEAR = 0,
    RADAR_ZONE_1     = 1,   // < 0.7 m  (At device)
    RADAR_ZONE_2     = 2,   // 0.7 - 1.4 m (Near field)
    RADAR_ZONE_3     = 3,   // > 1.4 m  (Room presence)
} radar_zone_t;

typedef struct {
    bool         occupied;
    uint16_t     distance_cm;
    radar_zone_t zone;
    uint16_t     gate_energies[TOTAL_GATES];
    uint8_t      peak_gate;
    uint16_t     peak_energy;
} radar_data_t;

// =============================================================================
// Hardware Control & UART Functions
// =============================================================================
static void power_cycle_radar(void) {
    gpio_config_t pwr_cfg = {
        .pin_bit_mask = (1ULL << LD2420_RAIL_EN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&pwr_cfg);

    // Active LOW load switch: 1 = Power OFF, 0 = Power ON
    ESP_LOGI(TAG, "Power-cycling radar via RAIL_EN (GPIO %d)...", LD2420_RAIL_EN_PIN);
    gpio_set_level(LD2420_RAIL_EN_PIN, 1);  // Cut power to radar rail
    vTaskDelay(pdMS_TO_TICKS(500));         // Allow decoupling caps to discharge
    gpio_set_level(LD2420_RAIL_EN_PIN, 0);  // Re-enable power to radar rail (Active LOW)
    ESP_LOGI(TAG, "Radar rail re-energised. Waiting 2.5s for radar MCU & RF to stabilize...");
    vTaskDelay(pdMS_TO_TICKS(2500));
}

static esp_err_t uart_init_radar(uint32_t baud_rate) {
    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    if (uart_is_driver_installed(LD2420_UART_PORT)) {
        uart_driver_delete(LD2420_UART_PORT);
    }

    esp_err_t ret = uart_driver_install(LD2420_UART_PORT, LD2420_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) return ret;

    ret = uart_param_config(LD2420_UART_PORT, &uart_config);
    if (ret != ESP_OK) return ret;

    ret = uart_set_pin(LD2420_UART_PORT, LD2420_TX_PIN, LD2420_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) return ret;

    // Enable internal pull-ups on TX/RX lines
    gpio_set_pull_mode(LD2420_TX_PIN, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(LD2420_RX_PIN, GPIO_PULLUP_ONLY);

    uart_flush_input(LD2420_UART_PORT);
    return ESP_OK;
}

// Send command frame and receive response frame
static esp_err_t ld2420_send_command(uint16_t cmd, const uint8_t *payload, uint16_t payload_len,
                                     uint8_t *resp_data, uint16_t resp_max_len, uint16_t *resp_data_len,
                                     uint32_t timeout_ms) {
    uint8_t tx_buf[128];
    uint16_t intra_len = 2 + payload_len;
    uint16_t tx_idx = 0;

    tx_buf[tx_idx++] = CMD_HEADER_BYTE0;
    tx_buf[tx_idx++] = CMD_HEADER_BYTE1;
    tx_buf[tx_idx++] = CMD_HEADER_BYTE2;
    tx_buf[tx_idx++] = CMD_HEADER_BYTE3;

    tx_buf[tx_idx++] = (uint8_t)(intra_len & 0xFF);
    tx_buf[tx_idx++] = (uint8_t)((intra_len >> 8) & 0xFF);

    tx_buf[tx_idx++] = (uint8_t)(cmd & 0xFF);
    tx_buf[tx_idx++] = (uint8_t)((cmd >> 8) & 0xFF);

    if (payload && payload_len > 0) {
        memcpy(&tx_buf[tx_idx], payload, payload_len);
        tx_idx += payload_len;
    }

    tx_buf[tx_idx++] = CMD_FOOTER_BYTE0;
    tx_buf[tx_idx++] = CMD_FOOTER_BYTE1;
    tx_buf[tx_idx++] = CMD_FOOTER_BYTE2;
    tx_buf[tx_idx++] = CMD_FOOTER_BYTE3;

    uart_flush_input(LD2420_UART_PORT);
    uart_write_bytes(LD2420_UART_PORT, (const char *)tx_buf, tx_idx);

    if (cmd == CMD_RESTART) {
        vTaskDelay(pdMS_TO_TICKS(100));
        return ESP_OK;
    }

    int64_t start_time = esp_timer_get_time();
    int64_t timeout_us = (int64_t)timeout_ms * 1000;
    uint8_t rx_buf[256];
    uint16_t rx_idx = 0;

    while ((esp_timer_get_time() - start_time) < timeout_us) {
        uint8_t b;
        int n = uart_read_bytes(LD2420_UART_PORT, &b, 1, pdMS_TO_TICKS(10));
        if (n <= 0) continue;

        rx_buf[rx_idx++] = b;
        if (rx_idx >= sizeof(rx_buf)) {
            rx_idx = 0;
            continue;
        }

        if (rx_idx >= 4 &&
            rx_buf[rx_idx - 4] == CMD_HEADER_BYTE0 &&
            rx_buf[rx_idx - 3] == CMD_HEADER_BYTE1 &&
            rx_buf[rx_idx - 2] == CMD_HEADER_BYTE2 &&
            rx_buf[rx_idx - 1] == CMD_HEADER_BYTE3) {
            rx_buf[0] = CMD_HEADER_BYTE0;
            rx_buf[1] = CMD_HEADER_BYTE1;
            rx_buf[2] = CMD_HEADER_BYTE2;
            rx_buf[3] = CMD_HEADER_BYTE3;
            rx_idx = 4;
            continue;
        }

        if (rx_idx >= 6 && rx_buf[0] == CMD_HEADER_BYTE0) {
            uint16_t frame_len = rx_buf[4] | (rx_buf[5] << 8);
            uint16_t total_expected = 4 + 2 + frame_len + 4;

            if (total_expected > sizeof(rx_buf)) {
                rx_idx = 0;
                continue;
            }

            if (rx_idx >= total_expected) {
                uint16_t f_idx = 6 + frame_len;
                if (rx_buf[f_idx] == CMD_FOOTER_BYTE0 &&
                    rx_buf[f_idx + 1] == CMD_FOOTER_BYTE1 &&
                    rx_buf[f_idx + 2] == CMD_FOOTER_BYTE2 &&
                    rx_buf[f_idx + 3] == CMD_FOOTER_BYTE3) {
                    
                    if (frame_len < 4) return ESP_FAIL;
                    uint16_t status = rx_buf[8] | (rx_buf[9] << 8);
                    if (status != 0) {
                        ESP_LOGW(TAG, "Command 0x%04X returned status code 0x%04X", cmd, status);
                        return ESP_FAIL;
                    }

                    uint16_t payload_bytes = frame_len - 4;
                    if (resp_data && resp_data_len) {
                        uint16_t to_copy = payload_bytes < resp_max_len ? payload_bytes : resp_max_len;
                        memcpy(resp_data, &rx_buf[10], to_copy);
                        *resp_data_len = to_copy;
                    }
                    return ESP_OK;
                }
            }
        }
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t ld2420_open_config_mode(int max_retries) {
    for (int r = 1; r <= max_retries; r++) {
        uint8_t payload[2] = { 0x01, 0x00 };
        uint8_t resp[16];
        uint16_t resp_len = 0;
        uart_flush_input(LD2420_UART_PORT);
        esp_err_t err = ld2420_send_command(CMD_OPEN_CONFIG, payload, sizeof(payload), resp, sizeof(resp), &resp_len, 400);
        if (err == ESP_OK) return ESP_OK;

        payload[0] = 0x02;
        uart_flush_input(LD2420_UART_PORT);
        err = ld2420_send_command(CMD_OPEN_CONFIG, payload, sizeof(payload), resp, sizeof(resp), &resp_len, 400);
        if (err == ESP_OK) return ESP_OK;

        vTaskDelay(pdMS_TO_TICKS(150));
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t ld2420_close_config_mode(void) {
    return ld2420_send_command(CMD_CLOSE_CONFIG, NULL, 0, NULL, 0, NULL, 500);
}

static esp_err_t ld2420_read_version(char *ver_str, size_t max_len) {
    uint8_t resp[32];
    uint16_t resp_len = 0;
    esp_err_t err = ld2420_send_command(CMD_READ_VERSION, NULL, 0, resp, sizeof(resp), &resp_len, 500);
    if (err == ESP_OK && resp_len >= 2) {
        uint16_t str_len = resp[0] | (resp[1] << 8);
        if (str_len > (resp_len - 2)) str_len = resp_len - 2;
        if (str_len >= max_len) str_len = max_len - 1;
        memcpy(ver_str, &resp[2], str_len);
        ver_str[str_len] = '\0';
        return ESP_OK;
    }
    snprintf(ver_str, max_len, "unknown");
    return err;
}

static esp_err_t ld2420_read_params(uint32_t *min_gate, uint32_t *max_gate, uint32_t *delay_sec) {
    uint8_t payload[6] = {
        (uint8_t)(REG_MIN_GATE & 0xFF), (uint8_t)(REG_MIN_GATE >> 8),
        (uint8_t)(REG_MAX_GATE & 0xFF), (uint8_t)(REG_MAX_GATE >> 8),
        (uint8_t)(REG_DELAY_TIME & 0xFF), (uint8_t)(REG_DELAY_TIME >> 8),
    };
    uint8_t resp[24];
    uint16_t resp_len = 0;
    esp_err_t err = ld2420_send_command(CMD_READ_PARAM, payload, sizeof(payload), resp, sizeof(resp), &resp_len, 500);
    if (err == ESP_OK && resp_len >= 12) {
        if (min_gate)  *min_gate  = resp[0] | (resp[1] << 8) | (resp[2] << 16) | (resp[3] << 24);
        if (max_gate)  *max_gate  = resp[4] | (resp[5] << 8) | (resp[6] << 16) | (resp[7] << 24);
        if (delay_sec) *delay_sec = resp[8] | (resp[9] << 8) | (resp[10] << 16) | (resp[11] << 24);
        return ESP_OK;
    }
    return err;
}

static esp_err_t ld2420_read_gate_thresholds(uint32_t *trigger_thresh, uint32_t *hold_thresh) {
    for (uint8_t g = 0; g < TOTAL_GATES; g++) {
        uint16_t trig_reg = REG_TRIGGER_GATE0 + g;
        uint16_t hold_reg = REG_HOLD_GATE0 + g;
        uint8_t payload[4] = {
            (uint8_t)(trig_reg & 0xFF), (uint8_t)(trig_reg >> 8),
            (uint8_t)(hold_reg & 0xFF), (uint8_t)(hold_reg >> 8),
        };
        uint8_t resp[16];
        uint16_t resp_len = 0;
        esp_err_t err = ld2420_send_command(CMD_READ_PARAM, payload, sizeof(payload), resp, sizeof(resp), &resp_len, 300);
        if (err == ESP_OK && resp_len >= 8) {
            trigger_thresh[g] = resp[0] | (resp[1] << 8) | (resp[2] << 16) | (resp[3] << 24);
            hold_thresh[g]    = resp[4] | (resp[5] << 8) | (resp[6] << 16) | (resp[7] << 24);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

static esp_err_t ld2420_set_system_mode(uint16_t mode) {
    uint8_t payload[6] = {
        0x00, 0x00,
        (uint8_t)(mode & 0xFF), (uint8_t)(mode >> 8),
        0x00, 0x00
    };
    uint8_t resp[8];
    uint16_t resp_len = 0;
    return ld2420_send_command(CMD_WRITE_SYS_PARAM, payload, sizeof(payload), resp, sizeof(resp), &resp_len, 500);
}

static radar_zone_t calculate_zone(bool occupied, uint16_t distance_cm) {
    if (!occupied) return RADAR_ZONE_CLEAR;
    if (distance_cm < 70) return RADAR_ZONE_1;
    if (distance_cm <= 140) return RADAR_ZONE_2;
    return RADAR_ZONE_3;
}

static const char *zone_name(radar_zone_t z) {
    switch (z) {
        case RADAR_ZONE_CLEAR: return "CLEAR";
        case RADAR_ZONE_1:     return "ZONE 1 (< 0.7m, At Device)";
        case RADAR_ZONE_2:     return "ZONE 2 (0.7m - 1.4m, Near Field)";
        case RADAR_ZONE_3:     return "ZONE 3 (> 1.4m, Room Presence)";
        default:               return "UNKNOWN";
    }
}

// =============================================================================
// Application Entry Point
// =============================================================================
void app_main(void) {
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "       HLK-LD2420 mmWave Radar Test Suite        ");
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "UART_NUM_0: TX=GPIO%d (radar RX), RX=GPIO%d (radar TX)", LD2420_TX_PIN, LD2420_RX_PIN);

    // 1. Initialize UART with pull-ups to guarantee idle line stays HIGH
    uart_init_radar(115200);

    // 2. Hardware Power Cycle: Pulse RAIL_EN (GPIO 21) so the radar gets a clean cold boot
    power_cycle_radar();

    // 3. Probing configuration mode (115200 first, then 256000)
    uint32_t active_baud = 115200;
    ESP_LOGI(TAG, "Attempting configuration mode handshake at %" PRIu32 " baud...", active_baud);
    esp_err_t err = ld2420_open_config_mode(5);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No response at %" PRIu32 " baud, trying legacy 256000 baud...", active_baud);
        active_baud = 256000;
        uart_init_radar(active_baud);
        vTaskDelay(pdMS_TO_TICKS(200));
        err = ld2420_open_config_mode(5);
    }

    if (err == ESP_OK) {
        ESP_LOGI(TAG, ">>> Successfully entered Configuration Mode at %" PRIu32 " baud! <<<", active_baud);

        char fw_version[32] = { 0 };
        if (ld2420_read_version(fw_version, sizeof(fw_version)) == ESP_OK) {
            ESP_LOGI(TAG, "[INFO] Radar Firmware Version: %s", fw_version);
        }

        uint32_t min_gate = 0, max_gate = 0, delay_sec = 0;
        if (ld2420_read_params(&min_gate, &max_gate, &delay_sec) == ESP_OK) {
            ESP_LOGI(TAG, "[CONFIG] Detection Range: Gate %" PRIu32 " (%.2f m) to Gate %" PRIu32 " (%.2f m)",
                     min_gate, min_gate * GATE_DISTANCE_M,
                     max_gate, max_gate * GATE_DISTANCE_M);
            ESP_LOGI(TAG, "[CONFIG] Absence Delay Time: %" PRIu32 " seconds", delay_sec);
        }

        uint32_t trig_thresh[TOTAL_GATES] = { 0 };
        uint32_t hold_thresh[TOTAL_GATES] = { 0 };
        if (ld2420_read_gate_thresholds(trig_thresh, hold_thresh) == ESP_OK) {
            ESP_LOGI(TAG, "--- Per-Gate Sensitivity Thresholds ---");
            for (uint8_t g = 0; g < TOTAL_GATES; g += 4) {
                ESP_LOGI(TAG, "Gates %02d-%02d | Trig: %5" PRIu32 ", %5" PRIu32 ", %5" PRIu32 ", %5" PRIu32
                               " | Hold: %5" PRIu32 ", %5" PRIu32 ", %5" PRIu32 ", %5" PRIu32,
                         g, g + 3,
                         trig_thresh[g], trig_thresh[g+1], trig_thresh[g+2], trig_thresh[g+3],
                         hold_thresh[g], hold_thresh[g+1], hold_thresh[g+2], hold_thresh[g+3]);
            }
        }

        // Set Energy Mode for gate-level telemetry
        ld2420_set_system_mode(SYS_MODE_ENERGY);
        ld2420_close_config_mode();
        vTaskDelay(pdMS_TO_TICKS(100));
    } else {
        ESP_LOGW(TAG, "Config mode handshake not acknowledged.");
        ESP_LOGI(TAG, "Listening directly to autonomous radar stream at 115200 baud...");
        active_baud = 115200;
        uart_init_radar(active_baud);
    }

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "Radar active! Move or walk in front of the sensor.");
    ESP_LOGI(TAG, "==================================================");

    // =========================================================================
    // Real-Time Radar Stream Processor
    // =========================================================================
    uint8_t stream_buf[512];
    uint16_t stream_len = 0;

    radar_data_t current_data = { 0 };
    bool last_occupied = false;
    radar_zone_t last_zone = RADAR_ZONE_CLEAR;
    int64_t last_log_time = 0;
    int64_t last_energy_table_time = 0;
    int64_t last_rx_byte_time = 0;
    uint32_t total_bytes_rx = 0;

    while (1) {
        uint8_t b;
        int n = uart_read_bytes(LD2420_UART_PORT, &b, 1, pdMS_TO_TICKS(50));
        if (n > 0) {
            total_bytes_rx++;
            last_rx_byte_time = esp_timer_get_time();
            stream_buf[stream_len++] = b;

            if (stream_len >= sizeof(stream_buf)) {
                memmove(stream_buf, &stream_buf[stream_len - 64], 64);
                stream_len = 64;
            }

            // -------------------------------------------------------------
            // Parser A: Energy Mode Binary Frames (45 bytes)
            // -------------------------------------------------------------
            for (uint16_t i = 0; i + 45 <= stream_len; i++) {
                if (stream_buf[i]   == DATA_HEADER_BYTE0 &&
                    stream_buf[i+1] == DATA_HEADER_BYTE1 &&
                    stream_buf[i+2] == DATA_HEADER_BYTE2 &&
                    stream_buf[i+3] == DATA_HEADER_BYTE3) {

                    if (stream_buf[i+41] == DATA_FOOTER_BYTE0 &&
                        stream_buf[i+42] == DATA_FOOTER_BYTE1 &&
                        stream_buf[i+43] == DATA_FOOTER_BYTE2 &&
                        stream_buf[i+44] == DATA_FOOTER_BYTE3) {

                        current_data.occupied = (stream_buf[i+6] != 0);
                        current_data.distance_cm = stream_buf[i+7] | (stream_buf[i+8] << 8);
                        current_data.zone = calculate_zone(current_data.occupied, current_data.distance_cm);

                        current_data.peak_gate = 0;
                        current_data.peak_energy = 0;
                        for (uint8_t g = 0; g < TOTAL_GATES; g++) {
                            uint16_t e = stream_buf[i + 9 + g * 2] | (stream_buf[i + 10 + g * 2] << 8);
                            current_data.gate_energies[g] = e;
                            if (e > current_data.peak_energy) {
                                current_data.peak_energy = e;
                                current_data.peak_gate = g;
                            }
                        }

                        int64_t now = esp_timer_get_time();
                        bool state_changed = (current_data.occupied != last_occupied) || (current_data.zone != last_zone);
                        bool heartbeat_due = (now - last_log_time) > (1000 * 1000);

                        if (state_changed || heartbeat_due) {
                            last_log_time = now;
                            last_occupied = current_data.occupied;
                            last_zone = current_data.zone;

                            if (current_data.occupied) {
                                ESP_LOGI(TAG, ">>> OCCUPIED | Dist: %4d cm (%4.2f m) | %s | Peak Gate: %2d (energy %5d)",
                                         current_data.distance_cm,
                                         (float)current_data.distance_cm / 100.0f,
                                         zone_name(current_data.zone),
                                         current_data.peak_gate,
                                         current_data.peak_energy);
                            } else {
                                ESP_LOGI(TAG, "    CLEAR    | No target detected | %s", zone_name(current_data.zone));
                            }
                        }

                        if (current_data.occupied && (now - last_energy_table_time) > (3000 * 1000)) {
                            last_energy_table_time = now;
                            ESP_LOGI(TAG, "    [Gates 00-07 Energies]: %4d %4d %4d %4d %4d %4d %4d %4d",
                                     current_data.gate_energies[0], current_data.gate_energies[1],
                                     current_data.gate_energies[2], current_data.gate_energies[3],
                                     current_data.gate_energies[4], current_data.gate_energies[5],
                                     current_data.gate_energies[6], current_data.gate_energies[7]);
                        }

                        uint16_t consumed = i + 45;
                        memmove(stream_buf, &stream_buf[consumed], stream_len - consumed);
                        stream_len -= consumed;
                        i = 0;
                    }
                }
            }

            // -------------------------------------------------------------
            // Parser B: Simple Mode ASCII Lines ("ON\r\n", "Range %d\r\n")
            // -------------------------------------------------------------
            for (uint16_t i = 0; i < stream_len; i++) {
                if (stream_buf[i] == '\n' || stream_buf[i] == '\r') {
                    stream_buf[i] = '\0';
                    char *line = (char *)stream_buf;

                    if (strlen(line) > 0) {
                        if (strstr(line, "ON") != NULL) {
                            current_data.occupied = true;
                            current_data.zone = calculate_zone(true, current_data.distance_cm);
                            ESP_LOGI(TAG, "[TEXT] Target: OCCUPIED | %s", zone_name(current_data.zone));
                        } else if (strstr(line, "OFF") != NULL) {
                            current_data.occupied = false;
                            current_data.zone = RADAR_ZONE_CLEAR;
                            ESP_LOGI(TAG, "[TEXT] Target: CLEAR");
                        }

                        int dist = -1;
                        if (sscanf(line, "Range %d", &dist) == 1 || sscanf(line, "%d", &dist) == 1) {
                            current_data.distance_cm = (uint16_t)dist;
                            current_data.zone = calculate_zone(current_data.occupied, current_data.distance_cm);
                            ESP_LOGI(TAG, "[TEXT] Distance: %d cm (%.2f m) | %s",
                                     current_data.distance_cm, (float)current_data.distance_cm / 100.0f,
                                     zone_name(current_data.zone));
                        } else if (strstr(line, "ON") == NULL && strstr(line, "OFF") == NULL) {
                            ESP_LOGI(TAG, "[RADAR RAW]: %s", line);
                        }
                    }

                    uint16_t consumed = i + 1;
                    memmove(stream_buf, &stream_buf[consumed], stream_len - consumed);
                    stream_len -= consumed;
                    break;
                }
            }
        } else {
            // Heartbeat
            int64_t now = esp_timer_get_time();
            if ((now - last_log_time) > (3000 * 1000)) {
                last_log_time = now;
                if (total_bytes_rx == 0) {
                    ESP_LOGI(TAG, "Waiting for radar data... (0 bytes received)");
                } else if ((now - last_rx_byte_time) > (3000 * 1000)) {
                    ESP_LOGI(TAG, "Stream quiet (last RX %" PRIi64 " ms ago, total %" PRIu32 " bytes)",
                             (now - last_rx_byte_time) / 1000, total_bytes_rx);
                }
            }
        }
    }
}