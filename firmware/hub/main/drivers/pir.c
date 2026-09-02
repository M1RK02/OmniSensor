#include "pir.h"

#include "app_priv.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* How often the pin is sampled while waiting for motion. */
#define PIR_POLL_INTERVAL_MS  50

static const char *TAG = "pir";

esp_err_t pir_init(void)
{
    /* No runtime interrupt on this pin, deliberately.
     *
     * Light-sleep GPIO wakeup only supports level triggers, and
     * gpio_wakeup_enable() sets the pin's interrupt type to do it — which
     * overwrites any edge type registered for a runtime ISR. A level-triggered
     * interrupt whose condition persists re-enters its handler forever, so a
     * PIR that happens to be asserted when this runs locks the CPU out of
     * every other task. Polling a pin every 50 ms costs nothing and cannot do
     * that; the wake itself is handled by the sleep subsystem below, not by an
     * ISR, so instant wake is unaffected. */
    gpio_config_t io_conf = {
        .intr_type    = GPIO_INTR_DISABLE,
        .mode         = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << OMNI_PIN_PIR,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }

    /* Arm as a light-sleep wake source. This sets the pin's interrupt type to
     * level-high, but leaves the CPU interrupt itself disabled, which is
     * exactly what we want: it wakes the chip without ever running a handler. */
    err = gpio_wakeup_enable(OMNI_PIN_PIR, GPIO_INTR_HIGH_LEVEL);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "gpio_wakeup_enable failed: %s", esp_err_to_name(err));
    } else {
        esp_sleep_enable_gpio_wakeup();
    }

    ESP_LOGI(TAG, "PIR armed on GPIO%d (level %d)",
             OMNI_PIN_PIR, gpio_get_level(OMNI_PIN_PIR));
    return ESP_OK;
}

bool pir_is_asserted(void)
{
    return gpio_get_level(OMNI_PIN_PIR) == 1;
}

bool pir_wait_for_motion(uint32_t timeout_ms)
{
    uint32_t waited = 0;
    while (waited < timeout_ms) {
        if (pir_is_asserted()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(PIR_POLL_INTERVAL_MS));
        waited += PIR_POLL_INTERVAL_MS;
    }
    return pir_is_asserted();
}
