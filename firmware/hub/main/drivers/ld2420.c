#include "ld2420.h"

#include "app_priv.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#define LD2420_UART_PORT   UART_NUM_1
#define LD2420_BAUD_RATE   115200
#define LD2420_RX_BUF_SIZE 512
#define LD2420_LINE_MAX    256

static const char *TAG = "ld2420";
static bool s_installed;
/* Partial line carried between polls: UART reads do not respect line boundaries. */
static char s_accumulator[LD2420_LINE_MAX];
/* Bounded so a permanently unparseable stream cannot flood the log. */
#define LD2420_UNPARSED_LOG_LIMIT 3
static int s_unparsed_logged;

esp_err_t ld2420_init(void)
{
    if (s_installed) {
        return ESP_OK;
    }

    const uart_config_t cfg = {
        .baud_rate  = LD2420_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        /* Not UART_SCLK_DEFAULT: that is PLL_F80M on the C6, which dynamic
         * frequency scaling and light sleep both gate. XTAL keeps the baud rate
         * and the receive timeout honest while power management is enabled. */
        .source_clk = UART_SCLK_XTAL,
    };

    esp_err_t err = uart_driver_install(LD2420_UART_PORT, LD2420_RX_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(LD2420_UART_PORT, &cfg);
    if (err == ESP_OK) {
        err = uart_set_pin(LD2420_UART_PORT, OMNI_PIN_RADAR_TX, OMNI_PIN_RADAR_RX,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart configuration failed: %s", esp_err_to_name(err));
        uart_driver_delete(LD2420_UART_PORT);
        return err;
    }

    /* Light sleep isolates every GPIO on this chip: ESP_SLEEP_GPIO_RESET_WORKAROUND
     * is on by default for the C6 and selects PM_SLP_DISABLE_GPIO, which cannot
     * be overridden from Kconfig. Isolating RX cuts the radar's stream at the
     * pin, so exempt just these two and leave the erratum workaround in place
     * everywhere else. The no-light-sleep lock in power.c covers the other half
     * of the problem: a sleeping UART is not clocked at all. */
    gpio_sleep_sel_dis(OMNI_PIN_RADAR_RX);
    gpio_sleep_sel_dis(OMNI_PIN_RADAR_TX);

    s_accumulator[0] = '\0';
    s_unparsed_logged = 0;
    s_installed = true;
    ESP_LOGI(TAG, "radar UART up on UART%d (TX %d / RX %d)",
             LD2420_UART_PORT, OMNI_PIN_RADAR_TX, OMNI_PIN_RADAR_RX);
    return ESP_OK;
}

esp_err_t ld2420_deinit(void)
{
    if (!s_installed) {
        return ESP_OK;
    }
    esp_err_t err = uart_driver_delete(LD2420_UART_PORT);
    s_installed = false;
    s_accumulator[0] = '\0';
    return err;
}

/* Pull one complete line out of the accumulator, if there is one. */
static bool take_line(char *out, size_t out_len)
{
    char *newline = strchr(s_accumulator, '\n');
    if (newline == NULL) {
        return false;
    }
    *newline = '\0';
    strlcpy(out, s_accumulator, out_len);
    memmove(s_accumulator, newline + 1, strlen(newline + 1) + 1);
    return true;
}

esp_err_t ld2420_poll_distance_cm(int *distance_cm, uint32_t timeout_ms)
{
    if (!s_installed) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t chunk[LD2420_RX_BUF_SIZE];
    int len = uart_read_bytes(LD2420_UART_PORT, chunk, sizeof(chunk) - 1,
                              pdMS_TO_TICKS(timeout_ms));
    if (len < 0) {
        ESP_LOGE(TAG, "uart_read_bytes failed: %d", len);
        return ESP_FAIL;
    }

    if (len > 0) {
        chunk[len] = '\0';
        if (strlen(s_accumulator) + (size_t)len < sizeof(s_accumulator)) {
            strlcat(s_accumulator, (const char *)chunk, sizeof(s_accumulator));
        } else {
            /* A line longer than the buffer means we are out of sync with the
             * stream. Drop what we have and resynchronise on the next newline. */
            ESP_LOGW(TAG, "accumulator overflow, resynchronising");
            s_accumulator[0] = '\0';
        }
    }

    bool found = false;
    int  unparsed = 0;
    char line[LD2420_LINE_MAX];
    /* Drain every complete line; keep the most recent range so a burst of
     * buffered lines reports the latest position, not the oldest. */
    while (take_line(line, sizeof(line))) {
        const char *range = strstr(line, "Range ");
        int cm = -1;
        if (range != NULL && sscanf(range, "Range %d", &cm) == 1 && cm >= 0) {
            if (distance_cm) *distance_cm = cm;
            found = true;
        } else if (s_unparsed_logged < LD2420_UNPARSED_LOG_LIMIT && unparsed++ == 0) {
            /* The parser expects the module's text mode. If it is in binary
             * mode, or streaming some other format, say so with the evidence
             * rather than silently reporting no target forever. */
            ESP_LOGW(TAG, "unparsed radar line: \"%s\"", line);
            s_unparsed_logged++;
        }
    }

    return found ? ESP_OK : ESP_ERR_TIMEOUT;
}
