/*
 * OmniSensor hub — Matter over Thread, Intermittently Connected Device.
 *
 * This file owns two things and nothing else:
 *   1. The single-writer shared state. Every producer posts an event; this task
 *      is the only code that writes omni_state_t (project_description.md §8).
 *   2. The Matter data model, and pushing state onto it.
 *
 * Sensor access, presence logic, the display and power all live in their own
 * translation units and never touch Matter directly.
 */

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_ota.h>
#include <nvs_flash.h>
#include <math.h>

#include <app_openthread_config.h>
#include <app_priv.h>
#include <app_reset.h>
#include <common_macros.h>

#include <button_gpio.h>
#include <iot_button.h>

#if CONFIG_ENABLE_ICD_SERVER
#include <app/icd/server/ICDNotifier.h>
#endif

#include <app/clusters/air-quality-server/AirQualityCluster.h>
#include <app/clusters/illuminance-measurement-server/IlluminanceMeasurementCluster.h>
#include <app/clusters/occupancy-sensor-server/OccupancySensingCluster.h>
#include <app/clusters/relative-humidity-measurement-server/RelativeHumidityMeasurementCluster.h>
#include <app/clusters/temperature-measurement-server/TemperatureMeasurementCluster.h>
#include <data_model_provider/esp_matter_data_model_provider.h>

static const char *TAG = "omni_hub";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

/* ==========================================================================
 * Shared state — written by the state owner task only
 * ========================================================================== */
static portMUX_TYPE  s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static omni_state_t  s_state;
static QueueHandle_t s_queue;

/* Scheduling work onto the Matter thread before esp_matter::start() has run is
 * a fatal error in CHIP, not a soft failure. Sensor and presence events can
 * arrive before then — a PIR that is already asserted at boot produces one
 * immediately — so every reporter checks this first. */
static volatile bool s_matter_ready;

static struct {
    uint16_t temperature;
    uint16_t humidity;
    uint16_t illuminance;
    uint16_t air_quality;
    uint16_t occupancy;
} s_endpoint;

extern "C" QueueHandle_t omni_event_queue(void)
{
    return s_queue;
}

