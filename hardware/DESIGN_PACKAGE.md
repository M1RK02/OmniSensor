# OmniSensor PCB — design package

**Everything needed to draw the board in Altium without stopping to ask a question.**

This board is a **design deliverable, not a fabricated one** (`docs/HARDWARE_DESIGN.md` §0). It is still
drawn to fabrication standard — gerbers and a schematic PDF are committed — because for a design-only
deliverable the exported documents *are* the artifact.

---

## 1. Scope decision: what goes on the board

Not every device becomes a chip on this PCB, and the split is deliberate:

| Device | On-board part | Why |
|---|---|---|
| XIAO ESP32-C6 | Socketed on 2×7 headers | It is a module. Socketing keeps the expensive part reusable and the USB-C connector at a known height. |
| SHT40 | **Bare IC, DFN-4** | Thermal isolation is the whole point (§9). A breakout on a header sits in unpredictable airflow and cannot have copper relief milled around it. |
| SCD41 | **Bare module, LGA** | Same reason, plus it needs local bulk capacitance right at its pins. |
| BH1750 | **Bare IC, WSOF-6** | Cheap to place, and it wants a defined optical window position. |
| SSD1315 OLED | 4-pin header | Off-board: it mounts to the enclosure front face, not the PCB. |
| LD2420 radar | 4-pin header | Off-board: it must sit flush against the case wall, so it cannot be co-planar with the PCB. |
| SR602 PIR | 3-pin header | Off-board: the Fresnel lens focal spacing is set by the enclosure, not the board. |

The three off-board devices are exactly the three with mechanical or optical constraints the enclosure
owns. That is not a coincidence — it is the reason for the split.

---

## 2. Power rails

| Net | Source | Feeds |
|---|---|---|
| `+3V3_AO` | XIAO `3V3` pin | SHT40, SCD41, BH1750, OLED, I²C pull-ups, button pull-up, SI2301 source and gate pull-up |
| `+3V3_SW` | SI2301 drain | **LD2420 only** |
| `VBAT` | XIAO `BAT+` pad | Battery divider top |
| `GND` | XIAO `GND` | Everything |

**Why only the radar is switched.** The original design put all four sensors on the switched rail. That
cannot work: the OLED is on the always-on rail and shares the I²C bus, so the bus pull-ups stay
energised when the rail is cut, and every unpowered I²C device is back-powered through its ESD diodes.
No current is saved and a half-powered SCD41 can hold SDA low and wedge the bus. The SHT40 (~0.08 µA
idle) and BH1750 (~0.01 µA powered down) never justified a switch anyway. The SCD41 is parked with its
own `power_down` command in firmware. The LD2420 is the real load — tens of milliamps, no low-power
mode — and it is on UART, so cutting its rail creates no bus hazard at all. Full reasoning in
`docs/HARDWARE_DESIGN.md` §2.

---

## 3. Net list

XIAO pin names are the silkscreen labels; GPIO numbers are the ESP32-C6 pins behind them.

| Net | XIAO pin | Connects to |
|---|---|---|
| `I2C_SDA` | D4 (GPIO22) | SHT40.SDA, SCD41.SDA, BH1750.SDA, J_OLED.3, R1 → `+3V3_AO` |
| `I2C_SCL` | D5 (GPIO23) | SHT40.SCL, SCD41.SCL, BH1750.SCL, J_OLED.4, R2 → `+3V3_AO` |
| `RADAR_TX` | D6 (GPIO16) | J_RADAR.3 (module RX) |
| `RADAR_RX` | D7 (GPIO17) | J_RADAR.4 (module TX) |
| `PIR_OUT` | D2 (GPIO2) | J_PIR.2, R3 (100 k) → `GND` |
| `BATT_SENSE` | D0 (GPIO0) | R4/R5 divider midpoint, C1 (100 nF) → `GND` |
| `BTN` | D1 (GPIO1) | SW1 → `GND`, R6 (10 k) → `+3V3_AO`, C2 (100 nF) → `GND` |
| `RAIL_EN` | D3 (GPIO21) | Q1 gate, R7 (100 k) → `+3V3_AO` |
| `VBAT` | BAT+ | R4 top |
| `+3V3_SW` | — | Q1 drain, J_RADAR.1, C3 (100 µF), C4 (100 nF) |

