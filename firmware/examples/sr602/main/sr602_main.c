#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_log.h"

#define PIR_GPIO GPIO_NUM_2

static const char *TAG = "SR602";

void app_main(void) {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    // Check the wakeup reason and log it
    if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
        ESP_LOGI(TAG, "Wakeup from GPIO");
    } else {
        ESP_LOGI(TAG, "Cold boot or other wakeup reason: %d", wakeup_reason);
    }

    // Configure the PIR GPIO pin as input with pull-down resistor
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE, 
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIR_GPIO),
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Wait for the PIR sensor to be in a stable state (LOW) before going to sleep
    // Timeout after 60 seconds to avoid draining the battery on sensor fault or long retrigger
    if (gpio_get_level(PIR_GPIO) == 1) {
        ESP_LOGI(TAG, "Waiting for PIR sensor to stabilize...");
        const int timeout_ms = 60000;
        int elapsed_ms = 0;
        while (gpio_get_level(PIR_GPIO) == 1 && elapsed_ms < timeout_ms) {
            vTaskDelay(pdMS_TO_TICKS(100));
            elapsed_ms += 100;
        }
        if (gpio_get_level(PIR_GPIO) == 1) {
            ESP_LOGE(TAG, "PIR still HIGH after timeout - rebooting to avoid rapid wake/sleep loop");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        } else {
            ESP_LOGI(TAG, "PIR sensor stabilized");
        }
    }

    // Configure the wakeup source to be the PIR GPIO pin (HIGH level)
    ESP_LOGI(TAG, "Configuring wakeup on GPIO %d (HIGH)", PIR_GPIO);
    ESP_ERROR_CHECK(esp_deep_sleep_enable_gpio_wakeup(1ULL << PIR_GPIO, ESP_GPIO_WAKEUP_GPIO_HIGH));

    // Go to deep sleep
    ESP_LOGI(TAG, "Entering Deep Sleep");
    vTaskDelay(pdMS_TO_TICKS(100)); 
    esp_deep_sleep_start();
}