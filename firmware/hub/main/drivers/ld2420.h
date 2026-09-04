/*
 * HLK-LD2420 24 GHz mmWave Presence Radar Driver
 * OmniSensor Project
 *
 * Communicates over UART0 (GPIO16 TX -> Radar RX, GPIO17 RX <- Radar TX) at 115200 baud.
 * Decodes both Energy Mode binary telemetry and Simple Mode ASCII stream.
 */

#pragma once

#include "app_priv.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool        occupied;
    uint16_t    distance_cm;
    omni_zone_t zone;
    uint16_t    peak_energy;
    uint8_t     peak_gate;
} ld2420_data_t;

/* Initialize UART0 and configure GPIOs for LD2420 radar */
esp_err_t ld2420_init(void);

/* Poll for incoming radar stream data.
 * Reads UART bytes with timeout_ms, updates *data if a new valid state or frame was decoded.
 * Returns true if new data was parsed, false otherwise. */
bool ld2420_poll(ld2420_data_t *data, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
