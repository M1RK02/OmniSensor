/* Power management and battery ADC.
 *
 * Battery ADC (project_description.md §7): 2x100k divider into GPIO0 with a
 * 100 nF reservoir, -12 dB attenuation, 32x multisampling, curve-fitting
 * calibration. Sampled before the radio comes up, because RF activity couples
 * noise onto the ADC input.
 */

#include "app_priv.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_pm.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BATT_ADC_UNIT        ADC_UNIT_1
#define BATT_ADC_CHANNEL     ADC_CHANNEL_0      /* GPIO0 / D0 / A0 */
#define BATT_ADC_ATTEN       ADC_ATTEN_DB_12    /* usable to ~3.1 V */
#define BATT_SAMPLE_COUNT    32                 /* multisampling, §7 */
#define BATT_DIVIDER_RATIO   2                  /* two equal 100k resistors */

/* LiPo discharge curve, open-circuit. Deliberately not a straight line: a
 * linear map badly overstates remaining charge across the long 3.7-3.9 V
 * plateau where the cell spends most of its life. */
typedef struct {
    uint16_t mv;
    uint8_t  pct;
} batt_point_t;

static const batt_point_t k_batt_curve[] = {
    { 4200, 100 }, { 4100, 92 }, { 4000, 82 }, { 3900, 70 }, { 3800, 57 },
    { 3750, 47 }, { 3700, 36 }, { 3650, 26 }, { 3600, 17 }, { 3500, 8 },
    { 3400, 3 },  { 3300, 0 },
};

static const char *TAG = "power";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;
static bool                      s_cali_enabled;
#if CONFIG_PM_ENABLE
static esp_pm_lock_handle_t      s_active_lock;
#endif

static uint8_t batt_mv_to_pct(uint16_t mv)
{
    const size_t n = sizeof(k_batt_curve) / sizeof(k_batt_curve[0]);
    if (mv >= k_batt_curve[0].mv) {
        return 100;
    }
    if (mv <= k_batt_curve[n - 1].mv) {
        return 0;
    }
    for (size_t i = 1; i < n; i++) {
        if (mv >= k_batt_curve[i].mv) {
            const batt_point_t *hi = &k_batt_curve[i - 1];
            const batt_point_t *lo = &k_batt_curve[i];
            int span_mv  = hi->mv - lo->mv;
            int span_pct = hi->pct - lo->pct;
            return (uint8_t)(lo->pct + ((mv - lo->mv) * span_pct) / span_mv);
        }
    }
    return 0;
}

static esp_err_t battery_adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = BATT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = BATT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, BATT_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    /* esp_adc_cal is gone in IDF 5.x; the ESP32-C6 uses the curve-fitting
     * scheme, which reads the per-chip correction burned into eFuse. */
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = BATT_ADC_UNIT,
        .chan     = BATT_ADC_CHANNEL,
        .atten    = BATT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK) {
        s_cali_enabled = true;
        ESP_LOGI(TAG, "ADC curve-fitting calibration enabled");
    } else {
        ESP_LOGW(TAG, "ADC calibration unavailable; readings will be uncalibrated");
    }
#else
    ESP_LOGW(TAG, "no ADC calibration scheme for this target");
#endif
    return ESP_OK;
}

static esp_err_t rf_switch_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << OMNI_PIN_RF_SWITCH_EN) | (1ULL << OMNI_PIN_RF_ANT_SEL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to configure RF switch GPIOs: %s", esp_err_to_name(err));
        return err;
    }

    /* Enable RF switch (active LOW on Seeed XIAO ESP32-C6) */
    gpio_set_level(OMNI_PIN_RF_SWITCH_EN, 0);

    /* Select internal ceramic antenna (0 = internal ceramic, 1 = external U.FL) */
    gpio_set_level(OMNI_PIN_RF_ANT_SEL, 0);

    /* Exempt these pins from sleep isolation so the RF path remains intact */
    gpio_sleep_sel_dis(OMNI_PIN_RF_SWITCH_EN);
    gpio_sleep_sel_dis(OMNI_PIN_RF_ANT_SEL);

    ESP_LOGI(TAG, "RF switch enabled (internal ceramic antenna selected)");
    return ESP_OK;
}

esp_err_t omni_power_init(void)
{
#if CONFIG_PM_ENABLE
    if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "omni_active", &s_active_lock) != ESP_OK) {
        ESP_LOGW(TAG, "could not create the no-light-sleep lock");
        s_active_lock = NULL;
    }
#endif

    rf_switch_init();

    return battery_adc_init();
}

void omni_stay_awake(bool hold)
{
#if CONFIG_PM_ENABLE
    if (s_active_lock == NULL) {
        return;
    }
    /* esp_pm_lock counts acquisitions, so independent callers can nest freely. */
    if (hold) {
        esp_pm_lock_acquire(s_active_lock);
    } else {
        esp_pm_lock_release(s_active_lock);
    }
#endif
}

esp_err_t omni_battery_sample(uint16_t *mv_out, uint8_t *pct_out)
{
    if (s_adc == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int32_t accumulator = 0;
    int      valid      = 0;
    for (int i = 0; i < BATT_SAMPLE_COUNT; i++) {
        int raw;
        if (adc_oneshot_read(s_adc, BATT_ADC_CHANNEL, &raw) == ESP_OK) {
            accumulator += raw;
            valid++;
        }
    }
    if (valid == 0) {
        ESP_LOGE(TAG, "no valid ADC samples");
        return ESP_FAIL;
    }

    int mean_raw = (int)(accumulator / valid);
    int mv_at_pin;
    if (s_cali_enabled) {
        if (adc_cali_raw_to_voltage(s_cali, mean_raw, &mv_at_pin) != ESP_OK) {
            return ESP_FAIL;
        }
    } else {
        /* Rough fallback: 12-bit full scale against the ~3.1 V -12 dB range. */
        mv_at_pin = (mean_raw * 3100) / 4095;
    }

    uint16_t batt_mv = (uint16_t)(mv_at_pin * BATT_DIVIDER_RATIO);
    uint8_t  pct     = batt_mv_to_pct(batt_mv);

    if (mv_out)  *mv_out  = batt_mv;
    if (pct_out) *pct_out = pct;
    ESP_LOGI(TAG, "battery: raw %d -> %d mV at pin -> %u mV cell (%u%%)",
             mean_raw, mv_at_pin, batt_mv, pct);
    return ESP_OK;
}

static void battery_task(void *arg)
{
    /* One immediate reading so the UI is populated, then a slow cadence:
     * a LiPo does not move fast enough to justify waking for it. */
    for (;;) {
        uint16_t mv  = 0;
        uint8_t  pct = 0;
        if (omni_battery_sample(&mv, &pct) == ESP_OK) {
            omni_evt_t evt = {
                .type = OMNI_EVT_BATTERY,
                .battery = { .mv = mv, .pct = pct },
            };
            omni_post_event(&evt);
        }
        vTaskDelay(pdMS_TO_TICKS(OMNI_BATT_INTERVAL_MS));
    }
}

esp_err_t omni_battery_task_start(void)
{
    BaseType_t ok = xTaskCreate(battery_task, "omni_batt", 3072, NULL,
                                OMNI_PRIO_BATTERY, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
