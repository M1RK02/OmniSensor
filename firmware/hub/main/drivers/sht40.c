#include "sht40.h"

#include "app_priv.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensirion_common.h"

#define SHT40_CMD_MEASURE_HIGH_PRECISION  0xFD
#define SHT40_MEASURE_DELAY_MS            20

static const char *TAG = "sht40";
static i2c_master_dev_handle_t s_dev;

esp_err_t sht40_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = OMNI_ADDR_SHT40,
        .scl_speed_hz    = 100000,
    };
    return i2c_master_bus_add_device(bus, &cfg, &s_dev);
}

esp_err_t sht40_measure(float *temp_c, float *rh_pct)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t cmd = SHT40_CMD_MEASURE_HIGH_PRECISION;
    esp_err_t err = i2c_master_transmit(s_dev, &cmd, 1, 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "measure trigger failed: %s", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(SHT40_MEASURE_DELAY_MS));

    uint8_t data[6]; /* T MSB, T LSB, CRC, RH MSB, RH LSB, CRC */
    err = i2c_master_receive(s_dev, data, sizeof(data), 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read failed: %s", esp_err_to_name(err));
        return err;
    }

    if (sensirion_crc8(data, 2) != data[2]) {
        ESP_LOGE(TAG, "CRC mismatch on temperature word");
        return ESP_ERR_INVALID_CRC;
    }
    if (sensirion_crc8(data + 3, 2) != data[5]) {
        ESP_LOGE(TAG, "CRC mismatch on humidity word");
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t t_ticks  = ((uint16_t)data[0] << 8) | data[1];
    uint16_t rh_ticks = ((uint16_t)data[3] << 8) | data[4];

    float t  = -45.0f + 175.0f * (float)t_ticks / 65535.0f;
    float rh =  -6.0f + 125.0f * (float)rh_ticks / 65535.0f;

    /* The transfer function can run slightly outside 0-100 %RH at the rails. */
    if (rh > 100.0f) rh = 100.0f;
    if (rh < 0.0f)   rh = 0.0f;

    if (temp_c) *temp_c = t;
    if (rh_pct) *rh_pct = rh;
    return ESP_OK;
}
