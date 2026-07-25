# OmniSensor — Milestones

Project deadline: **31 August 2026**.

These four milestones mirror the GitHub Milestones on the repository. Each has a due date and a task
checklist grouped by discipline.

| # | Milestone | Due | Status |
|---|---|---|---|
| 1 | [Procurement & Standalone Validation](#milestone-1--procurement--standalone-validation) | 2026-04-20 | ✅ Complete |
| 2 | [Hardware Design & Power Management](#milestone-2--hardware-design--power-management) | 2026-08-06 | ⏳ In progress |
| 3 | [Matter Integration & Advanced UI](#milestone-3--matter-integration--advanced-ui) | 2026-08-19 | ⬜ Not started |
| 4 | [Final Assembly, Testing & Release](#milestone-4--final-assembly-testing--release) | 2026-08-31 | ⬜ Not started |

## Critical path

The PCB is a **design deliverable only** — it is designed in Altium but never fabricated, and the working
unit is hand-wired. That removes board fabrication and shipping from the schedule entirely, so nothing
here waits on a supplier.

The binding constraint is instead the **7-day stability test** in Milestone 4. To report a full week of
uptime by 31 August, it must start no later than **23 August**. That is what sets Milestone 3's due date
of 19 August — four days of buffer before the test has to begin.

**If Milestone 3 slips past 23 August:** run the stability test for whatever time remains and report the
actual elapsed duration honestly, rather than delaying the release. A truthful 4-day result is worth more
than a padded one.

Enclosure printing is scheduled early in Milestone 4 (20–22 August) so that a failed print has room for a
second attempt without touching the deadline.

---

## Milestone 1 — Procurement & Standalone Validation

**Due 2026-04-20 · ✅ Complete**

> **Goal:** select components, assemble the breadboard prototype, and validate every sensor driver
> independently before attempting integration.

### Hardware
- [x] Select and procure components (XIAO ESP32-C6, LD2420, SCD41, SHT40, BH1750, SSD1315, SR602)
- [x] Solder headers and assemble the multi-sensor breadboard array

### Firmware
- [x] Standalone driver example — SR602 PIR with GPIO deep-sleep wakeup
- [x] Standalone driver example — SHT40 temperature and humidity, CRC-8 validated
- [x] Standalone driver example — BH1750 ambient light
- [x] Standalone driver example — SCD41 CO₂ single-shot measurement, CRC-8 validated
- [x] Standalone driver example — LD2420 mmWave radar over UART
- [x] Standalone driver example — SSD1315 OLED bring-up
- [x] Verify I²C bus stability and confirm no address conflicts (`0x23` / `0x3C` / `0x44` / `0x62`)
- [x] Verify UART communication with the radar
- [x] Integrate all seven into a single `all` example with RTC-memory fast wake

---

## Milestone 2 — Hardware Design & Power Management

**Due 2026-08-06 · ⏳ In progress**

> **Goal:** produce the complete PCB design, build the power hardware on the wired prototype, and
> implement the low-power multitasking firmware — with the sleep current actually measured.

### Hardware — design (Altium, not fabricated)
- [ ] Draw the electrical schematic including the SI2301 load switch and the ADC divider
- [ ] Specify external I²C pull-ups and per-device decoupling
- [ ] Route the PCB, separating the XIAO heat source from the SHT40 and SCD41
- [ ] Add copper relief slots around the temperature and humidity sensors
- [ ] Export gerbers to `hardware/gerbers/` and a schematic PDF to `hardware/pdf/`

### Hardware — wired prototype (built)
- [ ] Wire the SI2301 P-MOSFET rail cut, with the external gate pull-up (fail-safe OFF)
- [ ] Fit the 100 µF+ bulk capacitor on the switched rail
- [ ] Wire the 2× 100 kΩ battery divider with the 100 nF stabilising capacitor
- [ ] Document the wiring in `hardware/prototype/` with a signal table and photos

### Firmware
- [ ] Implement the FreeRTOS task architecture with the priority split from `project_description.md` §8
- [ ] Implement RTC shared memory for instant UI refresh on wake
- [ ] Implement GPIO load-switch control, cutting the sensor rail before deep sleep
- [ ] Implement the ADC battery task using `esp_adc_cal` and 32–64× multisampling
- [ ] Sample the battery *before* enabling the Thread radio, to avoid RF noise on the ADC
- [ ] Implement the physical button handler for manual wake and Matter factory reset
- [ ] **Move the radar off `UART_NUM_0`** — it currently collides with the log console
      (see `project_description.md` §17)

### Measurement
- [ ] Measure deep-sleep current with the sensor rail off; record against the ~7–10 µA target
- [ ] Verify the divider reading against a multimeter across the LiPo range
- [ ] Record both results in `docs/HARDWARE_DESIGN.md`

### Design
- [ ] Model the 3D enclosure with the PIR lens holder and flush mmWave mounting points
- [ ] Size the internal volume for cabling rather than a flat PCBA

---

## Milestone 3 — Matter Integration & Advanced UI

**Due 2026-08-19 · ⬜ Not started**

> **Goal:** join the Matter ecosystem and build a user interface worth looking at. Runs entirely on the
> wired prototype.

### Firmware
- [ ] Configure Matter endpoints for Temperature, Relative Humidity, CO₂ and Illuminance
- [ ] Configure the Occupancy Sensing cluster
- [ ] Configure the Power Source cluster for battery reporting
- [ ] Implement Thread commissioning and verify against Apple Home
- [ ] Verify commissioning against Home Assistant
- [ ] Operate as a Sleepy End Device with instant unsolicited reporting on presence
- [ ] Decide how to expose the three distance zones over Matter (see `project_description.md` §17)
- [ ] Enforce a minimum interval between SCD41 measurements to bound wake cost

### UI
- [ ] Write a graphics driver supporting proportional fonts
- [ ] Add per-measurement icons (temperature, humidity, CO₂, lux)
- [ ] Add an explicit "updating" state so stale RTC data is never mistaken for live data

### Calibration
- [ ] Build a radar calibration routine that tunes the per-gate sensitivity threshold
- [ ] Calibrate against an empty room and validate the 0.7 m zone boundaries
- [ ] Validate PIR + radar fusion holds occupancy for a stationary person

---

## Milestone 4 — Final Assembly, Testing & Release

**Due 2026-08-31 · ⬜ Not started**

> **Goal:** assemble the finished unit, prove it runs, and document it well enough for someone else to
> rebuild it.

### Assembly — target 20–22 August
- [ ] Print the enclosure
- [ ] Mount the PIR Fresnel lens holder at the correct focal distance
- [ ] Flush-mount the mmWave radar to avoid internal reflections
- [ ] Verify SCD41 airflow through the vents
- [ ] Final wiring cleanup and strain relief; fit the prototype into the case

### Testing — start no later than 23 August
- [ ] Run the 7-day stability test
- [ ] Record power consumption and real-world uptime
- [ ] Confirm no Task Watchdog resets over the full run
- [ ] Verify Matter reporting stays reliable across the whole period

### Release
- [ ] Tag firmware v1.0
- [ ] Complete the README with the final Bill of Materials
- [ ] Publish schematics, gerbers and wiring diagrams
- [ ] Write the step-by-step Matter commissioning and sensor calibration guide
- [ ] Record the measured power figures alongside the original targets
