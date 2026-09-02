#include "scd41.h"

#include "app_priv.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sensirion_common.h"

#define SCD41_CMD_MEASURE_SINGLE_SHOT  0x219D  /* ~5000 ms */
#define SCD41_CMD_READ_MEASUREMENT     0xEC05
#define SCD41_CMD_POWER_DOWN           0x36E0
#define SCD41_CMD_WAKE_UP              0x36F6  /* not acknowledged by the sensor */
#define SCD41_CMD_GET_DATA_READY       0xE4B8
#define SCD41_CMD_GET_SERIAL_NUMBER    0x3682
#define SCD41_CMD_STOP_PERIODIC        0x3F86  /* 500 ms execution time */
#define SCD41_CMD_REINIT               0x3646  /* 30 ms, requires stop first */

/* The datasheet gives 5000 ms as the *maximum* single-shot duration, so waiting
 * exactly that long leaves no margin and the sensor NACKs the read. */
#define SCD41_SINGLE_SHOT_DELAY_MS     5200
#define SCD41_WAKE_UP_DELAY_MS         30
#define SCD41_STOP_PERIODIC_DELAY_MS   500
#define SCD41_REINIT_DELAY_MS          30
#define SCD41_WAKE_RETRY_MS            50
/* read_measurement needs a command execution time before the data is clocked
 * out. Reading immediately gets a NACK. */
#define SCD41_READ_CMD_DELAY_MS        5
/* Rather than trusting a fixed delay, poll the sensor's own ready flag. */
#define SCD41_READY_POLL_INTERVAL_MS   100
#define SCD41_READY_POLL_ATTEMPTS      20

static const char *TAG = "scd41";
static i2c_master_dev_handle_t  s_dev;
static i2c_master_bus_handle_t  s_bus;

/* Defined below, used by scd41_init. */
esp_err_t scd41_wake_up(void);

static esp_err_t scd41_send_cmd(uint16_t cmd);

static esp_err_t scd41_send_cmd(uint16_t cmd)
{
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    esp_err_t err = i2c_master_transmit(s_dev, buf, sizeof(buf), 1000);
    if (err != ESP_OK && s_bus != NULL) {
        /* A NACK leaves the master's state machine in an error state, and every
         * subsequent transfer then fails with ESP_ERR_INVALID_STATE regardless
         * of what the sensor is doing. One wedge early in init was enough to
         * kill CO2 for the whole session. Clear it here, once, for every path. */
        i2c_master_bus_reset(s_bus);
    }
    return err;
}

/* Read a command's response word(s), validating the CRC on each. */
static esp_err_t scd41_read_words(uint16_t cmd, uint16_t *words, size_t count, uint32_t delay_ms)
{
    esp_err_t err = scd41_send_cmd(cmd);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    uint8_t buf[9];
    if (count * 3 > sizeof(buf)) {
        return ESP_ERR_INVALID_ARG;
    }
    err = i2c_master_receive(s_dev, buf, count * 3, 1000);
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < count; i++) {
        const uint8_t *p = buf + i * 3;
        if (sensirion_crc8(p, 2) != p[2]) {
            return ESP_ERR_INVALID_CRC;
        }
        words[i] = ((uint16_t)p[0] << 8) | p[1];
    }
    return ESP_OK;
}

