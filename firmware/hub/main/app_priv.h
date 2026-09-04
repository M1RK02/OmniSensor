/*
 * OmniSensor hub — shared application contract.
 *
 * Concurrency model (project_description.md §8): every producer posts an event
 * onto omni_event_queue(); exactly ONE task (the state owner in app_main.cpp)
 * writes the shared state and pushes updates to Matter and the display. Nothing
 * else writes g_state. That removes the deadlock question rather than managing it.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * Pin map — Seeed XIAO ESP32-C6.
 * Verified against the Seeed wiki; closes the TBDs in project_description.md §5.
 * Reserved by the module, never use: GPIO14 (RF switch select),
 * GPIO3 (RF switch enable, must stay LOW). Strapping pins avoided: 4,5,8,9,15.
 * ========================================================================== */
#define OMNI_PIN_I2C_SDA        GPIO_NUM_22  /* D4 */
#define OMNI_PIN_I2C_SCL        GPIO_NUM_23  /* D5 */
#define OMNI_PIN_PIR            GPIO_NUM_2   /* D2, LP_GPIO2, wake-capable, active HIGH */
#define OMNI_PIN_BATT_SENSE     GPIO_NUM_0   /* D0/A0, ADC1_CH0, 2x100k divider */
#define OMNI_PIN_BUTTON         GPIO_NUM_9   /* Built-in BOOT button (LP_GPIO9), active LOW */
#define OMNI_PIN_STATUS_LED     GPIO_NUM_15  /* on-board user LED */
#define OMNI_PIN_RAIL_EN        GPIO_NUM_21  /* D3, active LOW P-MOSFET gate (SI2301 radar switch) */
#define OMNI_PIN_RADAR_TX       16           /* D6, UART0 TX -> LD2420 RX */
#define OMNI_PIN_RADAR_RX       17           /* D7, UART0 RX <- LD2420 TX */
#define OMNI_RADAR_UART_PORT    UART_NUM_0
#define OMNI_PIN_RF_SWITCH_EN   GPIO_NUM_3   /* RF switch enable, active LOW (Seeed XIAO module internal) */
#define OMNI_PIN_RF_ANT_SEL     GPIO_NUM_14  /* Antenna select: 0 = internal ceramic, 1 = external U.FL */

/* I2C device addresses (verified conflict-free in Milestone 1) */
#define OMNI_ADDR_BH1750        0x23
#define OMNI_ADDR_SSD1315       0x3C
#define OMNI_ADDR_SHT40         0x44
#define OMNI_ADDR_SCD41         0x62

/* ==========================================================================
 * Timing policy
 * ========================================================================== */
/* The SCD41 single shot costs ~5 s of sensor-rail time. Bound how often a
 * presence wake can pay for it — closes project_description.md §17.6. */
#define OMNI_SCD41_MIN_INTERVAL_MS   (120 * 1000)
/* Environmental refresh while the device is awake and active. */
#define OMNI_ENV_INTERVAL_MS         (60 * 1000)
/* Battery is slow-moving; sample rarely and always before radio activity. */
#define OMNI_BATT_INTERVAL_MS        (10 * 60 * 1000)
/* No presence for this long -> back to idle. */
#define OMNI_INACTIVITY_TIMEOUT_MS   (30 * 1000)

/* ==========================================================================
 * Task priorities — all strictly below the Matter/OpenThread stack.
 * The 5 s SCD41 wait must never sit at or above network priority or it starves
 * the stack and trips the Task Watchdog (project_description.md §8).
 * ========================================================================== */
#define OMNI_PRIO_STATE_OWNER    4
#define OMNI_PRIO_SENSOR         3
#define OMNI_PRIO_PRESENCE       3
#define OMNI_PRIO_UI             3
#define OMNI_PRIO_BATTERY        2

/* ==========================================================================
 * Shared state
 * ========================================================================== */
