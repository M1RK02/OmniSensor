#include "ssd1315.h"

#include "app_priv.h"
#include "esp_log.h"
#include <string.h>

#define SSD1315_CTRL_CMD   0x00
#define SSD1315_CTRL_DATA  0x40

static const char *TAG = "ssd1315";
static i2c_master_dev_handle_t s_dev;

/* ---------------------------------------------------------------------------
 * Minimal 5x7 font, ASCII 32 (space) through 90 ('Z'), lifted unchanged from
 * firmware/examples/all. Uppercase only — that is all the UI needs, and it
 * keeps the table under 300 bytes.
 * ------------------------------------------------------------------------ */
static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62}, {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1C, 0x00}, {0x14, 0x08, 0x3E, 0x08, 0x14}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x08, 0x14, 0x22, 0x41, 0x00}, {0x14, 0x14, 0x14, 0x14, 0x14}, {0x00, 0x41, 0x22, 0x14, 0x08}, {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E}, {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x09, 0x01}, {0x3E, 0x41, 0x49, 0x49, 0x7A},
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x3F, 0x40, 0x38, 0x40, 0x3F},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}
};

#define FONT_FIRST_CHAR  32
#define FONT_LAST_CHAR   90

static esp_err_t ssd1315_send_cmds(const uint8_t *cmds, size_t len)
{
    /* Caller supplies the 0x00 control byte as cmds[0]. */
    return i2c_master_transmit(s_dev, cmds, len, 1000);
}

static esp_err_t ssd1315_set_cursor(uint8_t page, uint8_t col)
{
    uint8_t cmd[] = {
        SSD1315_CTRL_CMD,
        (uint8_t)(0xB0 | (page & 0x07)),   /* page address */
        (uint8_t)(col & 0x0F),             /* lower column nibble */
        (uint8_t)(0x10 | (col >> 4)),      /* upper column nibble */
    };
    return ssd1315_send_cmds(cmd, sizeof(cmd));
}

esp_err_t ssd1315_init(i2c_master_bus_handle_t bus)
{
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = OMNI_ADDR_SSD1315,
        .scl_speed_hz    = 400000,  /* the OLED tolerates fast mode; the sensors do not */
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    static const uint8_t init_seq[] = {
        SSD1315_CTRL_CMD,
        0xAE,              /* display off */
        0xD5, 0x80,        /* clock divide / oscillator frequency */
        0xA8, 0x3F,        /* multiplex ratio = 64 */
        0xD3, 0x00,        /* display offset = 0 */
        0x40,              /* start line = 0 */
        0x8D, 0x14,        /* charge pump on */
        0x20, 0x00,        /* horizontal addressing mode */
        0xA1,              /* segment remap */
        0xC8,              /* COM scan direction remapped */
        0xDA, 0x12,        /* COM pins configuration */
        0x81, 0xCF,        /* contrast */
        0xD9, 0xF1,        /* pre-charge period */
        0xDB, 0x40,        /* VCOMH deselect level */
        0xA4,              /* resume from RAM content */
        0xA6,              /* normal (not inverted) */
        0xAF,              /* display on */
    };
    err = ssd1315_send_cmds(init_seq, sizeof(init_seq));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init sequence failed: %s", esp_err_to_name(err));
        return err;
    }
    return ssd1315_clear();
}

esp_err_t ssd1315_clear_page(uint8_t page)
{
    uint8_t buffer[1 + SSD1315_COLUMNS];
    buffer[0] = SSD1315_CTRL_DATA;
    memset(&buffer[1], 0, SSD1315_COLUMNS);

    esp_err_t err = ssd1315_set_cursor(page, 0);
    if (err != ESP_OK) {
        return err;
    }
    return i2c_master_transmit(s_dev, buffer, sizeof(buffer), 1000);
}

esp_err_t ssd1315_clear(void)
{
    for (uint8_t page = 0; page < SSD1315_PAGES; page++) {
        esp_err_t err = ssd1315_clear_page(page);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t ssd1315_write_string(const char *str, uint8_t page, uint8_t col)
{
    if (s_dev == NULL || str == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ssd1315_set_cursor(page, col);
    if (err != ESP_OK) {
        return err;
    }

    /* Send the whole line in one transaction rather than one per glyph: at
     * 400 kHz that is the difference between a snappy repaint and a visible wipe. */
    uint8_t buffer[1 + SSD1315_COLUMNS];
    size_t len = 1;
    buffer[0] = SSD1315_CTRL_DATA;

    for (const char *p = str; *p != '\0' && len + 5 <= sizeof(buffer); p++) {
        char c = *p;
        if (c < FONT_FIRST_CHAR || c > FONT_LAST_CHAR) {
            c = ' ';
        }
        memcpy(&buffer[len], font5x7[c - FONT_FIRST_CHAR], 5);
        len += 5;
        if (len < sizeof(buffer)) {
            buffer[len++] = 0x00;  /* inter-character spacing */
        }
    }

    return i2c_master_transmit(s_dev, buffer, len, 1000);
}

esp_err_t ssd1315_sleep(void)
{
    /* Display off AND charge pump off — the charge pump is the expensive half. */
    static const uint8_t seq[] = {
        SSD1315_CTRL_CMD,
        0xAE,        /* display off */
        0x8D, 0x10,  /* charge pump disable */
    };
    return ssd1315_send_cmds(seq, sizeof(seq));
}

esp_err_t ssd1315_wake(void)
{
    static const uint8_t seq[] = {
        SSD1315_CTRL_CMD,
        0x8D, 0x14,  /* charge pump enable */
        0xAF,        /* display on */
    };
    return ssd1315_send_cmds(seq, sizeof(seq));
}
