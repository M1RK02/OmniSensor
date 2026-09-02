/* Presence: PIR trigger + mmWave qualifier, and the rail that feeds the radar.
 *
 * The two sensors are not redundant (project_description.md §9). The SR602
 * detects motion, costs almost nothing and can wake the SoC — it is the
 * TRIGGER. The LD2420 detects a stationary person and reports distance, but
 * needs its rail up and a UART running — it is the QUALIFIER, and it holds
 * occupancy true for someone sitting still, the case a PIR alone gets wrong.
 *
 * This task owns the whole presence state machine, including when the radar
 * rail is energised, so there is exactly one place that decides "occupied".
 */

#include "app_priv.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ld2420.h"
#include "pir.h"

#define RADAR_WARMUP_MS      300   /* rail settle before the module talks */
#define RADAR_POLL_MS        100
#define RADAR_LOOP_PACE_MS   50
#define PIR_IDLE_POLL_MS     1000

static const char *TAG = "presence";

static omni_zone_t zone_for_distance(int cm)
{
    if (cm < 0) {
        return OMNI_ZONE_CLEAR;
    }
    if (cm < OMNI_ZONE1_MAX_CM) {
        return OMNI_ZONE_1;
    }
    if (cm < OMNI_ZONE2_MAX_CM) {
        return OMNI_ZONE_2;
    }
    return OMNI_ZONE_3;
}

static void post_presence(omni_zone_t zone, bool occupied, bool zone_valid)
{
    omni_evt_t evt = {
        .type = OMNI_EVT_PRESENCE,
        .presence = { .zone = zone, .occupied = occupied, .zone_valid = zone_valid },
    };
    omni_post_event(&evt);
}

static void presence_task(void *arg)
{
    bool        active         = false;
    omni_zone_t last_zone      = OMNI_ZONE_CLEAR;
    int64_t     last_motion_us = 0;

    /* Watch this loop explicitly: the idle task cannot be watched on a
     * device that light-sleeps, so these are the loops that must prove
     * they are still turning. */
    esp_task_wdt_add(NULL);

    for (;;) {
        esp_task_wdt_reset();

        if (!active) {
            /* Idle: radar rail is off, we are waiting on the PIR only. */
            if (!pir_wait_for_motion(PIR_IDLE_POLL_MS)) {
                continue;
            }

            ESP_LOGI(TAG, "PIR trigger — powering the radar");
            active         = true;
            last_motion_us = esp_timer_get_time();
            last_zone      = OMNI_ZONE_CLEAR;

            /* Occupancy is true the moment the PIR fires. The zone is not known
             * yet, so leave it to the radar rather than guessing. */
            post_presence(OMNI_ZONE_CLEAR, true, false);
            omni_sensor_request_refresh();

            omni_radar_rail_set(true);
#if CONFIG_OMNI_LOAD_SWITCH_PRESENT
            /* Let the rail settle and the module boot before talking to it. */
            vTaskDelay(pdMS_TO_TICKS(RADAR_WARMUP_MS));
#endif
            if (ld2420_init() != ESP_OK) {
                /* Without this the poll below returns ESP_ERR_INVALID_STATE
                 * immediately every iteration, spinning the task at priority 3
                 * and starving the idle task until the watchdog fires. Stay in
                 * PIR-only mode instead. */
                ESP_LOGE(TAG, "radar init failed; staying in PIR-only mode");
                omni_radar_rail_set(false);
                active = false;
            }
            continue;
        }

        /* Active: the radar qualifies what the PIR triggered. */
        int distance_cm = -1;
        if (ld2420_poll_distance_cm(&distance_cm, RADAR_POLL_MS) == ESP_OK) {
            last_motion_us = esp_timer_get_time();
            omni_zone_t zone = zone_for_distance(distance_cm);
            if (zone != last_zone) {
                ESP_LOGI(TAG, "target at %d cm -> zone %d", distance_cm, (int)zone);
                last_zone = zone;
                post_presence(zone, true, true);
            }
        }

        /* A retriggering PIR also counts as continued presence. */
        if (pir_is_asserted()) {
            last_motion_us = esp_timer_get_time();
        }

        /* Pace the loop explicitly. uart_read_bytes usually blocks for the
         * poll timeout, but it returns early once the buffer fills, and this
         * task must never be the reason the idle task does not run. */
        vTaskDelay(pdMS_TO_TICKS(RADAR_LOOP_PACE_MS));

        int64_t idle_ms = (esp_timer_get_time() - last_motion_us) / 1000;
        if (idle_ms > OMNI_INACTIVITY_TIMEOUT_MS) {
            ESP_LOGI(TAG, "no presence for %lld ms — radar rail off", (long long)idle_ms);
            ld2420_deinit();
            omni_radar_rail_set(false);
            active    = false;
            last_zone = OMNI_ZONE_CLEAR;
            post_presence(OMNI_ZONE_CLEAR, false, true);
        }
    }
}

esp_err_t omni_radar_start(void)
{
    BaseType_t ok = xTaskCreate(presence_task, "omni_presence", 4096, NULL,
                                OMNI_PRIO_RADAR, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t omni_pir_start(void)
{
    return pir_init();
}
