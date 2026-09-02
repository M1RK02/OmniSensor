#include "pir.h"

#include "app_priv.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "pir";
static SemaphoreHandle_t s_motion_sem;

static void IRAM_ATTR pir_isr_handler(void *arg)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_motion_sem, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

esp_err_t pir_init(void)
{
    s_motion_sem = xSemaphoreCreateBinary();
    if (s_motion_sem == NULL) {
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_POSEDGE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << OMNI_PIN_PIR,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {  /* already installed is fine */
        return err;
    }

    err = gpio_isr_handler_add(OMNI_PIN_PIR, pir_isr_handler, NULL);
    if (err != ESP_OK) {
        return err;
    }

    /* Arm as a light-sleep wake source. The hub sleeps LIGHT, not deep: deep
     * sleep would tear down the Thread stack and drop us off the mesh. */
    err = gpio_wakeup_enable(OMNI_PIN_PIR, GPIO_INTR_HIGH_LEVEL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "gpio_wakeup_enable failed: %s", esp_err_to_name(err));
    } else {
        esp_sleep_enable_gpio_wakeup();
    }

    ESP_LOGI(TAG, "PIR armed on GPIO%d", OMNI_PIN_PIR);
    return ESP_OK;
}

bool pir_is_asserted(void)
{
    return gpio_get_level(OMNI_PIN_PIR) == 1;
}

bool pir_wait_for_motion(uint32_t timeout_ms)
{
    if (s_motion_sem == NULL) {
        return false;
    }
    return xSemaphoreTake(s_motion_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