**Cross the radar UART deliberately.** `RADAR_TX` (an output) goes to the module's **RX** pin and
`RADAR_RX` to its **TX**. Label the header pins with the *module's* names on the silkscreen and add a
`TX→RX` note, because this is the single easiest thing to get backwards.

Unconnected and available for expansion: D8 (GPIO19), D9 (GPIO20), D10 (GPIO18). Bring them to a
3-pin header with `+3V3_AO` and `GND` if there is room — it costs nothing and makes the board reusable.

**Never route to:** GPIO14 (RF switch select) and GPIO3 (RF switch enable) are internal to the XIAO
module and not on the header. Strapping pins GPIO4, 5, 8, 9, 15 are also avoided by this pin map.

---

## 4. I²C pull-up sizing

The examples currently lean on the ESP32-C6's internal pull-ups, which are tens of kΩ and marginal for
a four-device bus. This closes `project_description.md` §17.3 with a number rather than a shrug.

Estimated bus capacitance: 4 devices × ~10 pF + ~20 pF of trace and header capacitance ≈ **80 pF**.

The OLED runs at **400 kHz**, so the fast-mode rise-time limit of 300 ns governs:

```
Rp(max) = tr / (0.8473 × Cb) = 300 ns / (0.8473 × 80 pF) ≈ 4.4 kΩ
Rp(min) = (VDD − VOL) / IOL = (3.3 V − 0.4 V) / 3 mA    ≈ 0.97 kΩ
```

A 4.7 kΩ pull-up gives tr = 0.8473 × 4.7 kΩ × 80 pF ≈ **319 ns — over the limit.** It is the reflexive
default and it is wrong here.

**Specify 3.3 kΩ** (R1, R2): tr ≈ 224 ns, comfortable margin, and only ~1 mA sink per line while a
device holds the line low — which happens only during a transaction, never at idle. If the design is
later restricted to 100 kHz everywhere, 4.7 kΩ becomes correct again.

---

## 5. Decoupling

| Location | Capacitors | Note |
|---|---|---|
| SHT40 | 100 nF | Standard local decoupling |
| BH1750 | 100 nF | Standard local decoupling |
| **SCD41** | **100 nF + 10 µF** | Non-negotiable. The photoacoustic measurement pulses draw peaks in the hundreds of mA. Without local bulk *at the pins* the rail sags and the other I²C devices see it. |
| OLED header | 100 nF | Charge pump switching noise |
| `+3V3_SW` rail | **100 µF + 100 nF** | Absorbs LD2420 inrush at rail turn-on |
| XIAO 3V3 header pin | 10 µF | Bulk at the source |

Place every 100 nF within 2 mm of its device's supply pin, on the same side, with a via to ground
directly under the pad where possible.

---

## 6. SI2301 load switch (Q1)

```
        +3V3_AO ──┬───────────── source (pin 3)
                  │
                 R7 100k
                  │
   RAIL_EN ───────┴───────────── gate (pin 1)

                     drain (pin 2) ──┬── +3V3_SW ── J_RADAR.1
                                     ├── C3 100 µF
                                     └── C4 100 nF
```

- **Direct GPIO drive, no level shifter.** The source sits at 3.3 V and the GPIO swings 0–3.3 V, so
  Vgs is 0 V (off) or −3.3 V (on). The SI2301's threshold is well inside that.
- **Active LOW.** `RAIL_EN` low turns the rail ON.
- **R7 is the safety feature, not a formality.** It holds the gate at the source voltage whenever the
  GPIO is not actively driving — during boot, after a crash, throughout sleep. A P-MOSFET with its gate
  at the source is *off*. The rail therefore fails to OFF, never to a silently flat battery.
- Orientation matters: source to the supply, drain to the load. Reversed, the body diode conducts and
  the switch never turns off. Double-check this against the SOT-23 pinout at schematic review.

---

## 7. Battery divider

