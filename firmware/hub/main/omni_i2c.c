/* Shared I2C bus. One bus, four devices: SHT40, SCD41, BH1750 (100 kHz) and
 * the SSD1315 OLED (400 kHz). Per-device speeds are set when each driver adds
 * itself to the bus. */

#include "app_priv.h"
#include "esp_log.h"

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
    ESP_LOGI(TAG, "I2C bus up (SDA %d / SCL %d)", OMNI_PIN_I2C_SDA, OMNI_PIN_I2C_SCL);
    return ESP_OK;
}

i2c_master_bus_handle_t omni_i2c_bus(void)
{
    return s_bus;
}
