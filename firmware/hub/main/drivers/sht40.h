/* Sensirion SHT40 — temperature and relative humidity over I2C (0x44). */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t sht40_init(i2c_master_bus_handle_t bus);

/* High-precision single measurement. Blocks ~20 ms. Both CRCs are validated;
 * a mismatch returns ESP_ERR_INVALID_CRC and leaves the outputs untouched. */
esp_err_t sht40_measure(float *temp_c, float *rh_pct);
