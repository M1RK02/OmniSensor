/* Local UI on the SSD1315.
 *
 * The display is on the always-on rail, so it can show retained readings the
 * instant someone walks in — before the 5 s CO2 measurement has even started.
 * An explicit UPDATING line makes it obvious when what is on screen is the last
 * known value rather than a live one (project_description.md §11).
 */

#include "app_priv.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ssd1315.h"
#include <stdio.h>
#include <string.h>

#define UI_REFRESH_MS   500
#define UI_LINE_LEN     (SSD1315_MAX_CHARS + 1)
#define UI_LINES        7

/* Font is uppercase-only (ASCII 32-90), so every string here stays uppercase. */
static const char *TAG = "display";
static char s_rendered[UI_LINES][UI_LINE_LEN];

static const char *zone_label(omni_zone_t zone)
{
    switch (zone) {
    case OMNI_ZONE_1: return "1 AT DEVICE";
    case OMNI_ZONE_2: return "2 NEAR";
    case OMNI_ZONE_3: return "3 ROOM";
    default:          return "CLEAR";
    }
}

static void render_line(uint8_t page, const char *text)
{
    if (page >= UI_LINES) {
        return;
    }
    if (strncmp(s_rendered[page], text, UI_LINE_LEN) == 0) {
        return;  /* unchanged — do not spend I2C time repainting it */
    }
    ssd1315_clear_page(page);
    ssd1315_write_string(text, page, 0);
    strlcpy(s_rendered[page], text, UI_LINE_LEN);
}

static void draw(const omni_state_t *st)
{
    char line[UI_LINE_LEN];

    if (st->env_valid) {
        snprintf(line, sizeof(line), "TEMP: %.1f C", st->temp_c);
        render_line(0, line);
        snprintf(line, sizeof(line), "HUM:  %.1f %%", st->humidity_pct);
        render_line(1, line);
        snprintf(line, sizeof(line), "CO2:  %u PPM", st->co2_ppm);
        render_line(2, line);
        snprintf(line, sizeof(line), "LUX:  %.0f", st->lux);
        render_line(3, line);
    } else {
        render_line(0, "OMNISENSOR");
        render_line(1, "STARTING UP...");
        render_line(2, "");
        render_line(3, "");
    }

    snprintf(line, sizeof(line), "ZONE: %s", zone_label(st->zone));
    render_line(4, line);

    if (st->batt_mv > 0) {
        snprintf(line, sizeof(line), "BATT: %u.%02u V %u%%",
                 st->batt_mv / 1000, (st->batt_mv % 1000) / 10, st->batt_pct);
    } else {
        snprintf(line, sizeof(line), "PWR:  USB");
    }
    render_line(5, line);

    render_line(6, st->updating ? "UPDATING..." : (st->occupied ? "OCCUPIED" : ""));
}

static void ui_task(void *arg)
{
    /* Watch this loop explicitly: the idle task cannot be watched on a
     * device that light-sleeps, so these are the loops that must prove
     * they are still turning. */
    esp_task_wdt_add(NULL);

    uint32_t iteration = 0;
    for (;;) {
        esp_task_wdt_reset();

        omni_state_t snapshot;
        omni_get_state(&snapshot);
        draw(&snapshot);

        /* Once a minute, a line the soak test can be judged on: if free heap
         * trends down over hours, something leaks. */
        if ((++iteration % (60000 / UI_REFRESH_MS)) == 0) {
            ESP_LOGI(TAG, "health: up %lld s, heap %lu free / %lu min",
                     esp_timer_get_time() / 1000000,
                     (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)esp_get_minimum_free_heap_size());
        }

        vTaskDelay(pdMS_TO_TICKS(UI_REFRESH_MS));
    }
}

esp_err_t omni_display_start(void)
{
    i2c_master_bus_handle_t bus = omni_i2c_bus();
    if (bus == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ssd1315_init(bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED init failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Paint whatever state we already hold before the task even starts, so the
     * first frame is not a blank screen. */
    memset(s_rendered, 0, sizeof(s_rendered));
    omni_state_t snapshot;
    omni_get_state(&snapshot);
    draw(&snapshot);

    BaseType_t ok = xTaskCreate(ui_task, "omni_ui", 3072, NULL, OMNI_PRIO_UI, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