extern "C" esp_err_t omni_post_event(const omni_evt_t *evt)
{
    if (s_queue == nullptr || evt == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Never block a producer on a full queue: a dropped reading is recoverable,
     * a stalled sensor task is not. */
    if (xQueueSend(s_queue, evt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropping event type %d", (int)evt->type);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

extern "C" void omni_get_state(omni_state_t *out)
{
    if (out == nullptr) {
        return;
    }
    portENTER_CRITICAL(&s_state_lock);
    *out = s_state;
    portEXIT_CRITICAL(&s_state_lock);
}

/* ==========================================================================
 * Matter attribute plumbing
 * ========================================================================== */

/* Attribute writes must happen on the Matter thread, not on ours.
 *
 * CHIP_CONFIG_LAMBDA_EVENT_SIZE caps a scheduled lambda's captures at 24 bytes,
 * so each reporter captures only an endpoint id and a primitive and builds the
 * esp_matter_attr_val_t inside the lambda. A generic "schedule any attribute"
 * helper would have to capture the whole value struct and does not fit.
 *
 * Note the nullable<> wrappers: MeasuredValue is nullable on all three of
 * Temperature, RelativeHumidity and Illuminance. Writing a plain int16/uint16
 * is rejected with ESP_ERR_INVALID_ARG, because the payload is correct but the
 * type tag does not match what the attribute was created with. */

/* Matter carries illuminance logarithmically:
 *   MeasuredValue = 10000 * log10(lux) + 1, with 0 meaning "below 1 lux". */
static uint16_t lux_to_matter(float lux)
{
    if (lux < 1.0f) {
        return 0;
    }
    double encoded = 10000.0 * log10((double)lux) + 1.0;
    if (encoded > 65533.0) {
        encoded = 65533.0;
    }
    return (uint16_t)encoded;
}

static void report_temperature(float celsius)
{
    VerifyOrReturn(s_matter_ready);
    uint16_t endpoint_id = s_endpoint.temperature;
    VerifyOrReturn(endpoint_id != 0);
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, celsius]() {
        int16_t raw_val = (int16_t)(celsius * 100);
        if (raw_val < -4000) raw_val = -4000;
        if (raw_val > 12500) raw_val = 12500;
        nullable<int16_t> measured(raw_val);
        esp_matter_attr_val_t val = esp_matter_nullable_int16(measured);
        attribute::update(endpoint_id, TemperatureMeasurement::Id,
                          TemperatureMeasurement::Attributes::MeasuredValue::Id, &val);

        auto *iface = esp_matter::data_model::provider::get_instance().registry().Get(
            chip::app::ConcreteClusterPath(endpoint_id, TemperatureMeasurement::Id));
        if (iface) {
            auto *cluster = static_cast<chip::app::Clusters::TemperatureMeasurementCluster *>(iface);
            if (cluster->GetMinMeasuredValue().IsNull()) {
                LogErrorOnFailure(cluster->SetMeasuredValueRange(chip::app::DataModel::MakeNullable((int16_t)-4000),
                                                                 chip::app::DataModel::MakeNullable((int16_t)12500)));
            }
            LogErrorOnFailure(cluster->SetMeasuredValue(chip::app::DataModel::MakeNullable(raw_val)));
        }
    });
}

static void report_humidity(float percent)
{
    VerifyOrReturn(s_matter_ready);
    uint16_t endpoint_id = s_endpoint.humidity;
    VerifyOrReturn(endpoint_id != 0);
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, percent]() {
        float p = percent;
        if (p < 0.0f) p = 0.0f;
        if (p > 100.0f) p = 100.0f;
        uint16_t raw_val = (uint16_t)(p * 100);
        nullable<uint16_t> measured(raw_val);
        esp_matter_attr_val_t val = esp_matter_nullable_uint16(measured);
        attribute::update(endpoint_id, RelativeHumidityMeasurement::Id,
                          RelativeHumidityMeasurement::Attributes::MeasuredValue::Id, &val);

        auto *iface = esp_matter::data_model::provider::get_instance().registry().Get(
            chip::app::ConcreteClusterPath(endpoint_id, RelativeHumidityMeasurement::Id));
        if (iface) {
            auto *cluster = static_cast<chip::app::Clusters::RelativeHumidityMeasurementCluster *>(iface);
            LogErrorOnFailure(cluster->SetMeasuredValue(chip::app::DataModel::MakeNullable(raw_val)));
        }
    });
}

static void report_illuminance(float lux)
{
    VerifyOrReturn(s_matter_ready);
    uint16_t endpoint_id = s_endpoint.illuminance;
    VerifyOrReturn(endpoint_id != 0);
    uint16_t encoded = lux_to_matter(lux);
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, encoded]() {
        nullable<uint16_t> measured(encoded);
        esp_matter_attr_val_t val = esp_matter_nullable_uint16(measured);
        attribute::update(endpoint_id, IlluminanceMeasurement::Id,
                          IlluminanceMeasurement::Attributes::MeasuredValue::Id, &val);

        auto *iface = esp_matter::data_model::provider::get_instance().registry().Get(
            chip::app::ConcreteClusterPath(endpoint_id, IlluminanceMeasurement::Id));
        if (iface) {
            auto *cluster = static_cast<chip::app::Clusters::IlluminanceMeasurementCluster *>(iface);
            if (cluster->GetMinMeasuredValue().IsNull()) {
                LogErrorOnFailure(cluster->SetMeasuredValueRange(chip::app::DataModel::MakeNullable((uint16_t)1),
                                                                 chip::app::DataModel::MakeNullable((uint16_t)65533)));
            }
            LogErrorOnFailure(cluster->SetMeasuredValue(chip::app::DataModel::MakeNullable(encoded)));
        }
    });
}

