/* Solomon Systech SSD1315 — 128x64 monochrome OLED over I2C (0x3C).
 * SSD1306-compatible command set, driven directly with a 5x7 bitmap font.
 *
 * The display sits on the ALWAYS-ON rail so it can be repainted from retained
 * state before the sensors have finished warming up (project_description.md §6).
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#define SSD1315_PAGES      8   /* 8 pages of 8 pixel rows = 64 rows */
#define SSD1315_COLUMNS    128
#define SSD1315_CHAR_WIDTH 6   /* 5 pixel glyph + 1 pixel spacing */
#define SSD1315_MAX_CHARS  (SSD1315_COLUMNS / SSD1315_CHAR_WIDTH) /* 21 */

esp_err_t ssd1315_init(i2c_master_bus_handle_t bus);
esp_err_t ssd1315_clear(void);

/* Draw text at a page (0-7) and pixel column. Characters outside the font's
 * range (ASCII 32-90) render as spaces. */
esp_err_t ssd1315_write_string(const char *str, uint8_t page, uint8_t col);

/* Blank a whole page — cheaper than clearing the screen when updating one line. */
esp_err_t ssd1315_clear_page(uint8_t page);

/* Display off plus charge-pump shutdown. Worth the extra command on battery. */
esp_err_t ssd1315_sleep(void);
esp_err_t ssd1315_wake(void);
