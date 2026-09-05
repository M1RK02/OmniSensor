# Hardware Design & Power Management

**Milestone 2 engineering document · OmniSensor**
Platform: Seeed Studio XIAO ESP32-C6 (single-core RISC-V)
Last revised: July 2026

This is the detailed engineering companion to [`../project_description.md`](../project_description.md).
Where that document summarises, this one records the reasoning and the numbers.

---

## 0. PCB design vs. wired prototype

Read this first, or the rest of the document is misleading.

The project produces **two hardware artifacts**, and only one of them physically exists:

| | PCB | Wired prototype |
|---|---|---|
| **Status** | Designed in Altium, **never fabricated** | Built and running |
| **Lives in** | `hardware/altium/`, `hardware/outputs/` | `hardware/prototype/` |
| **Purpose** | Documented design deliverable | The unit that gets demonstrated and measured |
| **Contains** | Every design decision below, laid out in copper | All sensors cabled to the always-on 3V3 rail; the load switch and battery divider exist in the PCB design and firmware only |

There was not enough time in the schedule for board fabrication and shipping, so the decision was made
early to treat the PCB as a design exercise and build the working unit with cables. Crucially, the
**power hardware is still physically built** — the load switch and battery divider exist in the Altium PCB design and are implemented in firmware. Sleep current and battery gauge figures are projections from datasheet values.

Anything described below as a layout or routing decision exists only in the Altium design. Anything
described as a measurement comes from the wired unit.

---

## 1. Firmware and power architecture

### Operating system

FreeRTOS, single-core. The Matter/Thread stack runs at high priority; the sensor, UI and battery tasks
run at medium-low priority so they cannot block the CPU and trip the Task Watchdog Timer.

This is not a theoretical concern. The SCD41 single-shot measurement takes roughly **5 seconds**. A
sensor task running at or above the network stack's priority would starve it for that entire window.

### Deep sleep — Sleepy End Device

The device operates as a Matter **SED** to maximise energy efficiency. The radio stays off most of the
time, waking periodically to poll the Border Router for queued messages.

Two events break that cycle and cause an **immediate** GPIO wake, followed by an unsolicited push of data
to the Thread network:

- presence detected by the PIR (or, once running, the mmWave radar)
- a physical button press

### RTC shared memory

State is preserved across deep sleep using `RTC_DATA_ATTR`. Sensor readings and system state are written
there before sleeping.

On wake, the ESP32-C6 performs a software reset and re-enters `app_main`. In that early phase the code is
still effectively **single-threaded** — the FreeRTOS scheduler is not yet doing anything interesting — so
the last known values can be read straight out of RTC memory and pushed to the OLED with no locking at
all. The user sees populated data instantly, instead of staring at a blank screen for the 5 seconds the
CO₂ sensor needs.

This is the whole reason the OLED sits on the always-on rail rather than the switched one.

### Concurrency and deadlock prevention

Once the FreeRTOS tasks are running, the free ride ends and shared memory access must be genuinely
thread-safe. Two approaches, in order of preference:

1. **Single-writer via queues (preferred).** Sensor and battery tasks publish readings onto a FreeRTOS
   queue; exactly one owner task performs every write to shared state. This removes the possibility of a
   deadlock rather than managing it.
2. **Mutexes with timeouts.** Where a mutex is unavoidable, it is always taken with a finite timeout,
   never `portMAX_DELAY`, so a stall surfaces as a logged error instead of a silent hang.

---

## 2. Load switching — sensor rail power

**Component:** SI2301 P-channel MOSFET, cutting power to the LD2420 radar during deep sleep.

### Fail-safe default (the important part)

The gate is held by an **external pull-up resistor**. A P-channel MOSFET with its gate pulled up to the
source is **off**.

This matters because the ESP32-C6 control pin does not always drive a defined level. During boot, after a
firmware crash, and throughout deep sleep when the GPIO drivers are powered down, that pin floats. With a
pull-down-biased design, a float could leave the sensor rail energised and quietly flatten the battery
over days with nobody noticing.

The rule applied here: **failure modes should fail toward low power.** A floating control pin costs a
missed reading, not a dead battery.

The SHT40 (~0.08 µA idle) and BH1750 (~0.01 µA powered down) remain on the always-on rail — their sleep currents are negligible. The SCD41 is parked with its own `power_down` command in firmware rather than being physically switched, which avoids I²C bus hazards: an unpowered SCD41 sharing the bus with the always-on OLED would be back-powered through its ESD diodes and could hold SDA low.

### Inrush stabilisation

A bulk capacitor of **100 µF or more** sits on the switched rail to absorb the LD2420's inrush current
at turn-on and keep the rail stable.

---

## 3. Battery monitoring (ADC)

### Voltage divider

Two **100 kΩ** resistors in series: V<sub>batt</sub> → ADC pin → GND.

The value is chosen for **leakage, not accuracy**. At a full charge of 4.2 V the divider continuously
bleeds:

```
I = 4.2 V / 200 kΩ ≈ 21 µA
```

That is the same order of magnitude as the entire target sleep current, which is precisely why it cannot
go any lower in resistance. Smaller resistors would give a stiffer source and a marginally better reading
while costing far more current — a bad trade for a battery device.

### Reading stabilisation

A **100 nF** capacitor is placed in parallel with the low-side (shunt) resistor.

The ESP32-C6's ADC uses a sample-and-hold capacitor that draws a brief charge current at the moment of
sampling. Driven directly from a 50 kΩ Thévenin source, that charge transfer would not settle within the
sampling window and every reading would come out low. The 100 nF acts as a local charge reservoir.

### ADC configuration

Internal attenuation is set to **−12 dB**, giving a usable input range up to roughly 3.1–3.3 V.

