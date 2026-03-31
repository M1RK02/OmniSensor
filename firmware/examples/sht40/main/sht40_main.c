#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_sleep.h"
#include "esp_log.h"

// I2C Configuration
#define I2C_SDA_PIN 22
#define I2C_SCL_PIN 23
#define SHT40_ADDR  0x44
#define SHT40_CMD_MEASURE 0xFD // High precision measure

static const char *TAG = "SHT40_TEST";

// CRC-8 check for SHT40 (polynomial 0x31, init 0xFF)
static uint8_t sht40_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

void app_main(void) {
    // Short delay to allow serial monitor to connect after reset
    vTaskDelay(pdMS_TO_TICKS(5000));

   // Check wakeup reason
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "Periodic wakeup from timer");
    } else {
        ESP_LOGI(TAG, "Cold boot or other wakeup reason: %d", esp_sleep_get_wakeup_cause());
    }

    // Initialize I2C
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SHT40_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));

    // Communicate with SHT40
    uint8_t cmd = SHT40_CMD_MEASURE;
    uint8_t data[6]; // [Temp MSB, Temp LSB, CRC, Hum MSB, Hum LSB, CRC]

    // Send measurement command
    esp_err_t ret = i2c_master_transmit(dev_handle, &cmd, 1, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C transmit failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Wait for measurement to be taken (max 20ms for high precision)
    vTaskDelay(pdMS_TO_TICKS(20));

    // Receive measurement data
    ret = i2c_master_receive(dev_handle, data, 6, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C receive failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Validate CRC for temperature and humidity words
    if (sht40_crc8(data, 2) != data[2]) {
        ESP_LOGE(TAG, "CRC mismatch for temperature data");
        goto cleanup;
    }
    if (sht40_crc8(data + 3, 2) != data[5]) {
        ESP_LOGE(TAG, "CRC mismatch for humidity data");
        goto cleanup;
    }

    // Convert raw data to temperature and humidity
    {
        uint16_t t_ticks = (data[0] << 8) | data[1];
        uint16_t rh_ticks = (data[3] << 8) | data[4];

        float temperature = -45.0f + 175.0f * (float)t_ticks / 65535.0f;
        float humidity = -6.0f + 125.0f * (float)rh_ticks / 65535.0f;

        // Sanitize values (in case of sensor errors)
        if (humidity > 100) humidity = 100;
        if (humidity < 0) humidity = 0;

        ESP_LOGI(TAG, "Reading completed:");
        ESP_LOGI(TAG, "    Temperature: %.2f °C", temperature);
        ESP_LOGI(TAG, "    Humidity:    %.2f %%", humidity);
    }

cleanup:
    // Cleanup I2C
    ESP_ERROR_CHECK(i2c_master_bus_rm_device(dev_handle));
    ESP_ERROR_CHECK(i2c_del_master_bus(bus_handle));

    // Configure deep sleep
    uint64_t sleep_time_us = 60 * 1000000ULL;
    ESP_LOGI(TAG, "Back to sleep for %llu seconds...", (unsigned long long)(sleep_time_us / 1000000ULL));

    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(sleep_time_us));
    esp_deep_sleep_start();
}