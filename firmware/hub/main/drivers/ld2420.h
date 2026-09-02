/* Hi-Link LD2420 — 24 GHz mmWave presence radar over UART.
 *
 * Runs on UART_NUM_1, NOT UART_NUM_0. UART0 is the default ESP-IDF log console;
 * sharing it with the radar interleaves log output with radar traffic and
 * corrupts both. This closes project_description.md §17.1.
 *
 * The module streams human-readable "Range <cm>" lines in its default text mode.
 * Per-gate energy thresholds need the binary protocol and are future work.
 */
#pragma once

#include "esp_err.h"

esp_err_t ld2420_init(void);
esp_err_t ld2420_deinit(void);

/* Poll for the next distance report. Returns:
 *   ESP_OK            — *distance_cm holds a fresh reading
 *   ESP_ERR_TIMEOUT   — nothing arrived in the window (radar quiet = no target)
 *   otherwise         — UART error */
esp_err_t ld2420_poll_distance_cm(int *distance_cm, uint32_t timeout_ms);