```
   VBAT ──[ R4 100k ]──┬──[ R5 100k ]── GND
                       │
                       ├── C1 100 nF ── GND
                       │
                    BATT_SENSE → XIAO D0 (GPIO0 / ADC1_CH0)
```

- **100 kΩ chosen for leakage, not accuracy.** At 4.2 V the divider bleeds 4.2 V / 200 kΩ ≈ **21 µA**,
  the same order as the entire target sleep current. Lower resistances measure marginally better and
  cost far more current — a bad trade on a battery device.
- **C1 is required, not optional.** The ADC's sample-and-hold draws a brief charge current. Driven from
  a 50 kΩ Thévenin source it would not settle inside the sampling window and every reading would come
  out low. C1 is the local charge reservoir.
- Use **1 % resistors**. The ratio is what sets the reading; a 5 % pair can be 10 % apart.
- Firmware uses −12 dB attenuation, putting a halved 4.2 V at 2.1 V — mid-scale, the ADC's most linear
  region.

---

## 8. Placement and thermal plan

The XIAO module is the dominant heat source: radio, regulator and CPU are all inside it. A temperature
sensor reading its own board's self-heating is worse than useless, because the error is systematic and
looks entirely plausible.

1. **Divide the board into a hot half and a cold half.** XIAO, the load switch and the headers go in
   the hot half. SHT40 and SCD41 go as far into the cold half as the outline allows.
2. **Break the copper path.** Route milled slots between the two halves, on both sides of the SHT40 and
   partially around the SCD41. Copper is an excellent thermal conductor — that is precisely the problem.
   A ground pour that runs continuously from the module to the SHT40 delivers waste heat straight to it.
3. **Relieve the pour.** No pour directly under the SHT40. Connect its ground pin with a single narrow
   neck, not a plane.
4. **Keep the SCD41 near the vent path** identified in the enclosure spec, and away from the XIAO's
   convection plume.
5. **BH1750 needs line of sight** to the enclosure's light aperture — place it on the face that will
   point outward.
6. Keep the I²C pull-ups near the middle of the bus, not at one end.

---

## 9. Stack-up and routing rules

- **2 layers** is sufficient and appropriate. Top = signal + power, bottom = ground pour, deliberately
  interrupted by the thermal relief slots.
- Track widths: 0.25 mm signal, 0.5 mm for `+3V3_AO` and `+3V3_SW`, 0.8 mm for `GND` necks.
- Clearance 0.2 mm; via 0.3 mm drill / 0.6 mm pad. All well inside any fab's cheapest process.
- Keep `I2C_SDA` and `I2C_SCL` roughly the same length and routed together; do not run them under the
  XIAO's antenna keep-out.
- **Antenna keep-out:** no copper, no pour, no tracks under the XIAO's ceramic antenna end. Mark it as
  a keep-out region so the DRC enforces it rather than relying on memory.

---

## 10. Bill of materials

Values are what matter; substitute freely on package and manufacturer if stock is short. Verify
availability before ordering — though for this project nothing is being ordered.

| Ref | Value / Part | Package | Example MPN |
|---|---|---|---|
| U1 | Seeed XIAO ESP32-C6 | 2×7 2.54 mm socket | Seeed 113991114 |
| U2 | Sensirion SHT40-AD1B | DFN-4 1.5×1.5 mm | SHT40-AD1B-R2 |
| U3 | Sensirion SCD41 | LGA 10.1×10.1 mm | SCD41-D-R2 |
| U4 | ROHM BH1750FVI | WSOF-6 | BH1750FVI-TR |
| Q1 | P-channel MOSFET | SOT-23 | Vishay SI2301CDS-T1-GE3 |
| R1, R2 | 3.3 kΩ 1 % | 0603 | Yageo RC0603FR-073K3L |
| R3, R7 | 100 kΩ 1 % | 0603 | Yageo RC0603FR-07100KL |
| R4, R5 | 100 kΩ **1 %** | 0603 | Yageo RC0603FR-07100KL |
| R6 | 10 kΩ 1 % | 0603 | Yageo RC0603FR-0710KL |
| C1, C2, C4 | 100 nF X7R 50 V | 0603 | Samsung CL10B104KB8NNNC |
| C5–C8 | 100 nF X7R 50 V | 0603 | Samsung CL10B104KB8NNNC |
| C9 | 10 µF X5R 6.3 V (SCD41 bulk) | 0805 | Murata GRM21BR61A106KE19L |
| C10 | 10 µF X5R 6.3 V (3V3 bulk) | 0805 | Murata GRM21BR61A106KE19L |
| C3 | 100 µF X5R 6.3 V | 1206 | Murata GRM31CR60J107ME39L |
| SW1 | Tactile switch, SPST-NO | 6×6 mm THT | Any |
| J_OLED | 4-pin header 2.54 mm | THT | Any |
| J_RADAR | 4-pin header 2.54 mm | THT | Any |
| J_PIR | 3-pin header 2.54 mm | THT | Any |
| J_EXP | 5-pin header 2.54 mm (optional) | THT | Any |

