/* SR602 PIR — motion trigger on GPIO2, active HIGH.
 *
 * The PIR is the TRIGGER and the radar is the QUALIFIER (see
 * project_description.md §9). It draws almost nothing and can wake the SoC,
 * which is why it, not the radar, is the wake source.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

/* Configure the pin, install the ISR and arm it as a light-sleep wake source. */
esp_err_t pir_init(void);

/* Current pin level — used to avoid sleeping while the sensor is still asserted. */
bool pir_is_asserted(void);

/* Block until motion is detected, or until the timeout expires.
 * Returns true if motion fired. */
bool pir_wait_for_motion(uint32_t timeout_ms);
