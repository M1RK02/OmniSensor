# OmniSensor — Milestones

Project deadline: **8 September 2026, end of day.**

> **Schedule re-baselined on 2 September 2026.** The original plan targeted 31 August with a seven-day
> stability test. That date passed with Milestones 2–4 unstarted, so the remaining work has been
> replanned into the seven days that actually exist rather than left as a plan that no longer describes
> reality. The stability test is the visible casualty: it is now roughly 40 hours, not 7 days, and is
> reported as measured elapsed time. The original document's own policy applies — *a truthful 4-day
> result is worth more than a padded one.*

| # | Milestone | Due | Status |
|---|---|---|---|
| 1 | [Procurement & Standalone Validation](#milestone-1--procurement--standalone-validation) | 2026-04-20 | ✅ Complete |
| 2 | [Integrated Firmware & Power Management](#milestone-2--integrated-firmware--power-management) | 2026-09-04 | ⏳ In progress |
| 3 | [Matter Integration](#milestone-3--matter-integration) | 2026-09-05 | ⏳ Commissioned 2 Sep |
| 4 | [Hardware Design, Assembly & Release](#milestone-4--hardware-design-assembly--release) | 2026-09-08 | ⬜ Not started |

## Critical path

The PCB is a **design deliverable only** — designed in Altium, never fabricated — so nothing waits on a
supplier. Two things genuinely gate the deadline:

1. **The enclosure print.** Modelling finishes Friday 4 September and the print starts that evening,
   leaving Saturday as slack for exactly one reprint. Assembly is Sunday.
2. **The stability run.** It must start Sunday 6 September evening to report ~40 hours by Tuesday.

**If the print slips past Saturday:** run the stability test on the bare wired unit and photograph the
case separately. The case is a deliverable; running *inside* the case for 40 hours is not.

**If Matter commissioning has not succeeded by end of Saturday:** ship the integrated non-Matter hub and
document the blocker. Everything else in Milestone 2 still lands.

## Scope changes made at re-baseline

Recorded rather than quietly dropped:

- **Matter scope reduced** to "commission successfully once, against one controller, with all five
  measurements live". Multi-ecosystem verification, exposing the three distance zones over Matter, and
  the per-gate radar calibration routine are documented as future work.
- **No LiPo cell** — the battery did not arrive. The divider and load switch are still built and
  measured; the device runs from USB-C. Battery life is reported as an arithmetic projection from the
  measured 3V3-rail current, explicitly labelled as a projection, not a measurement.
- **Sensor rail scope corrected.** The load switch now feeds the LD2420 only; the SCD41 is parked with
  its own `power_down` command instead. See `docs/HARDWARE_DESIGN.md` §2 for why the original
  everything-on-one-rail design could not work with the OLED on the always-on bus.

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

## Milestone 2 — Integrated Firmware & Power Management

**Due 2026-09-04 · ⏳ In progress**

> **Goal:** turn the `all` example's monolith into the real `firmware/hub/` application — task
> architecture, load switching, battery sensing — and build the power hardware on the wired prototype.

### Firmware — 2–3 September
- [x] Scaffold `firmware/hub/` on ESP-Matter, ESP32-C6 target, ICD/light-sleep configuration
- [x] Verify the ESP-Matter toolchain builds for `esp32c6`
- [x] Extract per-device drivers out of the examples into `main/drivers/`
- [x] **Move the radar off `UART_NUM_0`** — closes `project_description.md` §17.1
- [x] Resolve the three unassigned GPIOs — closes `project_description.md` §17.2
- [x] Single-writer event queue and the task priority split from `project_description.md` §8
- [x] GPIO load-switch control with fail-safe-OFF ordering
- [x] Battery ADC with `adc_oneshot` + curve-fitting calibration and 32× multisampling
- [x] Sample the battery *before* the radio starts, to keep RF noise off the ADC
- [x] Enforce a minimum interval between SCD41 measurements — closes `project_description.md` §17.6
- [x] Flash and validate on the wired prototype

### Hardware — wired prototype, 3 September
- [ ] Wire the SI2301 rail cut with the external gate pull-up (fail-safe OFF)
- [ ] Fit the 100 µF+ bulk capacitor on the switched rail
- [ ] Wire the 2× 100 kΩ battery divider with the 100 nF stabilising capacitor
- [ ] Document the wiring in `hardware/prototype/` with a signal table and photos

### Measurement — 5 September
- [ ] Measure 3V3-rail current, radar rail off and SCD41 asleep, against the 7–10 µA design target
- [ ] Measure active current during a full measurement cycle
- [ ] Verify the divider against a multimeter across a 3.0–4.2 V bench-supply sweep
- [ ] Record all results in `docs/HARDWARE_DESIGN.md` §3

---

## Milestone 3 — Matter Integration

**Due 2026-09-05 · ⬜ Not started**

> **Goal:** join the Thread mesh and prove it, once, properly.

### Firmware — 4 September
- [x] Temperature, Relative Humidity and Illuminance endpoints reporting live values
- [x] CO₂ via a Carbon Dioxide Concentration Measurement cluster on an Air Quality Sensor endpoint
- [ ] Occupancy Sensing cluster driven by PIR + radar fusion — **PIR half working; the radar has never returned a parseable line**
- [x] Operate as an ICD (Sleepy End Device) with an immediate report on presence
- [ ] Physical button: short press refreshes, long press performs the Matter factory reset

### Commissioning — 4–5 September
- [x] Commission over Thread against the Border Router — fabric 0x1, `ha-thread-5af3`, PAN 0x5af3, channel 15, attached as child (2 Sep)
- [ ] Read all five measurements from the controller
- [ ] Confirm presence reaches the controller within ~2 s of someone walking in
- [ ] Confirm the long-press factory reset clears the fabric and re-advertises

### Known limitations

- **Apple Home and Google Home will reject this device.** It uses the esp-matter
  development attestation certificates (vendor ID `0xFFF1`), and both ecosystems enforce certified
  attestation. Home Assistant accepts them. This is a certification question, not a firmware defect,
  and closing it needs a real vendor ID from the CSA.
- **No real-time clock.** `GetClock_RealTimeMS()` is unsupported, so the stack falls back to Last
  Known Good Time. Harmless for a sensor, visible in the log during CASE.

### Deferred to future work
- [ ] Expose the three distance zones over Matter (`project_description.md` §17.4)
- [ ] Per-gate radar sensitivity calibration against an empty room
- [ ] Verify against a second ecosystem
- [ ] Proportional-font UI with per-measurement icons

---

## Milestone 4 — Hardware Design, Assembly & Release

**Due 2026-09-08 · ⬜ Not started**

> **Goal:** finish the PCB design, get the case printed and fitted, prove the thing runs, and document
> it well enough for someone else to rebuild it.

### PCB — Altium, designed not fabricated
- [ ] Design package: net list, BOM, pull-up and decoupling values, placement and thermal plan (3 Sep)
- [ ] Create the Altium project and library parts (3 Sep)
- [ ] Schematic entry and ERC (4 Sep)
- [ ] Placement, copper relief around the SHT40 and SCD41, routing, DRC (6 Sep)
- [ ] Export gerbers to `hardware/gerbers/` and the schematic PDF to `hardware/pdf/` (7 Sep)

### Enclosure
- [ ] Caliper every module and fill the dimension table (2 Sep)
- [ ] Enclosure specification with the PIR focal standoff and radar window constraints (3 Sep)
- [ ] Model in Fusion/FreeCAD, export STL and STEP (4 Sep)
- [ ] **Print — start Friday evening** (4 Sep)
- [ ] Test-fit, adjust, reprint if needed (5 Sep)
- [ ] Final wiring cleanup, strain relief, close the case (6 Sep)

### Testing — starts 6 September evening
- [ ] Run the stability test for the time available (~40 h) and report actual elapsed hours
- [ ] Confirm no Task Watchdog resets over the full run
- [ ] Verify Matter reporting is still live at the end of the run

### Release — 7–8 September
- [ ] Update `project_description.md` §17 as each open gap closes
- [ ] Record measured power figures alongside the original targets
- [ ] Complete the README with the final Bill of Materials
- [ ] Write the Matter commissioning guide
- [ ] Tag firmware v1.0