esp_err_t scd41_init(i2c_master_bus_handle_t bus)
{
    s_bus = bus;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = OMNI_ADDR_SCD41,
        .scl_speed_hz    = 100000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
    if (err != ESP_OK) {
        return err;
    }

    /* Wake it and confirm it answers BEFORE issuing anything else. A sleeping
     * or busy SCD41 NACKs, and every NACK wedges the master, so firing commands
     * at a silent device only guarantees the probe fails too.
     *
     * The probe is itself the wake-up stimulus: the SCD41 wakes on seeing its
     * address, which is all the wake_up command really does — the command bytes
     * never reach it anyway, because the master aborts on the address NACK.
     * So probe, clear the bus, wait, and probe again. The first attempt after a
     * power_down is expected to fail; it is the one doing the waking. */
    bool present = false;
    for (int attempt = 0; attempt < 10 && !present; attempt++) {
        present = (i2c_master_probe(bus, OMNI_ADDR_SCD41, 100) == ESP_OK);
        if (!present) {
            i2c_master_bus_reset(bus);
            vTaskDelay(pdMS_TO_TICKS(SCD41_WAKE_RETRY_MS));
        }
    }
    if (present) {
        ESP_LOGI(TAG, "sensor answered at 0x%02X", OMNI_ADDR_SCD41);
    } else {
        ESP_LOGW(TAG, "no ACK at 0x%02X — sensor asleep or not wired", OMNI_ADDR_SCD41);
        return ESP_OK;  /* keep going; the rest of the hub does not depend on CO2 */
    }

    /* It is awake and answering, so it is safe to put it back into a known
     * state. Resetting the ESP32 does not reset the sensor, so it may still be
     * running a periodic measurement from before the reboot. */
    scd41_send_cmd(SCD41_CMD_STOP_PERIODIC);
    vTaskDelay(pdMS_TO_TICKS(SCD41_STOP_PERIODIC_DELAY_MS));

    scd41_send_cmd(SCD41_CMD_REINIT);
    vTaskDelay(pdMS_TO_TICKS(SCD41_REINIT_DELAY_MS));

    uint16_t serial[3];
    if (scd41_read_words(SCD41_CMD_GET_SERIAL_NUMBER, serial, 3, 2) == ESP_OK) {
        ESP_LOGI(TAG, "serial %04X%04X%04X", serial[0], serial[1], serial[2]);
    } else {
        ESP_LOGW(TAG, "serial number read failed");
    }
    return ESP_OK;
}

esp_err_t scd41_wake_up(void)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Datasheet: the sensor does not ACK wake_up. Send it and ignore the NACK —
     * scd41_send_cmd already clears the state machine the NACK leaves behind —
     * then give it the full wake-up time before talking to it again. */
    (void)scd41_send_cmd(SCD41_CMD_WAKE_UP);
    vTaskDelay(pdMS_TO_TICKS(SCD41_WAKE_UP_DELAY_MS));
    return ESP_OK;
}

esp_err_t scd41_power_down(void)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = scd41_send_cmd(SCD41_CMD_POWER_DOWN);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "power_down failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t scd41_measure_single_shot(uint16_t *co2_ppm, float *temp_c, float *rh_pct)
{
    if (s_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = scd41_send_cmd(SCD41_CMD_MEASURE_SINGLE_SHOT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "single shot trigger failed: %s", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(SCD41_SINGLE_SHOT_DELAY_MS));

    /* Poll the sensor's own data-ready flag rather than trusting the 5 s figure
     * from the datasheet, which is a maximum and not a guarantee. */
    bool ready = false;
    for (int attempt = 0; attempt < SCD41_READY_POLL_ATTEMPTS && !ready; attempt++) {
        uint16_t status;
        if (scd41_read_words(SCD41_CMD_GET_DATA_READY, &status, 1, 2) == ESP_OK) {
            /* Bits 0-10 hold the count; zero means nothing to read yet. */
            ready = (status & 0x07FF) != 0;
        }
        if (!ready) {
            vTaskDelay(pdMS_TO_TICKS(SCD41_READY_POLL_INTERVAL_MS));
        }
    }
    if (!ready) {
        ESP_LOGE(TAG, "measurement never became ready");
        return ESP_ERR_TIMEOUT;
    }

    err = scd41_send_cmd(SCD41_CMD_READ_MEASUREMENT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read command failed: %s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(SCD41_READ_CMD_DELAY_MS));

    uint8_t data[9]; /* CO2, T, RH — each a 16-bit word followed by its CRC */
    err = i2c_master_receive(s_dev, data, sizeof(data), 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "read failed: %s", esp_err_to_name(err));
        return err;
    }

    for (int word = 0; word < 3; word++) {
        const uint8_t *p = data + word * 3;
        if (sensirion_crc8(p, 2) != p[2]) {
            ESP_LOGE(TAG, "CRC mismatch on word %d", word);
            return ESP_ERR_INVALID_CRC;
        }
    }

    uint16_t co2    = ((uint16_t)data[0] << 8) | data[1];
    uint16_t t_raw  = ((uint16_t)data[3] << 8) | data[4];
    uint16_t rh_raw = ((uint16_t)data[6] << 8) | data[7];

    /* A CO2 reading of 0 means the sensor produced no valid sample. */
    if (co2 == 0) {
        ESP_LOGW(TAG, "sensor reported CO2 = 0, discarding sample");
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (co2_ppm) *co2_ppm = co2;
    if (temp_c)  *temp_c  = -45.0f + 175.0f * (float)t_raw / 65536.0f;
    if (rh_pct)  *rh_pct  = 100.0f * (float)rh_raw / 65536.0f;
    return ESP_OK;
}
