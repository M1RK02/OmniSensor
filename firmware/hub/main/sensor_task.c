/* Environmental sensing: SHT40, BH1750 and SCD41, sequenced on one I2C bus.
 *
 * Runs below the Matter stack priority. The SCD41 single shot blocks for ~5 s;
 * at or above network priority that would starve the stack and trip the Task
 * Watchdog (project_description.md §8).
 */

#include "app_priv.h"
#include "bh1750.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "scd41.h"
#include "sht40.h"

static const char *TAG = "sensor";

static TaskHandle_t s_task;
static int64_t      s_last_co2_us = INT64_MIN;

void omni_sensor_request_refresh(void)
{
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

static void post_updating(bool in_progress)
{
    omni_evt_t evt = {
        .type = OMNI_EVT_UPDATING,
        .updating = { .in_progress = in_progress },
    };
    omni_post_event(&evt);
}

/* Has enough time passed to justify paying 5 s of CO2 measurement again?
 * Closes project_description.md §17.6. */
static bool co2_is_due(void)
{
    int64_t now = esp_timer_get_time();
    if (s_last_co2_us == INT64_MIN) {
        return true;
    }
    return (now - s_last_co2_us) >= ((int64_t)OMNI_SCD41_MIN_INTERVAL_MS * 1000);
}

static void measure_once(void)
{
    omni_evt_t evt = { .type = OMNI_EVT_ENV };

    /* The SCD41 single shot blocks for over five seconds and the bus has to stay
     * alive for all of it. Exempting the pins from sleep isolation is not enough
     * on its own, because a sleeping peripheral is not clocked either. */
    omni_stay_awake(true);
    post_updating(true);

    /* Fast sensors first, so the display gets something real quickly. */
    float temp = 0.0f, rh = 0.0f;
    if (sht40_measure(&temp, &rh) == ESP_OK) {
        evt.env.temp_c         = temp;
        evt.env.humidity_pct   = rh;
        evt.env.temp_valid     = true;
        evt.env.humidity_valid = true;
    }

    float lux = 0.0f;
    if (bh1750_measure(&lux) == ESP_OK) {
        evt.env.lux       = lux;
        evt.env.lux_valid = true;
    }

    if (co2_is_due()) {
        uint16_t co2 = 0;
        float    scd_temp = 0.0f, scd_rh = 0.0f;

#if CONFIG_OMNI_BATTERY_PRESENT
        /* Only worth waking it if we put it to sleep, and we only do that on
         * battery. See the power_down call below. */
        scd41_wake_up();
#endif
        if (scd41_measure_single_shot(&co2, &scd_temp, &scd_rh) == ESP_OK) {
            evt.env.co2_ppm   = co2;
            evt.env.co2_valid = true;
            s_last_co2_us     = esp_timer_get_time();

            /* The SCD41 reports temperature and humidity too, but the SHT40 is
             * the more accurate part (±0.2 °C vs ±0.8 °C) and sits further from
             * the module's waste heat. Use the SCD41 only as a fallback —
             * averaging two sensors of different accuracy just pollutes the
             * better one. */
            if (!evt.env.temp_valid) {
                evt.env.temp_c     = scd_temp;
                evt.env.temp_valid = true;
            }
            if (!evt.env.humidity_valid) {
                evt.env.humidity_pct   = scd_rh;
                evt.env.humidity_valid = true;
            }
        }
#if CONFIG_OMNI_BATTERY_PRESENT
        /* Park the sensor whether or not the measurement worked. Sleep mode is
         * a battery optimisation worth roughly 0.15 mA; on USB it buys nothing
         * and costs a wake_up handshake before every measurement, which is one
         * more thing to get wrong. Leave it idle instead. */
        scd41_power_down();
#endif
    }

    post_updating(false);
    omni_stay_awake(false);

    if (evt.env.temp_valid || evt.env.humidity_valid ||
        evt.env.lux_valid  || evt.env.co2_valid) {
        omni_post_event(&evt);
    } else {
        ESP_LOGW(TAG, "measurement cycle produced no valid readings");
    }
}

static void sensor_task(void *arg)
{
    /* First cycle immediately on boot so the UI is not empty for a minute. */
    measure_once();

    for (;;) {
        /* Wake on an explicit refresh request (presence) or on the periodic
         * interval, whichever comes first. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(OMNI_ENV_INTERVAL_MS));
        measure_once();
    }
}

esp_err_t omni_sensor_task_start(void)
{
    i2c_master_bus_handle_t bus = omni_i2c_bus();
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err;
    if ((err = sht40_init(bus))  != ESP_OK) { ESP_LOGE(TAG, "SHT40 init failed");  return err; }
    if ((err = bh1750_init(bus)) != ESP_OK) { ESP_LOGE(TAG, "BH1750 init failed"); return err; }
    if ((err = scd41_init(bus))  != ESP_OK) { ESP_LOGE(TAG, "SCD41 init failed");  return err; }

    BaseType_t ok = xTaskCreate(sensor_task, "omni_sensor", 4096, NULL,
                                OMNI_PRIO_SENSOR, &s_task);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
