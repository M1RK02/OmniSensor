/* Shared I2C bus. One bus, four devices: SHT40, SCD41, BH1750 (100 kHz) and
 * the SSD1315 OLED (400 kHz). Per-device speeds are set when each driver adds
 * itself to the bus. */

#include "app_priv.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "omni_i2c";
static i2c_master_bus_handle_t s_bus;

esp_err_t omni_i2c_init(void)
{
    if (s_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t cfg = {
        .i2c_port                     = I2C_NUM_0,
        .sda_io_num                   = OMNI_PIN_I2C_SDA,
        .scl_io_num                   = OMNI_PIN_I2C_SCL,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        /* The wired prototype has no external pull-ups, so the internal ones
         * (weak, tens of kOhm) carry the bus. The PCB specifies external 4.7k
         * on the always-on rail — see hardware/DESIGN_PACKAGE.md. Leave this
         * enabled either way: it is harmless in parallel with a stiffer pull-up. */
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bus init failed: %s", esp_err_to_name(err));
        return err;
    }
    /* Light sleep isolates every GPIO on this chip (PM_SLP_DISABLE_GPIO, pulled
     * in by the C6's ESD erratum workaround and not overridable from Kconfig).
     * Isolating SDA and SCL cuts a transfer in half and leaves the bus in an
     * undefined state, which a sensor recovers from only by being unplugged.
     * Exempt these two; the workaround stays in force everywhere else. */
    gpio_sleep_sel_dis(OMNI_PIN_I2C_SDA);
    gpio_sleep_sel_dis(OMNI_PIN_I2C_SCL);

    ESP_LOGI(TAG, "I2C bus up (SDA %d / SCL %d)", OMNI_PIN_I2C_SDA, OMNI_PIN_I2C_SCL);

    /* Scan once and say what is actually on the bus. A device that does not
     * appear here is a wiring or power problem, not a driver problem, and
     * that distinction is otherwise guesswork from the far end of a log. */
    char found[64];
    int  len = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(s_bus, addr, 50) == ESP_OK && len < (int)sizeof(found) - 6) {
            len += snprintf(found + len, sizeof(found) - len, " 0x%02X", addr);
        }
    }
    if (len == 0) {
        ESP_LOGE(TAG, "bus scan found nothing at all — check SDA/SCL and power");
    } else {
        ESP_LOGI(TAG, "bus scan:%s  (expect 0x23 0x3C 0x44 0x62)", found);
    }
    return ESP_OK;
}

i2c_master_bus_handle_t omni_i2c_bus(void)
{
    return s_bus;
}
