#include "bh1750.h"

#include "app_priv.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* One-time H-resolution mode: 1 lx resolution, 120 ms typical / 180 ms max. */
#define BH1750_CMD_ONE_TIME_H_RES   0x20
#define BH1750_CONVERSION_DELAY_MS  180

static const char *TAG = "bh1750";
static i2c_master_dev_handle_t s_dev;

esp_err_t bh1750_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = OMNI_ADDR_BH1750,
        .scl_speed_hz    = 100000,
    };
    return i2c_master_bus_add_device(bus, &cfg, &s_dev);
}

esp_err_t bh1750_measure(float *lux)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t cmd = BH1750_CMD_ONE_TIME_H_RES;
    esp_err_t err = i2c_master_transmit(s_dev, &cmd, 1, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "measure trigger failed: %s", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(BH1750_CONVERSION_DELAY_MS));

    uint8_t data[2];
    err = i2c_master_receive(s_dev, data, sizeof(data), 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read failed: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t raw = ((uint16_t)data[0] << 8) | data[1];
    if (lux) *lux = (float)raw / 1.2f;
    return ESP_OK;
}