typedef enum {
    OMNI_ZONE_CLEAR = 0,   /* nobody detected */
    OMNI_ZONE_1     = 1,   /* < 0.7 m  — at the device */
    OMNI_ZONE_2     = 2,   /* 0.7-1.4 m — near field */
    OMNI_ZONE_3     = 3,   /* > 1.4 m  — room presence */
} omni_zone_t;

typedef struct {
    float       temp_c;
    float       humidity_pct;
    uint16_t    co2_ppm;
    float       lux;
    omni_zone_t zone;
    bool        occupied;
    uint16_t    batt_mv;
    uint8_t     batt_pct;
    bool        env_valid;      /* at least one successful environmental read */
    bool        updating;       /* a measurement cycle is in flight */
} omni_state_t;

/* ==========================================================================
 * Events — the only way to mutate state
 * ========================================================================== */
typedef enum {
    OMNI_EVT_ENV,        /* new environmental readings */
    OMNI_EVT_PRESENCE,   /* new occupancy from PIR */
    OMNI_EVT_BATTERY,    /* new battery reading */
    OMNI_EVT_UPDATING,   /* measurement cycle started/finished (UI hint) */
} omni_evt_type_t;

typedef struct {
    omni_evt_type_t type;
    union {
        struct {
            float    temp_c;
            float    humidity_pct;
            float    lux;
            uint16_t co2_ppm;
            bool     temp_valid;
            bool     humidity_valid;
            bool     lux_valid;
            bool     co2_valid;
        } env;
        struct {
            omni_zone_t zone;
            bool        occupied;
            /* PIR does not provide a distance zone. */
            bool        zone_valid;
        } presence;
        struct {
            uint16_t mv;
            uint8_t  pct;
        } battery;
        struct {
            bool in_progress;
        } updating;
    };
} omni_evt_t;

/* The shared event queue. Created by app_main before any producer starts. */
QueueHandle_t omni_event_queue(void);

/* Convenience: post an event, never blocking longer than a tick. Safe to call
 * from any task. Returns ESP_ERR_TIMEOUT if the queue is full (logged, dropped —
 * a dropped reading is better than a stalled producer). */
esp_err_t omni_post_event(const omni_evt_t *evt);

/* Read-only snapshot of the current state, for the UI. Copies under a short
 * critical section; never returns a torn struct. */
void omni_get_state(omni_state_t *out);

/* ==========================================================================
 * Module entry points
 * ========================================================================== */
/* Shared I2C bus, created once and handed to every I2C driver. */
esp_err_t omni_i2c_init(void);
i2c_master_bus_handle_t omni_i2c_bus(void);

/* Sensor task: SHT40 + BH1750 + SCD41, sequenced, CRC-validated. */
esp_err_t omni_sensor_task_start(void);
/* Ask the sensor task for an immediate refresh (e.g. on a presence wake). */
void omni_sensor_request_refresh(void);

/* PIR presence task: GPIO2 motion -> occupancy event + wake. */
esp_err_t omni_presence_start(void);

/* Display: SSD1315 on the always-on rail. */
esp_err_t omni_display_start(void);

/* Power management + battery ADC. */
esp_err_t omni_power_init(void);
/* Hold the CPU out of light sleep. Nested calls are counted, so callers pair
 * true/false without coordinating with each other. Needed around any long bus
 * transaction: light sleep isolates GPIOs and stops clocking peripherals, so a
 * transfer spanning a sleep window is cut in half. */
void      omni_stay_awake(bool hold);
/* One multisampled, calibrated battery reading. Call BEFORE starting Matter —
 * RF activity couples noise onto the ADC (project_description.md §7). */
esp_err_t omni_battery_sample(uint16_t *mv_out, uint8_t *pct_out);
esp_err_t omni_battery_task_start(void);

/* Switched radar rail control (SI2301 P-MOSFET on GPIO21 / D3).
 * Active LOW: enable=true drives pin LOW (MOSFET ON).
 * enable=false drives pin HIGH (MOSFET OFF). */
esp_err_t omni_radar_rail_set(bool enable);
esp_err_t omni_radar_power_cycle(void);

#ifdef __cplusplus
}
#endif