The divider halves the LiPo's maximum 4.2 V down to **2.1 V**, which places the reading in the middle of
the ADC's transfer curve — its most linear region — rather than compressed against either end.

### Software and calibration

- Use the **`esp_adc_cal`** API to apply the per-chip calibration values Espressif burns into the
  ESP32-C6's eFuses at manufacture. Raw ADC counts on this family are not accurate enough without them.
- Take an average of **32–64 readings** (multisampling).
- Perform that sampling **immediately after wake and before the Thread radio is enabled**. RF activity
  couples measurable noise onto the ADC input; sampling before the radio starts avoids the problem
  entirely rather than trying to filter it afterwards.

### Measured results

> **Projected values.** The load switch and battery divider are implemented in the PCB design; the breadboard prototype runs from USB-C without these circuits. The figures below are projections from datasheet values.
>
> | Measurement | Target | Measured |
> |---|---|---|
> | Deep sleep current, sensor rail off | 7–10 µA | projected |
> | Active current, all sensors powered | — | projected |
> | Divider reading vs. multimeter, 4.2 V | ±1 % | projected |
> | Divider reading vs. multimeter, 3.3 V | ±1 % | projected |

---

## 4. Layout and thermal isolation

*(Altium design only — not fabricated.)*

The environmental sensors are physically isolated from the XIAO ESP32-C6 module on the PCB. The module is
the dominant heat source on the board: the radio, the regulator and the CPU all sit inside it.

Two mitigations:

- **Physical separation.** The SHT40 and SCD41 are placed as far from the module as the outline allows.
- **Copper relief.** Milled slots and thermal cut-outs in the copper pour surround the temperature and
  humidity sensors, breaking the conduction path through the board itself. Copper is an excellent thermal
  conductor, which is exactly the problem here — without relief slots, the ground pour delivers the
  radio's waste heat straight to the sensor that is trying to measure room temperature.

A temperature sensor reading its own PCB's self-heating is worse than useless, because the error is
systematic and looks plausible.

Also specified in the layout:

- **External I²C pull-ups.** The current examples rely on the ESP32-C6's internal pull-ups. Those are
  weak — tens of kΩ — and marginal for a four-device bus with any meaningful trace capacitance. The PCB
  specifies proper external resistors. Final values depend on measured bus capacitance.
- **Decoupling.** Local ceramic capacitors at each device, plus the 100 µF+ bulk on the switched rail.

---

## 5. mmWave radar — gate logic

The LD2420 reports detection strength per distance **gate**. Gates are grouped into 0.7 m sectors to give
zone-based presence:

| Zone | Distance | Meaning |
|---|---|---|
| Zone 1 | < 0.7 m | At the device |
| Zone 2 | 0.7 – 1.4 m | Near field |
| Zone 3 | > 1.4 m | Room presence |

Each gate needs its **own sensitivity threshold**, because return signal strength falls off sharply with
distance — a single global threshold would either miss distant targets or produce constant false
positives up close. The Milestone 3 calibration routine tunes these against an empty room.

### Why two presence sensors

They are not redundant; they do different jobs:

| | SR602 (PIR) | LD2420 (mmWave) |
|---|---|---|
| Detects | Motion | Presence, including stationary |
| Distance | No | Yes, per gate |
| Power | Negligible | Needs the sensor rail up |
| Can wake from deep sleep | **Yes**, via GPIO | No |
| Role | **Trigger** | **Qualifier** |

The PIR wakes the device. The radar then determines which zone is occupied and holds occupancy true while
a motionless person remains — the exact case a PIR alone gets wrong, and the reason PIR-only lights
switch off on people sitting still.

---

## 6. Matter integration

Presence events and environmental data map onto standard Matter clusters, handled natively by the
ESP-Matter SDK. Using standard clusters means Apple Home and Home Assistant support the device with no
custom integration code.

The XIAO's built-in BOOT button (GPIO9) provides the Matter-mandated **factory reset** via long press, clearing fabric
credentials.

Open question: Occupancy Sensing carries a single boolean. Exposing three distance zones needs either
multiple endpoints or a manufacturer-specific cluster. Undecided — see `project_description.md` §17.

---

## 7. Enclosure integration

- **PIR Fresnel lens.** A dedicated holder positions the lens at its designed focal distance from the
  pyroelectric element. This is not adjustable after the fact: get the spacing wrong and detection range
  collapses, with no obvious symptom other than poor sensitivity.
- **mmWave mounting.** The radar is mounted **flush** with the case wall. Plastic in the beam path
  produces internal reflections that the radar reports as phantom targets — ghosting that would corrupt
  the zone logic with detections that never move.
- **SCD41 airflow.** Vents provide genuine air exchange with the room. A CO₂ sensor in a sealed
  enclosure measures the enclosure. Vent placement must also avoid drawing XIAO-warmed convection air
  across the SHT40.
- **Internal volume.** Sized for cabling, since the internals are a wired prototype rather than a flat
  PCB assembly.

---

## 8. Exporting from Altium

Keep the repository tidy and reviewable:

1. Export gerbers, drill files, BOM, STEP 3D model, and schematic PDF into **`hardware/outputs/`** via an OutJob so the outputs are reproducible.
2. The **PDF of the schematic** and **Gerber/STEP files** in `hardware/outputs/` allow the design to be reviewed by anyone without an Altium licence.
3. Do not commit Altium's `History/` folder. It accumulates large zip blobs and is already excluded in
   `.gitignore`.

> **Note on line endings.** `.SchDoc` and `.PcbDoc` are OLE compound binary files. The repository's
> `.gitattributes` marks them `binary` so Git never attempts line-ending conversion or a three-way merge
> on them — either would corrupt the file silently. This matters here because Altium runs on the Windows
> host while the repository lives in WSL. Do not remove those rules.
