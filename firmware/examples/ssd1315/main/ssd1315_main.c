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
#define SSD1315_ADDR 0x3C // Standard I2C address for SSD1306/SSD1315

static const char *TAG = "SSD1315_TEST";

// Basic initialization sequence for SSD1306/SSD1315 128x64 OLED screens.
// The first byte (0x00) tells the chip that the following bytes are "Commands" and not "Graphic data".
static const uint8_t oled_init_cmds[] = {
    0x00,       // Control byte: Command stream
    0xAE,       // Display OFF
    0xD5, 0x80, // Set display clock divide
    0xA8, 0x3F, // Set multiplex ratio (1/64)
    0xD3, 0x00, // Set display offset
    0x40,       // Set start line (0)
    0x8D, 0x14, // Enable Charge Pump (Crucial, otherwise the screen remains black)
    0x20, 0x00, // Memory addressing mode (Horizontal)
    0xA1,       // Segment remap (Horizontal flip)
    0xC8,       // COM scan direction (Vertical flip)
    0xDA, 0x12, // COM hardware configuration
    0x81, 0xCF, // Set contrast (0x00 to 0xFF)
    0xD9, 0xF1, // Pre-charge period
    0xDB, 0x40, // VCOMH deselect level
    0xA4,       // Entire display ON resume
    0xA6,       // Normal display (0xA7 for inverted colors)
    0xAF        // Display ON
};

// Command to turn on all pixels (Hardware test)
static const uint8_t oled_test_on[] = { 0x00, 0xA5 }; 
// OLED sleep sequence bytes: 0x00/0xAE (Display OFF), 0x00/0x8D (Charge Pump command), 0x00/0x10 (Charge Pump disable).
static const uint8_t oled_sleep[] = { 0x00, 0xAE, 0x00, 0x8D, 0x00, 0x10 };

void app_main(void) {
    // Short delay to allow serial monitor to connect after reset
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 1. Check wakeup reason
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG, "Periodic wakeup from timer");
    } else {
        ESP_LOGI(TAG, "Cold boot or other wakeup reason: %d", wakeup_cause);
    }

    esp_err_t ret;

    // 2. Initialize I2C
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
        .device_address = SSD1315_ADDR,
        .scl_speed_hz = 400000, // OLED panels support fast I2C (400kHz)
    };
    i2c_master_dev_handle_t dev_handle;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));

    // 3. Send OLED Initialization
    ESP_LOGI(TAG, "Initializing OLED screen...");
    ret = i2c_master_transmit(dev_handle, oled_init_cmds, sizeof(oled_init_cmds), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C transmit failed (Init): %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // 4. Visual Test: Turn on all pixels
    ESP_LOGI(TAG, "Turning on all pixels (Hardware test)...");
    ret = i2c_master_transmit(dev_handle, oled_test_on, sizeof(oled_test_on), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C transmit failed (Test ON): %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Keep the screen on for 3 seconds to allow visual verification
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 5. Turn off the OLED before Deep Sleep (CRITICAL FOR BATTERY LIFE)
    ESP_LOGI(TAG, "Turning off the screen to save energy...");
    ret = i2c_master_transmit(dev_handle, oled_sleep, sizeof(oled_sleep), -1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C transmit failed (Sleep sequence), retrying once: %s", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(10));
        ret = i2c_master_transmit(dev_handle, oled_sleep, sizeof(oled_sleep), -1);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "I2C transmit failed (Sleep sequence retry): %s", esp_err_to_name(ret));
        }
    }

cleanup:
    // 6. Cleanup I2C
    i2c_master_bus_rm_device(dev_handle);
    i2c_del_master_bus(bus_handle);

    // 7. Configure Deep Sleep (30 seconds timer)
    uint64_t sleep_time_us = 30 * 1000000ULL;
    ESP_LOGI(TAG, "Back to sleep for %llu seconds...", (unsigned long long)(sleep_time_us / 1000000ULL));
    
    vTaskDelay(pdMS_TO_TICKS(100)); // Delay to flush the serial log buffer
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(sleep_time_us));
    esp_deep_sleep_start();
}
