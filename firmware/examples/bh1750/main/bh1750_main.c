#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_err.h"

// I2C Configuration (Seeed XIAO ESP32-C6 default I2C pins: D4=SDA, D5=SCL)
#define I2C_SDA_PIN 22
#define I2C_SCL_PIN 23

// I2C address of the BH1750 (ADDR pin tied to GND → 0x23)
#define BH1750_ADDR 0x23 

// One-Time High-Resolution Mode (1 lux resolution, 120ms measurement time)
#define BH1750_CMD_ONE_TIME_H_RES 0x20 

static const char *TAG = "BH1750_TEST";

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

    // Initialize I2C master bus
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, // Internal pull-ups for SDA and SCL
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BH1750_ADDR,
        .scl_speed_hz = 100000, // 100kHz standard mode
    };
    i2c_master_dev_handle_t dev_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));

    // BH1750 communication
    uint8_t cmd = BH1750_CMD_ONE_TIME_H_RES;
    uint8_t data[2]; // 2 bytes: High byte and Low byte
    uint16_t raw_lux;
    float lux;
    esp_err_t ret;

    // Send the measurement command
    ret = i2c_master_transmit(dev_handle, &cmd, 1, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C transmit failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Wait for measurement to be taken (120ms according to datasheet)
    vTaskDelay(pdMS_TO_TICKS(180));

    // Receive the measurement data
    ret = i2c_master_receive(dev_handle, data, 2, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C receive failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Lux calculation: (High byte << 8) | Low byte, then divide by 1.2 to get lux
    raw_lux = (data[0] << 8) | data[1];
    lux = (float)raw_lux / 1.2f;

    ESP_LOGI(TAG, "Reading completed:");
    ESP_LOGI(TAG, "    Luminosity: %.1f Lux", lux);

cleanup:
    // I2C Cleanup
    i2c_master_bus_rm_device(dev_handle);
    i2c_del_master_bus(bus_handle);

    // Configure Deep Sleep with 60-second timer wakeup
    ESP_LOGI(TAG, "Back to sleep for 60 seconds...");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_sleep_enable_timer_wakeup(60 * 1000000ULL);
    esp_deep_sleep_start();
}