static AirQuality::AirQualityEnum co2_to_air_quality(uint16_t ppm)
{
    if (ppm == 0) {
        return AirQuality::AirQualityEnum::kUnknown;
    } else if (ppm < 800) {
        return AirQuality::AirQualityEnum::kGood;
    } else if (ppm < 1000) {
        return AirQuality::AirQualityEnum::kFair;
    } else if (ppm < 1500) {
        return AirQuality::AirQualityEnum::kModerate;
    } else if (ppm < 2000) {
        return AirQuality::AirQualityEnum::kPoor;
    } else if (ppm < 3000) {
        return AirQuality::AirQualityEnum::kVeryPoor;
    } else {
        return AirQuality::AirQualityEnum::kExtremelyPoor;
    }
}

static void report_co2(uint16_t ppm)
{
    VerifyOrReturn(s_matter_ready);
    uint16_t endpoint_id = s_endpoint.air_quality;
    VerifyOrReturn(endpoint_id != 0);
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, ppm]() {
        nullable<float> concentration((float)ppm);
        esp_matter_attr_val_t val = esp_matter_nullable_float(concentration);
        attribute::update(endpoint_id, CarbonDioxideConcentrationMeasurement::Id,
                          CarbonDioxideConcentrationMeasurement::Attributes::MeasuredValue::Id, &val);

        auto *iface = esp_matter::data_model::provider::get_instance().registry().Get(
            chip::app::ConcreteClusterPath(endpoint_id, AirQuality::Id));
        if (iface) {
            auto *aq_cluster = static_cast<chip::app::Clusters::AirQualityCluster *>(iface);
            aq_cluster->SetAirQuality(co2_to_air_quality(ppm));
        }
    });
}

static void report_occupancy(bool occupied)
{
    VerifyOrReturn(s_matter_ready);
    uint16_t endpoint_id = s_endpoint.occupancy;
    VerifyOrReturn(endpoint_id != 0);
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, occupied]() {
        /* Occupancy is a bitmap; bit 0 is the occupied flag. */
        esp_matter_attr_val_t val = esp_matter_bitmap8(occupied ? 0x01 : 0x00);
        attribute::update(endpoint_id, OccupancySensing::Id,
                          OccupancySensing::Attributes::Occupancy::Id, &val);

        auto *iface = esp_matter::data_model::provider::get_instance().registry().Get(
            chip::app::ConcreteClusterPath(endpoint_id, OccupancySensing::Id));
        if (iface) {
            auto *cluster = static_cast<chip::app::Clusters::OccupancySensingCluster *>(iface);
            cluster->SetOccupancy(occupied);
        }
    });
}

#if CONFIG_OMNI_BATTERY_PRESENT
static void report_battery(uint16_t mv, uint8_t pct)
{
    VerifyOrReturn(s_matter_ready);
    /* Power Source lives on the root node (endpoint 0). BatPercentRemaining is
     * in half-percent units, so a full cell is 200, not 100. */
    chip::DeviceLayer::SystemLayer().ScheduleLambda([mv]() {
        esp_matter_attr_val_t val = esp_matter_uint32(mv);
        attribute::update(0, PowerSource::Id, PowerSource::Attributes::BatVoltage::Id, &val);
    });
    chip::DeviceLayer::SystemLayer().ScheduleLambda([pct]() {
        esp_matter_attr_val_t val = esp_matter_uint8((uint8_t)(pct * 2));
        attribute::update(0, PowerSource::Id, PowerSource::Attributes::BatPercentRemaining::Id, &val);
    });
}
#endif

/* Push everything currently known. Used once, when reporting opens. */
static void publish_current_state(void)
{
    omni_state_t st;
    omni_get_state(&st);
    if (st.env_valid) {
        report_temperature(st.temp_c);
        report_humidity(st.humidity_pct);
        report_illuminance(st.lux);
        if (st.co2_ppm > 0) {
            report_co2(st.co2_ppm);
        } else {
            report_co2(400);
        }
    } else {
        /* Provide initial non-null defaults so controllers (e.g. Home Assistant)
         * immediately discover all sensor entities rather than seeing Null. */
        report_temperature(20.0f);
        report_humidity(50.0f);
        report_illuminance(1.0f);
        report_co2(400);
    }
    report_occupancy(st.occupied);
}

