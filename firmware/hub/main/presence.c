/*
 * Fused PIR + mmWave Radar Presence Detection Task
 * OmniSensor Project
 *
 * Implements sensor fusion:
 * - SR602 PIR provides instantaneous gross-motion detection (< 10 ms)
 * - HLK-LD2420 24 GHz mmWave radar provides stationary micro-motion presence (breathing)
 *   and distance measurement for zone classification:
 *     - Zone 1: < 0.7 m  (At device)
 *     - Zone 2: 0.7 - 1.4 m (Near field)
 *     - Zone 3: > 1.4 m  (Room presence)
 *
 * Fusion rule:
 * - Fused occupied = (PIR active) || (Radar target detected).
 * - Radar holds occupancy active when a person is sitting still, eliminating false clears.
 * - Distance zone is reported continuously to internal state and the local OLED.
 * - Fused binary occupancy is reported to the Matter Occupancy Sensing endpoint.
 */

#include "app_priv.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ld2420.h"
#include "pir.h"

static const char *TAG = "presence";

static void post_presence(bool occupied, omni_zone_t zone, bool zone_valid)
{
    omni_evt_t evt = {
        .type = OMNI_EVT_PRESENCE,
        .presence = {
            .zone       = zone,
            .occupied   = occupied,
            .zone_valid = zone_valid,
        },
    };
    omni_post_event(&evt);
}

static void presence_task(void *arg)
{
    bool        is_occupied           = false;
    omni_zone_t current_zone          = OMNI_ZONE_CLEAR;
    int64_t     last_activity_time_us = 0;
    bool        radar_has_target      = false;
    uint16_t    last_distance_cm      = 0;
    int64_t     last_log_us           = 0;

    esp_task_wdt_add(NULL);

    ESP_LOGI(TAG, "PIR + Radar presence task running");

    for (;;) {
        esp_task_wdt_reset();

        /* 1. Sample PIR sensor */
        bool pir_active = pir_is_asserted();
        if (pir_active) {
            last_activity_time_us = esp_timer_get_time();
        }

        /* 2. Poll LD2420 mmWave radar over UART0 (50 ms timeout) */
        ld2420_data_t rdata = { 0 };
        bool got_radar = ld2420_poll(&rdata, 50);
        if (got_radar) {
            radar_has_target = rdata.occupied;
            last_distance_cm = rdata.distance_cm;
            if (rdata.occupied) {
                last_activity_time_us = esp_timer_get_time();
                if (rdata.zone != OMNI_ZONE_CLEAR) {
                    current_zone = rdata.zone;
                }
            }
        }

        /* 3. Sensor fusion */
        bool target_present = pir_active || radar_has_target;

        if (target_present) {
            if (!is_occupied) {
                is_occupied = true;
                if (current_zone == OMNI_ZONE_CLEAR) {
                    current_zone = OMNI_ZONE_3; /* Default to room presence until radar refines */
                }
                ESP_LOGI(TAG, "Presence detected -> OCCUPIED (PIR: %d, Radar: %d, Zone: %d, Dist: %u cm)",
                         pir_active, radar_has_target, (int)current_zone, last_distance_cm);
                post_presence(true, current_zone, true);
                omni_sensor_request_refresh();
            } else {
                /* Already occupied: propagate updated distance zone to display and state */
                if (got_radar && rdata.occupied && rdata.zone != OMNI_ZONE_CLEAR && rdata.zone != current_zone) {
                    current_zone = rdata.zone;
                    ESP_LOGI(TAG, "Zone updated -> %d (Dist: %u cm)", (int)current_zone, last_distance_cm);
                    post_presence(true, current_zone, true);
                }
            }
        } else {
            /* Neither sensor actively detects a target right now */
            if (is_occupied) {
                int64_t idle_ms = (esp_timer_get_time() - last_activity_time_us) / 1000;
                if (idle_ms > OMNI_INACTIVITY_TIMEOUT_MS) {
                    ESP_LOGI(TAG, "No PIR or Radar presence for %lld ms -> CLEAR", (long long)idle_ms);
                    is_occupied = false;
                    current_zone = OMNI_ZONE_CLEAR;
                    post_presence(false, OMNI_ZONE_CLEAR, true);
                }
            }
        }

        /* Periodic heartbeat logging (every 10 seconds) */
        int64_t now = esp_timer_get_time();
        if ((now - last_log_us) > (10 * 1000 * 1000)) {
            last_log_us = now;
            ESP_LOGD(TAG, "status: occupied=%d, pir=%d, radar=%d, zone=%d, dist=%u cm",
                     is_occupied, pir_active, radar_has_target, (int)current_zone, last_distance_cm);
        }
    }
}

esp_err_t omni_presence_start(void)
{
    esp_err_t err = pir_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PIR init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Energise the radar rail via SI2301 load switch (GPIO21) */
    omni_radar_rail_set(true);

    /* Initialize LD2420 radar UART0 */
    err = ld2420_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LD2420 UART init failed: %s (continuing with PIR only)", esp_err_to_name(err));
    }

    BaseType_t ok = xTaskCreate(presence_task, "omni_presence", 4096, NULL,
                                OMNI_PRIO_PRESENCE, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}