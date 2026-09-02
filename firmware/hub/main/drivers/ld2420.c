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
static char   s_accumulator[LD2420_LINE_MAX];
static size_t s_acc_len;
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
        /* Receive only. In text mode the module streams unprompted and we never
         * send it a command, so leaving TX unassigned means we can never inject
         * anything into it. This matters because GPIO16 is U0TXD by IO_MUX
         * default: anything UART0 emits, including during boot, lands on the
         * radar's RX and can leave it in a state that only a power cycle
         * clears. Park the pin high instead, which is the UART idle level. */
        err = uart_set_pin(LD2420_UART_PORT, UART_PIN_NO_CHANGE, OMNI_PIN_RADAR_RX,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        gpio_config_t tx_idle = {
            .intr_type    = GPIO_INTR_DISABLE,
            .mode         = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << OMNI_PIN_RADAR_TX,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
        };
        gpio_config(&tx_idle);
        gpio_set_level(OMNI_PIN_RADAR_TX, 1);
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
    s_acc_len = 0;
    s_unparsed_logged = 0;
    s_installed = true;
    ESP_LOGI(TAG, "radar UART up on UART%d (RX %d, TX %d parked idle)",
             LD2420_UART_PORT, OMNI_PIN_RADAR_RX, OMNI_PIN_RADAR_TX);
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

/* Handle one complete line. Returns true if it carried a distance. */
static bool handle_line(const char *line, int *distance_cm, bool *target_present)
{
    /* The module interleaves presence lines with range lines:
     *     ON
     *     Range 88
     *     OFF
     * ON and OFF are not parse failures, they are the presence flag. */
    if (strcmp(line, "ON") == 0) {
        if (target_present) *target_present = true;
        return false;
    }
    if (strcmp(line, "OFF") == 0) {
        if (target_present) *target_present = false;
        return false;
    }

    const char *range = strstr(line, "Range ");
    int cm = -1;
    if (range != NULL && sscanf(range, "Range %d", &cm) == 1 && cm >= 0) {
        if (distance_cm) *distance_cm = cm;
        if (target_present) *target_present = true;
        return true;
    }

    if (line[0] != '\0' && s_unparsed_logged < LD2420_UNPARSED_LOG_LIMIT) {
        ESP_LOGW(TAG, "unparsed radar line: \"%s\"", line);
        s_unparsed_logged++;
    }
    return false;
}

esp_err_t ld2420_poll_distance_cm(int *distance_cm, uint32_t timeout_ms)
{
    if (!s_installed) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t chunk[LD2420_RX_BUF_SIZE];
    int len = uart_read_bytes(LD2420_UART_PORT, chunk, sizeof(chunk),
                              pdMS_TO_TICKS(timeout_ms));
    if (len < 0) {
        ESP_LOGE(TAG, "uart_read_bytes failed: %d", len);
        return ESP_FAIL;
    }

    bool found = false;
    /* Assemble lines a byte at a time. The module terminates with CRLF, and a
     * stray carriage return left in the string wrecks both the comparisons and
     * the log output. Draining as we go also means the buffer cannot overflow
     * just because several lines arrived in one read. */
    for (int i = 0; i < len; i++) {
        char c = (char)chunk[i];

        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            s_accumulator[s_acc_len] = '\0';
            if (handle_line(s_accumulator, distance_cm, NULL)) {
                found = true;  /* keep the most recent range in this batch */
            }
            s_acc_len = 0;
            continue;
        }
        if (s_acc_len < sizeof(s_accumulator) - 1) {
            s_accumulator[s_acc_len++] = c;
        } else {
            /* Longer than any line the module emits: we are out of sync.
             * Drop it and resynchronise on the next newline. */
            s_acc_len = 0;
        }
    }

    return found ? ESP_OK : ESP_ERR_TIMEOUT;
}
