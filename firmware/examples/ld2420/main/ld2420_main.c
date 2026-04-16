#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_err.h"

// UART0 configuration for the LD2420 radar (XIAO C6 pins: D6=16, D7=17)
#define UART_PORT_NUM      UART_NUM_0
#define UART_BAUD_RATE     115200
#define UART_TX_PIN        16
#define UART_RX_PIN        17
#define BUF_SIZE           256

static const char *TAG = "LD2420_TEST";

void app_main(void) {
    esp_err_t ret;

    // 1. Initialize UART configuration
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // Install UART driver
    ret = uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
        return;
    }

    // Configure UART parameters
    ret = uart_param_config(UART_PORT_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(ret));
        uart_driver_delete(UART_PORT_NUM);
        return;
    }

    // Set UART pins
    ret = uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(ret));
        uart_driver_delete(UART_PORT_NUM);
        return;
    }

    ESP_LOGI(TAG, "UART initialized. Listening for radar TEXT data...");

    // Give the radar some time to power up and send data
    vTaskDelay(pdMS_TO_TICKS(500));

    // 2. Read data continuously
    uint8_t data[BUF_SIZE];
    char last_received[BUF_SIZE] = {0};
    int consecutive_read_errors = 0;
    
    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, data, (BUF_SIZE - 1), pdMS_TO_TICKS(100));

        if (len > 0) {
            consecutive_read_errors = 0;
            
            data[len] = '\0'; 


            for(int i = 0; i < len; i++) {
                if(data[i] == '\r' || data[i] == '\n') {
                    data[i] = ' '; 
                }
            }

            // Anti-spam: only print when the text differs from the previous iteration
            if (strcmp((char*)data, last_received) != 0) {

                // Skip strings made of whitespace only
                if (data[0] != ' ') {
                    ESP_LOGI(TAG, "Radar data: %s", data);
                }

                // Store this reading as the baseline for the next iteration
                strcpy(last_received, (char*)data);
            }

        } else if (len == 0) {
            consecutive_read_errors = 0;
            // No data — the radar is quiet (no movement detected)
        } else {
            consecutive_read_errors++;
            ESP_LOGE(TAG, "UART read failed with return value: %d", len);
            if (consecutive_read_errors >= 5) {
                ESP_LOGE(TAG, "Too many UART read errors, stopping reader");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    ESP_LOGI(TAG, "Cleaning up UART...");
    uart_driver_delete(UART_PORT_NUM);
}