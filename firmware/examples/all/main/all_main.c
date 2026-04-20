#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "SMART_HUB";

// ==========================================
// 1. HARDWARE PINS & CONSTANTS
// ==========================================
#define I2C_SDA_PIN         22
#define I2C_SCL_PIN         23
#define PIR_GPIO            GPIO_NUM_2

#define UART_PORT_NUM       UART_NUM_0  
#define UART_BAUD_RATE      115200
#define UART_TX_PIN         16          
#define UART_RX_PIN         17          
#define BUF_SIZE            256
#define SLEEP_TIMEOUT_MS    10000       

#define BH1750_ADDR         0x23
#define SCD41_ADDR          0x62
#define SHT40_ADDR          0x44
#define OLED_ADDR           0x3C

// ==========================================
// 2. RTC MEMORY (Survives Deep Sleep)
// ==========================================
RTC_DATA_ATTR float rtc_temp = 0.0f;
RTC_DATA_ATTR float rtc_hum = 0.0f;
RTC_DATA_ATTR uint16_t rtc_co2 = 0;
RTC_DATA_ATTR float rtc_lux = 0.0f;
RTC_DATA_ATTR bool rtc_has_data = false; // False on first boot, True after first read

// ==========================================
// 3. MINIMAL 5x7 OLED FONT
// ==========================================
const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62}, {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1C, 0x00}, {0x14, 0x08, 0x3E, 0x08, 0x14}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14}, {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E}, {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}
};

// ==========================================
// 4. OLED HELPER FUNCTIONS
// ==========================================
void oled_init(i2c_master_dev_handle_t dev) {
    uint8_t cmds[] = { 0x00, 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF };
    i2c_master_transmit(dev, cmds, sizeof(cmds), -1);
}

void oled_clear(i2c_master_dev_handle_t dev) {
    uint8_t buffer[129];
    buffer[0] = 0x40; // Data stream
    memset(&buffer[1], 0, 128);
    for (uint8_t i = 0; i < 8; i++) {
        uint8_t page_cmd[] = { 0x00, 0xB0 | i, 0x00, 0x10 };
        i2c_master_transmit(dev, page_cmd, 4, -1);
        i2c_master_transmit(dev, buffer, 129, -1);
    }
}

void oled_write_string(i2c_master_dev_handle_t dev, const char* str, uint8_t page, uint8_t col) {
    uint8_t page_cmd[] = { 0x00, 0xB0 | (page & 0x07), col & 0x0F, 0x10 | (col >> 4) };
    i2c_master_transmit(dev, page_cmd, 4, -1);
    while (*str) {
        char c = *str++;
        if (c < 32 || c > 90) c = 32; // Map unprintable to Space
        uint8_t data[6] = { 0x40 };
        memcpy(&data[1], font5x7[c - 32], 5);
        i2c_master_transmit(dev, data, 6, -1);
    }
}

void display_sensor_values(i2c_master_dev_handle_t h_oled, float t, float h, uint16_t c, float l) {
    char buf[20];
    oled_clear(h_oled);
    sprintf(buf, "TEMP: %.1f C", t); oled_write_string(h_oled, buf, 0, 0);
    sprintf(buf, "HUM:  %.1f %%", h); oled_write_string(h_oled, buf, 1, 0);
    sprintf(buf, "CO2:  %d PPM", c);  oled_write_string(h_oled, buf, 2, 0);
    sprintf(buf, "LUX:  %.1f", l);    oled_write_string(h_oled, buf, 3, 0);
}