---

## 11. Altium execution checklist

1. **Project.** `File → New → Project → PCB Project`, save as `hardware/altium/OmniSensor.PrjPcb`.
   Add `OmniSensor.SchDoc` and `OmniSensor.PcbDoc`.
2. **Library parts.** The passives and the SOT-23 come from Altium's standard libraries. For U1–U4 pull
   symbols and footprints from the manufacturer, SnapEDA or Ultra Librarian into a local
   `OmniSensor.SchLib` / `OmniSensor.PcbLib` in the same folder. Verify every footprint against the
   datasheet drawing — an imported footprint is a starting point, not an authority.
3. **Schematic.** Enter §3 net by net. Use net labels rather than long wires; place the power ports
   `+3V3_AO`, `+3V3_SW`, `VBAT`, `GND` explicitly so the two 3V3 rails can never be merged by accident.
4. **Annotate and ERC.** `Tools → Annotate`, then `Project → Validate`. Expect zero errors. Warnings
   about unconnected expansion pins are fine — mark them with No-ERC directives so the report stays
   clean and meaningful.
5. **Update PCB.** `Design → Update PCB Document`.
6. **Board outline and keep-outs.** Set the outline to the enclosure's internal footprint (from
   `enclosure/ENCLOSURE_SPEC.md`), add mounting holes, then add the XIAO antenna keep-out region.
7. **Placement** per §8, before any routing. Placement is the design; routing is bookkeeping.
8. **Thermal relief.** Draw the milled slots on the Mechanical/board-outline layer and add pour cut-outs
   around the SHT40 and SCD41.
9. **Route**, then pour the bottom ground plane, then **`Tools → Design Rule Check`**. Zero errors.
10. **Exports.** Use an OutJob so the settings are reproducible:
    - Gerber X2 + NC drill → `hardware/gerbers/`
    - Schematic PDF → `hardware/pdf/`
    - BOM CSV → `hardware/`
    Export to those folders directly, not to Altium's default `Project Outputs for ...`.
11. **Do not commit** the `History/` folder — it accumulates large zip blobs and is already excluded in
    `.gitignore`.

> **Line endings.** `.SchDoc` and `.PcbDoc` are OLE compound binary files. The repository's
> `.gitattributes` already marks them `binary` so Git never attempts CRLF conversion or a three-way
> merge on them — either would corrupt the file silently. Altium runs on Windows while the repo lives in
> WSL, so this matters. Do not change those rules.

---

## 12. Review checklist before exporting

- [ ] Radar header: XIAO TX goes to module RX, and the silkscreen says so
- [ ] Q1 source to `+3V3_AO`, drain to `+3V3_SW` — not reversed
- [ ] R7 present, gate to `+3V3_AO`
- [ ] Only the radar header is on `+3V3_SW`
- [ ] SCD41 has both its 100 nF and its 10 µF, placed at the pins
- [ ] No pour under the SHT40; relief slots actually break the copper path
- [ ] Antenna keep-out clear of copper
- [ ] Divider resistors are the 1 % parts, not the 5 % ones
- [ ] ERC and DRC both clean