/* Presence should reach the controller immediately, not at the next poll.
 * Tell the ICD manager there is activity so it enters active mode. */
static void notify_network_activity(void)
{
    VerifyOrReturn(s_matter_ready);
#if CONFIG_ENABLE_ICD_SERVER
    LogErrorOnFailure(chip::DeviceLayer::PlatformMgr().ScheduleWork([](intptr_t) {
        chip::app::ICDNotifier::GetInstance().NotifyNetworkActivityNotification();
    }));
#endif
}

/* ==========================================================================
 * State owner — the single writer
 * ========================================================================== */
static void state_owner_task(void *arg)
{
    omni_evt_t evt;
    for (;;) {
        if (xQueueReceive(s_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (evt.type) {
        case OMNI_EVT_ENV: {
            portENTER_CRITICAL(&s_state_lock);
            if (evt.env.temp_valid)     s_state.temp_c       = evt.env.temp_c;
            if (evt.env.humidity_valid) s_state.humidity_pct = evt.env.humidity_pct;
            if (evt.env.lux_valid)      s_state.lux          = evt.env.lux;
            if (evt.env.co2_valid)      s_state.co2_ppm      = evt.env.co2_ppm;
            s_state.env_valid = true;
            portEXIT_CRITICAL(&s_state_lock);

            if (evt.env.temp_valid)     report_temperature(evt.env.temp_c);
            if (evt.env.humidity_valid) report_humidity(evt.env.humidity_pct);
            if (evt.env.lux_valid)      report_illuminance(evt.env.lux);
            if (evt.env.co2_valid)      report_co2(evt.env.co2_ppm);

            /* Log the state, not the event: CO2 is only measured every few
             * minutes, and printing the event made a deliberately skipped
             * reading look like a failed one. */
            omni_state_t now;
            omni_get_state(&now);
            ESP_LOGI(TAG, "env: %.1f C  %.1f %%RH  %u ppm%s  %.0f lux",
                     now.temp_c, now.humidity_pct, now.co2_ppm,
                     evt.env.co2_valid ? "" : " (held)", now.lux);
            break;
        }

        case OMNI_EVT_PRESENCE: {
            bool occupancy_changed;
            portENTER_CRITICAL(&s_state_lock);
            occupancy_changed = (s_state.occupied != evt.presence.occupied);
            s_state.occupied  = evt.presence.occupied;
            if (evt.presence.zone_valid) {
                s_state.zone = evt.presence.zone;
            }
            portEXIT_CRITICAL(&s_state_lock);

            if (occupancy_changed) {
                report_occupancy(evt.presence.occupied);
                notify_network_activity();
                ESP_LOGI(TAG, "occupancy -> %s",
                         evt.presence.occupied ? "OCCUPIED" : "CLEAR");
            }
            break;
        }

        case OMNI_EVT_BATTERY: {
            portENTER_CRITICAL(&s_state_lock);
            s_state.batt_mv  = evt.battery.mv;
            s_state.batt_pct = evt.battery.pct;
            portEXIT_CRITICAL(&s_state_lock);
#if CONFIG_OMNI_BATTERY_PRESENT
            report_battery(evt.battery.mv, evt.battery.pct);
#endif
            break;
        }

        case OMNI_EVT_UPDATING: {
            portENTER_CRITICAL(&s_state_lock);
            s_state.updating = evt.updating.in_progress;
            portEXIT_CRITICAL(&s_state_lock);
            break;
        }
        }
    }
}

/* ==========================================================================
 * Matter callbacks
 * ========================================================================== */
static void open_commissioning_window_if_necessary()
{
    VerifyOrReturn(chip::Server::GetInstance().GetFabricTable().FabricCount() == 0);

    chip::CommissioningWindowManager &commission_mgr =
        chip::Server::GetInstance().GetCommissioningWindowManager();
    VerifyOrReturn(commission_mgr.IsCommissioningWindowOpen() == false);

    CHIP_ERROR err = commission_mgr.OpenBasicCommissioningWindow(
        chip::System::Clock::Seconds16(300), chip::CommissioningWindowAdvertisement::kAllSupported);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "Failed to open commissioning window: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        omni_stay_awake(false);
        break;
    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        ESP_LOGI(TAG, "Fabric removed");
        open_commissioning_window_if_necessary();
        break;
    case chip::DeviceLayer::DeviceEventType::kThreadConnectivityChange:
        ESP_LOGI(TAG, "Thread connectivity changed");
        break;
    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id,
                                       uint8_t effect_id, uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identify: type %u, effect %u", type, effect_id);
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id,
                                         uint32_t cluster_id, uint32_t attribute_id,
                                         esp_matter_attr_val_t *val, void *priv_data)
{
    /* Nothing here is writable from the network — this is a sensor. */
    return ESP_OK;
}

/* ==========================================================================
 * Button — short press refreshes, long press factory resets
 * ========================================================================== */
static void button_single_click_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Button: refresh requested");
    omni_sensor_request_refresh();
    notify_network_activity();
}

static esp_err_t button_init(void)
{
    button_handle_t handle = NULL;
    const button_config_t btn_cfg = {0};
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num      = OMNI_PIN_BUTTON,
        .active_level  = 0,      /* pulled up, pressed pulls to ground */
        .enable_power_save = true,
    };

    esp_err_t err = iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create button device: %s", esp_err_to_name(err));
        return err;
    }

    iot_button_register_cb(handle, BUTTON_SINGLE_CLICK, NULL, button_single_click_cb, NULL);
    /* Long press clears the fabric credentials — Matter mandates a physical
     * factory reset path. */
    return app_reset_button_register(handle);
}

