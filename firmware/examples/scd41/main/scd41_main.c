#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_err.h"

// I2C Configuration
#define I2C_SDA_PIN 22
#define I2C_SCL_PIN 23
#define SCD41_ADDR  0x62

// SCD41 Commands
#define SCD41_CMD_SINGLE_SHOT 0x219d // Single shot measure (CO2, T, RH) - takes ~5s
#define SCD41_CMD_READ_MEAS   0xec05 // Read measurement results

static const char *TAG = "SCD41_TEST";

// CRC-8 check for Sensirion sensors (polynomial 0x31, init 0xFF)
static uint8_t sensirion_crc8(const uint8_t *data, size_t len)
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
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "Periodic wakeup from timer");
    } else {
        ESP_LOGI(TAG, "Cold boot or other wakeup reason: %d", wakeup_cause);
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
        .device_address = SCD41_ADDR,
        .scl_speed_hz = 100000,
    };
    i2c_master_dev_handle_t dev_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));

    // SCD41 Communication variables
    uint8_t cmd_measure[2] = {(SCD41_CMD_SINGLE_SHOT >> 8), (SCD41_CMD_SINGLE_SHOT & 0xFF)};
    uint8_t cmd_read[2] = {(SCD41_CMD_READ_MEAS >> 8), (SCD41_CMD_READ_MEAS & 0xFF)};
    uint8_t data[9]; // 3 words (CO2, T, RH) + 3 CRC bytes
    esp_err_t ret;

    // 1. Send Single Shot Measurement Command
    ret = i2c_master_transmit(dev_handle, cmd_measure, 2, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C transmit failed (measure trigger): %s", esp_err_to_name(ret));
        goto cleanup;
    }
    
    // Wait for the single shot measurement to complete (exactly 5000ms per datasheet)
    ESP_LOGI(TAG, "Measurement started, waiting for completion...");

    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI(TAG, "Reading measurement results...");

    // 2. Read Results
    ret = i2c_master_transmit(dev_handle, cmd_read, 2, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C transmit failed (read cmd): %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ret = i2c_master_receive(dev_handle, data, 9, -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C receive failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // 3. Validate CRCs
    if (sensirion_crc8(data, 2) != data[2]) {
        ESP_LOGE(TAG, "CRC mismatch for CO2 data");
        goto cleanup;
    }
    if (sensirion_crc8(data + 3, 2) != data[5]) {
        ESP_LOGE(TAG, "CRC mismatch for temperature data");
        goto cleanup;
    }
    if (sensirion_crc8(data + 6, 2) != data[8]) {
        ESP_LOGE(TAG, "CRC mismatch for humidity data");
        goto cleanup;
    }

    // 4. Convert Data
    {
        uint16_t co2 = (data[0] << 8) | data[1];
        uint16_t t_raw = (data[3] << 8) | data[4];
        uint16_t rh_raw = (data[6] << 8) | data[7];

        float temperature = -45.0f + 175.0f * (float)t_raw / 65536.0f;
        float humidity = 100.0f * (float)rh_raw / 65536.0f;

        ESP_LOGI(TAG, "Reading completed:");
        ESP_LOGI(TAG, "    CO2:         %d ppm", co2);
        ESP_LOGI(TAG, "    Temperature: %.2f °C", temperature);
        ESP_LOGI(TAG, "    Humidity:    %.2f %%", humidity);
    }

cleanup:
    // Cleanup I2C
    i2c_master_bus_rm_device(dev_handle);
    i2c_del_master_bus(bus_handle);

    // Configure deep sleep (60 seconds)
    uint64_t sleep_time_s = 60ULL;
    uint64_t sleep_time_us = sleep_time_s * 1000000ULL;
    ESP_LOGI(TAG, "Back to sleep for %llu seconds...", (unsigned long long)sleep_time_s);
    
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(sleep_time_us));
    esp_deep_sleep_start();
}