// ==========================================
// 5. MAIN APPLICATION
// ==========================================
void app_main(void) {
    // Determine if we woke up from PIR or Cold Boot
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    bool is_pir_wakeup = (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO);

    // Give serial monitor a moment on cold boot
    if (!is_pir_wakeup) vTaskDelay(pdMS_TO_TICKS(1000));
    ESP_LOGI(TAG, "Smart Hub Starting...");

    // --- I2C INITIALIZATION ---
    i2c_master_bus_config_t bus_cfg = { 
        .i2c_port = I2C_NUM_0, .sda_io_num = I2C_SDA_PIN, .scl_io_num = I2C_SCL_PIN, 
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true 
    };
    i2c_master_bus_handle_t bus_h;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_h));

    i2c_device_config_t d_oled = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = OLED_ADDR,  .scl_speed_hz = 400000 };
    i2c_device_config_t d_sht  = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = SHT40_ADDR, .scl_speed_hz = 100000 };
    i2c_device_config_t d_scd  = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = SCD41_ADDR, .scl_speed_hz = 100000 };
    i2c_device_config_t d_bh   = { .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = BH1750_ADDR, .scl_speed_hz = 100000 };

    i2c_master_dev_handle_t h_oled, h_sht, h_scd, h_bh;
    i2c_master_bus_add_device(bus_h, &d_oled, &h_oled);
    i2c_master_bus_add_device(bus_h, &d_sht,  &h_sht);
    i2c_master_bus_add_device(bus_h, &d_scd,  &h_scd);
    i2c_master_bus_add_device(bus_h, &d_bh,   &h_bh);

    // --- UART INITIALIZATION (RADAR) ---
    uart_config_t uart_cfg = { 
        .baud_rate = UART_BAUD_RATE, .data_bits = UART_DATA_8_BITS, .parity = UART_PARITY_DISABLE, 
        .stop_bits = UART_STOP_BITS_1, .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT 
    };
    uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT_NUM, &uart_cfg);
    uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // --- GPIO INITIALIZATION (PIR) ---
    gpio_config_t pir_cfg = { .mode = GPIO_MODE_INPUT, .pin_bit_mask = (1ULL << PIR_GPIO), .pull_down_en = GPIO_PULLDOWN_ENABLE };
    gpio_config(&pir_cfg);

    // --- OLED INITIAL WAKEUP LOGIC ---
    oled_init(h_oled);
    
    if (is_pir_wakeup && rtc_has_data) {
        // INSTANT WAKE: Show the last saved data immediately
        display_sensor_values(h_oled, rtc_temp, rtc_hum, rtc_co2, rtc_lux);
        oled_write_string(h_oled, "(UPDATING...)", 5, 0); // Temporary visual feedback
    } else {
        // COLD BOOT: Show booting screen
        oled_clear(h_oled);
        oled_write_string(h_oled, "BOOTING SENSORS...", 3, 10);
    }

    // --- STAGGERED SENSOR WAKEUP ---
    uint8_t scd_cmd[] = {0x21, 0x9D}; i2c_master_transmit(h_scd, scd_cmd, 2, -1);
    vTaskDelay(pdMS_TO_TICKS(4800)); // Wait for SCD41

    uint8_t bh_cmd = 0x20; i2c_master_transmit(h_bh, &bh_cmd, 1, -1);
    vTaskDelay(pdMS_TO_TICKS(180)); // Wait for BH1750

    uint8_t sht_cmd = 0xFD; i2c_master_transmit(h_sht, &sht_cmd, 1, -1);
    vTaskDelay(pdMS_TO_TICKS(40)); // Wait for SHT40

    // --- SENSOR DATA COLLECTION ---
    uint8_t data[9];
    float sum_temp = 0, sum_hum = 0;
    int valid_temp_reads = 0, valid_hum_reads = 0;
    uint16_t co2 = 0;
    float lux = 0;

    // Read SCD41
    uint8_t scd_read_cmd[] = {0xEC, 0x05};
    if (i2c_master_transmit(h_scd, scd_read_cmd, 2, -1) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(5)); 
        if (i2c_master_receive(h_scd, data, 9, -1) == ESP_OK) {
            co2 = (data[0] << 8) | data[1];
            sum_temp += -45.0f + 175.0f * (float)((data[3] << 8) | data[4]) / 65536.0f;
            sum_hum += 100.0f * (float)((data[6] << 8) | data[7]) / 65536.0f;
            valid_temp_reads++; valid_hum_reads++;
        }
    }

    // Read SHT40
    if (i2c_master_receive(h_sht, data, 6, -1) == ESP_OK) {
        sum_temp += -45.0f + 175.0f * (float)((data[0] << 8) | data[1]) / 65535.0f;
        sum_hum += -6.0f + 125.0f * (float)((data[3] << 8) | data[4]) / 65535.0f;
        valid_temp_reads++; valid_hum_reads++;
    }

    // Read BH1750
    if (i2c_master_receive(h_bh, data, 2, -1) == ESP_OK) {
        lux = (float)((data[0] << 8) | data[1]) / 1.2f;
    }

    // Calculate Averages and Save to RTC Memory
    if (valid_temp_reads > 0) rtc_temp = sum_temp / valid_temp_reads;
    if (valid_hum_reads > 0) rtc_hum = sum_hum / valid_hum_reads;
    if (co2 > 0) rtc_co2 = co2;
    rtc_lux = lux;
    rtc_has_data = true; // Mark that we have valid data saved for the next wakeup

    // --- UPDATE OLED SCREEN WITH FRESH DATA ---
    display_sensor_values(h_oled, rtc_temp, rtc_hum, rtc_co2, rtc_lux);

    // --- MAIN ACTIVE LOOP (RADAR & PIR MONITORING) ---
    uint8_t r_buf[BUF_SIZE];
    char radar_accumulator[256] = ""; 
    char last_radar_msg[32] = "";
    uint32_t last_movement_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        if (gpio_get_level(PIR_GPIO) == 1) last_movement_time = current_time;

        if ((current_time - last_movement_time) > SLEEP_TIMEOUT_MS) {
            ESP_LOGI(TAG, "Timeout Reached. Entering Sleep Sequence.");
            break;
        }

        // Robust Line-by-Line Radar Reading
        int len = uart_read_bytes(UART_PORT_NUM, r_buf, BUF_SIZE - 1, pdMS_TO_TICKS(50));
        if (len > 0) {
            r_buf[len] = '\0';
            
            // Add to accumulator safely
            if (strlen(radar_accumulator) + len < sizeof(radar_accumulator) - 1) {
                strcat(radar_accumulator, (char*)r_buf);
            } else {
                radar_accumulator[0] = '\0'; // Prevent buffer overflow
            }

            char *newline_ptr;
            // Process every full line ending with \n
            while ((newline_ptr = strchr(radar_accumulator, '\n')) != NULL) {
                *newline_ptr = '\0'; // Temporarily terminate the string at \n
                char *line = radar_accumulator;
                
                // Safely extract the integer after "Range "
                char *range_ptr = strstr(line, "Range ");
                if (range_ptr != NULL) {
                    int distance = -1;
                    // sscanf is much safer than atoi for formatted string extraction
                    if (sscanf(range_ptr, "Range %d", &distance) == 1) {
                        char clean_msg[20];
                        sprintf(clean_msg, "RADAR: %d", distance);
                        
                        if (strcmp(clean_msg, last_radar_msg) != 0) {
                            oled_write_string(h_oled, "                ", 5, 0); 
                            oled_write_string(h_oled, clean_msg, 5, 0);          
                            strcpy(last_radar_msg, clean_msg);                   
                        }
                    }
                }
                
                // Remove the processed line from the accumulator
                memmove(radar_accumulator, newline_ptr + 1, strlen(newline_ptr + 1) + 1);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }

    // --- DEEP SLEEP SEQUENCE ---
    uint8_t sleep_cmds[] = { 0x00, 0xAE, 0x00, 0x8D, 0x00, 0x10 };
    i2c_master_transmit(h_oled, sleep_cmds, sizeof(sleep_cmds), -1);
    
    while(gpio_get_level(PIR_GPIO) == 1) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    esp_deep_sleep_enable_gpio_wakeup(1ULL << PIR_GPIO, ESP_GPIO_WAKEUP_GPIO_HIGH);
    esp_deep_sleep_start();
}