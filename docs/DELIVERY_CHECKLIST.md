# OmniSensor — Delivery Checklist & Schedule

Project deadline: **8 September 2026, end of day.**

## Project Overview

| # | Milestone | Status |
|---|---|---|
| 1 | **Procurement & Standalone Validation** — Select components, breadboard prototype, validate drivers. | ✅ Complete |
| 2 | **Integrated Firmware & Power Management** — Task architecture, load switching, battery sensing. | ✅ Firmware complete, hardware prototyping tasks cut |
| 3 | **Matter Integration** — Join the Thread mesh, report measurements to a controller. | ⏳ Mostly done (commissioned, radar parsing & button open) |
| 4 | **Hardware Design, Assembly & Release** — PCB design, enclosure print, stability test, documentation. | ⏳ In progress |

## Scope Changes & Clarifications

- **No LiPo cell.** The battery did not arrive. The divider and load switch are still designed in
  Altium; the device runs from USB-C. Battery life is reported as an arithmetic projection from
  datasheet values, explicitly labelled as a projection.
- **Sensor rail scope.** The switched rail feeds the LD2420 radar only. The SHT40, SCD41 and BH1750
  stay on the always-on 3V3 rail. The SCD41 is parked with its own `power_down` command in firmware.
- **No external button.** The XIAO's built-in BOOT button (GPIO9) is used for short-press refresh and
  long-press factory reset. There is no external tactile switch, pull-up or debounce capacitor.
- **Full design is the architecture.** The MOSFET load switch (SI2301 on GPIO21/D3), bulk capacitors,
  battery divider (GPIO0/D0) and external I²C pull-ups (3.3 kΩ) are part of the intended
  architecture, implemented in firmware and the Altium project. The breadboard prototype is the wired
  validation unit.
- **UART.** The radar runs on UART0, pins 16/17. This works because the XIAO logs over its native
  USB-Serial/JTAG peripheral, but any build that routes the console back to UART0 would conflict.
  Pin assignments: RAIL_EN = GPIO21 (D3), BATT_SENSE = GPIO0 (D0), BTN = GPIO9 (BOOT, on-module).

## Schedule (4–8 September 2026)

**M** = you, **C** = Claude.

### Thursday 4 September — today

- [x] **M/C** Firmware stabilisation: resolve radar parsing and implement BOOT button handler
- [x] **M** Enclosure modelling prep: confirm caliper measurements and dimensions

### Friday 5 September

- [ ] **M** Model the case in Fusion/FreeCAD from `enclosure/ENCLOSURE_SPEC.md`
- [ ] **M** Export STL to `enclosure/stl/` and STEP to `enclosure/cad/`
- [ ] **M** Slice, preview, **start the print before bed**
- [ ] **M** Altium schematic entry from `hardware/DESIGN_PACKAGE.md` §3

### Saturday 6 September

- [ ] **M** Test-fit every module; reprint if needed (this is your only slack)
- [ ] **M** Final wiring cleanup, strain relief, fit everything into the case
- [ ] **M** Photograph the build for `hardware/prototype/`
- [ ] **M** Altium: placement, thermal relief slots around SHT40 and SCD41, route, DRC clean

### Sunday 7 September

- [ ] **M** Altium: export gerbers to `hardware/gerbers/`, schematic PDF to `hardware/pdf/`, BOM CSV
- [ ] **M** Flash the final firmware and **start the stability run**:
      `idf.py monitor | tee ~/soak.log`
- [ ] **M** Confirm the device is visible in Home Assistant before walking away
- [ ] **C** Draft README, commissioning guide, and remaining docs

### Monday 8 September — delivery

- [ ] **M** Stop the soak, note the actual elapsed hours
- [ ] **C** Write up results: uptime, resets, heap trend, Matter still reporting
- [ ] **C** Finish the README: final BOM, build instructions, honest status
- [ ] **M** Read the README as if you had never seen the project
- [ ] **M** `git tag -a v1.0 -m "OmniSensor v1.0"` and push
- [ ] **Deliver**

---

## Definition of Done

1. `firmware/hub/` builds clean and runs on the wired prototype
2. Commissioned into Home Assistant with all five measurements live
3. Occupancy driven by PIR and radar fusion
4. Altium schematic and layout complete, gerbers and PDF committed
5. Enclosure designed, printed, and the unit assembled inside it
6. Stability run completed and reported as measured elapsed time
7. Documentation truthful about what was measured, what was projected, and what was cut

## Cut List

- **SI2301 and divider on protoboard** — the PCB design in Altium is the deliverable.
- **External button circuit** — using the built-in BOOT button (GPIO9).
- Proportional OLED font and per-measurement icons
- Per-gate radar calibration
- Zone exposure over Matter beyond the boolean
- A second Matter ecosystem

## Known Limitations

- **Apple Home and Google Home attestation.** These ecosystems reject the esp-matter development
  attestation certificates (vendor ID `0xFFF1`). Home Assistant accepts them. This is a certification
  question, not a firmware defect.
- **No real-time clock.** `GetClock_RealTimeMS()` is unsupported, so the stack falls back to Last
  Known Good Time. Harmless for a sensor, visible in the log during CASE.

## Deferred to Future Work

- Expose the three distance zones over Matter (`project_description.md` §17.4)
- Per-gate radar sensitivity calibration against an empty room
- Verify against a second ecosystem
- Proportional-font UI with per-measurement icons

## If You Fall Behind

- **Print fails** → run the soak on the bare breadboard and photograph the case separately. The case
  is a deliverable; running *inside* it for the full duration is not.
- **Soak slips** → report the hours you actually got and say so plainly. A truthful short result is
  worth more than a padded one.
- **CO₂ never works** → ship four measurements, document the sensor fault, keep the cluster in the
  data model. A known gap costs less than a vague one.