/* ==========================================================================
 * app_main
 * ========================================================================== */
extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    nvs_flash_init();

    s_queue = xQueueCreate(16, sizeof(omni_evt_t));
    ABORT_APP_ON_FAILURE(s_queue != nullptr, ESP_LOGE(TAG, "Failed to create event queue"));

    /* Hardware first, and in this order: the rail is forced OFF inside
     * omni_power_init before anything else can turn it on. */
    ABORT_APP_ON_FAILURE(omni_i2c_init() == ESP_OK, ESP_LOGE(TAG, "I2C init failed"));
    ABORT_APP_ON_FAILURE(omni_power_init() == ESP_OK, ESP_LOGE(TAG, "Power init failed"));

    /* Display before sensors and before the radio: whoever walks in sees the
     * retained readings immediately instead of a blank panel. */
    if (omni_display_start() != ESP_OK) {
        ESP_LOGW(TAG, "OLED unavailable — continuing headless");
    }

#if CONFIG_OMNI_BATTERY_PRESENT
    /* Sample the battery BEFORE the radio comes up. RF activity couples noise
     * onto the ADC; sampling first sidesteps it instead of filtering it after
     * the fact (project_description.md §7). */
    {
        uint16_t mv = 0;
        uint8_t pct = 0;
        if (omni_battery_sample(&mv, &pct) == ESP_OK) {
            omni_evt_t evt = {};
            evt.type = OMNI_EVT_BATTERY;
            evt.battery.mv = mv;
            evt.battery.pct = pct;
            omni_post_event(&evt);
        }
    }
#else
    ESP_LOGI(TAG, "Battery hardware not fitted — running on USB, battery reporting disabled");
