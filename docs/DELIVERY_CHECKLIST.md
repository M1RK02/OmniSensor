# Delivery checklist — to 8 September 2026, EOD

Five days, working every one of them. Tick as you go. **M** = you, **C** = Claude.

**The critical path is the enclosure, not the firmware.** The print has to start
Saturday evening to leave any room for a reprint, and assembly has to finish so the
stability run can start Sunday evening. Firmware can absorb slippage; the print
cannot.

**Hard gates**

| When | Gate |
|---|---|
| Fri 4 Sep, EOD | Caliper measurements done, or Saturday's modelling has nothing to work from |
| Sat 5 Sep, evening | Print started |
| Sun 6 Sep, evening | Unit assembled, stability run started — Monday evening is the fallback |
| Mon 7 Sep, EOD | Altium exports committed |
| Tue 8 Sep | Documentation finished, tagged, delivered |

> The run is now roughly 48 hours if it starts Sunday, 24 if it slips to Monday.
> Report the elapsed hours you actually got. A truthful 24-hour result is worth
> more than a padded one.

---

## Friday 4 September — today

Nothing has been flashed since Wednesday. There is an **unflashed build** in
`firmware/hub/build/` carrying the SCD41 probe-based wake and an I²C bus scan that
settles whether CO₂ is a firmware or a wiring problem.

- [x] **M** Remove the radar, and use PIR for occupancy
- [x] **M** `cd firmware/hub && idf.py -p /dev/ttyACM0 flash monitor` (5 min)
- [x] **M** Send the `omni_i2c: bus scan:` line — it prints every address that answers
  - `0x23 0x3C 0x44 0x62` → all four present, CO₂ is firmware, C fixes it
  - `0x62` missing → the SCD41 is not answering; reseat its power and ground jumpers first

- [x] **M** **Fill in [enclosure/DIMENSIONS.md](../enclosure/DIMENSIONS.md) with calipers** (30 min)
      — the one thing today that everything downstream waits on

- [x] **C** Write `enclosure/ENCLOSURE_SPEC.md` as soon as the measurements land
- [x] **C** Fix whatever the bus scan reveals
- [x] **M** Reflash, confirm a real ppm figure
- [x] **M** Remove the device from Home Assistant and **recommission** — the endpoint
      attributes changed and HA caches the cluster layout from the interview
- [x] **M** Confirm all five entities appear: temperature, humidity, illuminance, CO₂, occupancy
- [ ] **M** If there is any evening left: create the Altium project and pull library parts

- [ ] **M** Add back the radar, walk toward the radar, confirm `target at N cm -> zone` appears
---

## Saturday 5 September — model and print

**Gate: the print starts tonight.**

- [ ] **M** Model the case in Fusion/FreeCAD from `ENCLOSURE_SPEC.md` (morning)
- [ ] **M** Export STL to `enclosure/stl/` and STEP to `enclosure/cad/`
- [ ] **M** Slice and check the preview: no supports through the radar window
- [ ] **M** **Start the print before bed — this is the gate**
- [ ] **M** Altium schematic entry from [hardware/DESIGN_PACKAGE.md](../hardware/DESIGN_PACKAGE.md) §3
- [ ] **M** Annotate, run ERC, get it clean
- [ ] **C** Firmware stabilisation: anything still outstanding from Friday's flash

---

## Sunday 6 September — assemble and start the run

**Gate: the stability run starts this evening.**

- [ ] **M** Test-fit every module; reprint if needed (this is your only slack)
- [ ] **M** Final wiring cleanup, strain relief, fit everything into the case
- [ ] **M** Photograph the build for `hardware/prototype/`
- [ ] **M** Altium: placement, thermal relief slots around the SHT40 and SCD41, route, DRC clean
- [ ] **M** Flash the final firmware and **start the run**:
      `idf.py monitor | tee ~/soak.log`
- [ ] **M** Confirm it is still visible in Home Assistant before walking away
- [ ] **C** Wiring table into `hardware/prototype/`
- [ ] **C** Draft the README and commissioning guide — everything that does not
      depend on the soak numbers, so Tuesday is not writing from scratch

---

## Monday 7 September — exports, leave the soak alone

- [ ] **M** Altium: export gerbers to `hardware/gerbers/`, schematic PDF to
      `hardware/pdf/`, BOM CSV
- [ ] **C** Review the exports against the design package
- [ ] **M** Spot-check `~/soak.log` for watchdog resets or a falling heap trend
- [ ] **M** *If the run slipped to today, start it this evening — last possible moment*
- [ ] **C** Close out `project_description.md` §17 and the milestone checkboxes
- [ ] **C** Fill in the measured-results table in `docs/HARDWARE_DESIGN.md` §3

---

## Tuesday 8 September — documentation and delivery

- [ ] **M** Stop the soak, note the actual elapsed hours
- [ ] **C** Write up the results: uptime, resets, heap trend, Matter still reporting
- [ ] **C** Finish the README: final bill of materials, build instructions, honest status
- [ ] **C** Finish the commissioning guide, including that Apple and Google Home reject
      the development attestation certificates and Home Assistant does not
- [ ] **M** Read the README as if you had never seen the project
- [ ] **M** `git checkout main && git merge develop`
- [ ] **M** `git tag -a v1.0 -m "OmniSensor v1.0"` and push
- [ ] **Deliver**

---

## Definition of done

1. `firmware/hub/` builds clean and runs on the wired prototype
2. Commissioned into Home Assistant with all five measurements live
3. Occupancy driven by PIR and radar, with zones reported in the log
4. Altium schematic and layout complete, gerbers and PDF committed
5. Enclosure designed, printed, and the unit assembled inside it
6. Stability run completed and reported as measured elapsed time
7. Documentation truthful about what was measured, what was projected, what was cut

---

## Cut list

Two days were lost, so cut earlier than you would like. Everything here is already
recorded as future work in `docs/MILESTONES.md`; cutting it costs scope, not
credibility.

1. **The SI2301 and divider on protoboard** — cut this first and without regret. The
   PCB design is the deliverable, and with no LiPo the measurement was always going
   to be partial. Saves a Saturday afternoon you now need for the print.
2. Proportional OLED font and per-measurement icons
3. Per-gate radar calibration
4. Zone exposure over Matter beyond the boolean
5. A second Matter ecosystem
6. PCB routing — if Monday is tight, export the schematic PDF alone and say layout
   was not completed. A finished schematic beats a half-routed board.

**Never cut:** the printed case, the stability run however short, the Altium
schematic, and the honesty pass over the docs.

## If you fall behind

- **Print fails Sunday** → run the soak on the bare breadboard and photograph the
  case separately. The case is a deliverable; running *inside* it for 48 hours is not.
- **Soak slips to Monday night** → report the hours you got and say so plainly.
- **CO₂ never works** → ship four measurements, document the sensor fault, keep the
  cluster in the data model. A known gap costs less than a vague one.
