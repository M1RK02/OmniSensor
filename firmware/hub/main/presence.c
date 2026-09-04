/* PIR occupancy task. */

#include "app_priv.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pir.h"

#define PIR_IDLE_POLL_MS 1000
#define PIR_ACTIVE_POLL_MS 50

static const char *TAG = "presence";

static void post_presence(bool occupied)
{
    omni_evt_t evt = {
        .type = OMNI_EVT_PRESENCE,
        .presence = { .zone = OMNI_ZONE_CLEAR, .occupied = occupied, .zone_valid = false },
    };
    omni_post_event(&evt);
}

static void presence_task(void *arg)
{
    bool active = false;
    int64_t last_motion_us = 0;

    esp_task_wdt_add(NULL);

    for (;;) {
        esp_task_wdt_reset();

        if (!active) {
            if (!pir_wait_for_motion(PIR_IDLE_POLL_MS)) {
                continue;
            }

            ESP_LOGI(TAG, "PIR trigger");
            active = true;
            last_motion_us = esp_timer_get_time();
            post_presence(true);
            omni_sensor_request_refresh();
            continue;
        }

        if (pir_is_asserted()) {
            last_motion_us = esp_timer_get_time();
        }
        vTaskDelay(pdMS_TO_TICKS(PIR_ACTIVE_POLL_MS));

        int64_t idle_ms = (esp_timer_get_time() - last_motion_us) / 1000;
        if (idle_ms > OMNI_INACTIVITY_TIMEOUT_MS) {
            ESP_LOGI(TAG, "no PIR motion for %lld ms", (long long)idle_ms);
            active = false;
            post_presence(false);
        }
    }
}

esp_err_t omni_presence_start(void)
{
    esp_err_t err = pir_init();
    if (err != ESP_OK) {
        return err;
    }

    BaseType_t ok = xTaskCreate(presence_task, "omni_presence", 3072, NULL,
                                OMNI_PRIO_PRESENCE, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}