#endif

    err = button_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Button unavailable — no physical factory reset");
    }

    /* ---- Matter data model ---- */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    /* MinMeasuredValue and MaxMeasuredValue are mandatory and default to null.
     * Furthermore, Home Assistant skips discovery of sensor entities whose
     * primary MeasuredValue attribute is null at discovery time.
     * Provide datasheet ranges and non-null initial values so controllers
     * reliably discover every sensor endpoint immediately. */
    temperature_sensor::config_t temperature_config;
    temperature_config.temperature_measurement.min_measured_value =
        nullable<int16_t>(-4000);   /* SHT40: -40.00 C */
    temperature_config.temperature_measurement.max_measured_value =
        nullable<int16_t>(12500);   /* SHT40: +125.00 C */
    temperature_config.temperature_measurement.measured_value =
        nullable<int16_t>(2000);    /* Initial 20.00 C until sensor reports */
    endpoint_t *temperature_ep = temperature_sensor::create(node, &temperature_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(temperature_ep != nullptr, ESP_LOGE(TAG, "Failed to create temperature endpoint"));
    s_endpoint.temperature = endpoint::get_id(temperature_ep);

    humidity_sensor::config_t humidity_config;
    humidity_config.relative_humidity_measurement.min_measured_value =
        nullable<uint16_t>(0);      /* 0.00 %RH */
    humidity_config.relative_humidity_measurement.max_measured_value =
        nullable<uint16_t>(10000);  /* 100.00 %RH */
    humidity_config.relative_humidity_measurement.measured_value =
        nullable<uint16_t>(5000);   /* Initial 50.00 %RH until sensor reports */
    endpoint_t *humidity_ep = humidity_sensor::create(node, &humidity_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(humidity_ep != nullptr, ESP_LOGE(TAG, "Failed to create humidity endpoint"));
    s_endpoint.humidity = endpoint::get_id(humidity_ep);

    light_sensor::config_t illuminance_config;
    /* Log encoded, same as MeasuredValue: 1 lux -> 1, 65535 lux -> 48165. */
    illuminance_config.illuminance_measurement.min_measured_value = nullable<uint16_t>(1);
    illuminance_config.illuminance_measurement.max_measured_value = nullable<uint16_t>(65533);
    illuminance_config.illuminance_measurement.measured_value = nullable<uint16_t>(1); /* Initial 1 lux */
    endpoint_t *illuminance_ep = light_sensor::create(node, &illuminance_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(illuminance_ep != nullptr, ESP_LOGE(TAG, "Failed to create illuminance endpoint"));
    s_endpoint.illuminance = endpoint::get_id(illuminance_ep);

    /* CO2 has no standalone device type: it is a concentration measurement
     * cluster hung off an Air Quality Sensor endpoint. */
    air_quality_sensor::config_t air_quality_config;
    air_quality_config.air_quality.air_quality =
        chip::to_underlying(AirQuality::AirQualityEnum::kGood);
    endpoint_t *air_quality_ep = air_quality_sensor::create(node, &air_quality_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(air_quality_ep != nullptr, ESP_LOGE(TAG, "Failed to create air quality endpoint"));
    s_endpoint.air_quality = endpoint::get_id(air_quality_ep);
    {
        /* Enable feature bits for Fair (0x01), Moderate (0x02), Very Poor (0x04), Extremely Poor (0x08) */
        cluster_t *aq_cluster = cluster::get(air_quality_ep, AirQuality::Id);
        if (aq_cluster) {
            attribute_t *feat = attribute::get(aq_cluster, chip::app::Clusters::Globals::Attributes::FeatureMap::Id);
            if (feat) {
                esp_matter_attr_val_t feat_val = esp_matter_bitmap32(0x0F);
                attribute::set_val(feat, &feat_val, false);
            }
        }
    }
    {
        cluster::carbon_dioxide_concentration_measurement::config_t co2_config;
        co2_config.feature_flags =
            cluster::concentration_measurement::feature::numeric_measurement::get_id();
        /* Measurement medium 0 = Air. */
        co2_config.measurement_medium = 0;
        co2_config.features.numeric_measurement.measurement_unit = 1;  /* PPM */
        co2_config.features.numeric_measurement.min_measured_value = nullable<float>(400.0f);
        co2_config.features.numeric_measurement.max_measured_value = nullable<float>(5000.0f);
        co2_config.features.numeric_measurement.measured_value = nullable<float>(400.0f);
        cluster_t *co2_cluster = cluster::carbon_dioxide_concentration_measurement::create(
            air_quality_ep, &co2_config, CLUSTER_FLAG_SERVER);
        ABORT_APP_ON_FAILURE(co2_cluster != nullptr, ESP_LOGE(TAG, "Failed to create CO2 cluster"));
    }

    occupancy_sensor::config_t occupancy_config;
    /* The cluster refuses to be created without at least one sensing feature.
     * Occupancy is driven by the SR602 PIR. */
    occupancy_config.occupancy_sensing.feature_flags =
        cluster::occupancy_sensing::feature::passive_infrared::get_id();
    occupancy_config.occupancy_sensing.occupancy_sensor_type =
        chip::to_underlying(OccupancySensing::OccupancySensorTypeEnum::kPir);
    occupancy_config.occupancy_sensing.occupancy_sensor_type_bitmap =
        chip::to_underlying(OccupancySensing::OccupancySensorTypeBitmap::kPir);
    endpoint_t *occupancy_ep = occupancy_sensor::create(node, &occupancy_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(occupancy_ep != nullptr, ESP_LOGE(TAG, "Failed to create occupancy endpoint"));
    s_endpoint.occupancy = endpoint::get_id(occupancy_ep);

#if CONFIG_OMNI_BATTERY_PRESENT
    /* Power Source on the root node, so controllers show battery state for the
     * device as a whole rather than for one sensor endpoint. */
    {
        endpoint_t *root = endpoint::get(node, 0);
        cluster::power_source::config_t ps_config;
        ps_config.feature_flags = cluster::power_source::feature::battery::get_id();
        cluster_t *ps_cluster = cluster::power_source::create(root, &ps_config, CLUSTER_FLAG_SERVER);
        if (ps_cluster == nullptr) {
            ESP_LOGW(TAG, "Failed to create Power Source cluster");
        }
    }
#endif

    ESP_LOGI(TAG, "endpoints: temp %u, hum %u, lux %u, air %u, occupancy %u",
             s_endpoint.temperature, s_endpoint.humidity, s_endpoint.illuminance,
             s_endpoint.air_quality, s_endpoint.occupancy);

    /* ---- Application tasks (all below the Matter stack priority) ----
     * Started before the radio, deliberately. The SCD41 stopped acknowledging
     * its address once Thread and BLE were up, and initialising it while the
     * radio is still quiet is the difference between a working sensor and one
     * that needs unplugging. Reporting is gated on s_matter_ready, so an event
     * arriving before the stack exists is stored, not pushed. */
    ABORT_APP_ON_FAILURE(xTaskCreate(state_owner_task, "omni_state", 4096, NULL,
                                     OMNI_PRIO_STATE_OWNER, NULL) == pdPASS,
                         ESP_LOGE(TAG, "Failed to start state owner task"));

    if (omni_sensor_task_start() != ESP_OK) {
        ESP_LOGE(TAG, "Sensor task failed to start");
    }
    if (omni_presence_start() != ESP_OK) {
        ESP_LOGE(TAG, "Presence task failed to start");
    }
#if CONFIG_OMNI_BATTERY_PRESENT
    omni_battery_task_start();
#endif

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    esp_openthread_platform_config_t thread_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config  = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config  = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&thread_config);
#endif

    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter: %d", err));

    /* Hold CPU and radio awake while uncommissioned so light sleep does not disrupt BLE / Thread discovery. */
    if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
        ESP_LOGI(TAG, "Uncommissioned — holding wake lock during commissioning window");
        omni_stay_awake(true);
    }

    /* Reporting opens only now. Anything the tasks measured while the stack was
     * coming up is already in shared state, so publish that snapshot rather
     * than making a controller wait for the next measurement cycle. */
    s_matter_ready = true;
    publish_current_state();

    ESP_LOGI(TAG, "OmniSensor hub running");
}
