/* Sensirion SCD41 — CO2 (photoacoustic NDIR) over I2C (0x62).
 *
 * The SCD41 stays on the always-on rail and is parked with its own power_down
 * command between measurements rather than being cut by the load switch. Doing
 * it in firmware avoids back-powering the device through the shared I2C bus
 * pull-ups, which stay energised for the always-on OLED. See docs/HARDWARE_DESIGN.md §2.
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

esp_err_t scd41_init(i2c_master_bus_handle_t bus);

/* Leave sleep mode. Blocks ~30 ms. The sensor deliberately does NOT acknowledge
 * this command, so an I2C NACK here is expected and not an error. */
esp_err_t scd41_wake_up(void);

/* Enter sleep mode (sub-microamp per the SCD4x datasheet). */
esp_err_t scd41_power_down(void);

/* Single-shot measurement. BLOCKS FOR ~5 SECONDS — this is why the sensor task
 * runs below the Matter stack priority (project_description.md §8).
 * All three CRCs are validated. Temperature and humidity come free with the
 * measurement but are less accurate than the SHT40's; the caller decides. */
esp_err_t scd41_measure_single_shot(uint16_t *co2_ppm, float *temp_c, float *rh_pct);
