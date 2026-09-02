/* ROHM BH1750FVI — ambient light over I2C (0x23, ADDR tied to GND). */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t bh1750_init(i2c_master_bus_handle_t bus);

/* One-time high-resolution measurement. Blocks ~180 ms, then powers the sensor
 * down automatically (one-time modes return to power-down after conversion). */
esp_err_t bh1750_measure(float *lux);
