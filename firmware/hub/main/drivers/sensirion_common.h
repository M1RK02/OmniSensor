/* CRC-8 shared by the SHT40 and SCD41 (Sensirion polynomial 0x31, init 0xFF).
 * Lifted unchanged from the validated Milestone 1 examples. */
#pragma once

#include <stddef.h>
#include <stdint.h>

static inline uint8_t sensirion_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}
