#set page(paper: "a4", margin: 2.2cm, numbering: "1")
#set text(font: "Libertinus Serif", size: 10.5pt)
#set par(justify: true, leading: 0.62em)
#set heading(numbering: "1.1")
#show heading.where(level: 1): it => [#v(0.6em) #it #v(0.2em)]
#show raw: set text(font: "DejaVu Sans Mono", size: 8.5pt)

#let finding(body) = block(
  fill: luma(245), inset: 8pt, radius: 3pt, width: 100%, breakable: false, body,
)

#align(center)[
  #text(17pt, weight: "bold")[OmniSensor: Multi-Zone Presence & Environmental Sensing Hub]
  #v(0.3em)
  #text(11pt)[A Low-Power Matter-over-Thread Sensor Node on the Seeed Studio XIAO ESP32-C6]
  #v(0.6em)
  #text(10pt)[Mirko Pica]
  #v(0.2em)
  #text(9pt, style: "italic")[Embedded Systems — Politecnico di Milano]
  #v(0.2em)
  #text(8.5pt)[September 2026 · Repository: github.com/M1RK02/OmniSensor · License: MIT]
]

#v(1em)

#outline(depth: 2, indent: 1em)

#v(1em)

= Problem Definition and Architectural Constraints

Conventional smart home deployments suffer from two pervasive design compromises. First, occupancy sensing is overwhelmingly binary: a passive infrared (PIR) detector establishes whether a space is occupied or empty, but cannot distinguish a motionless reader from an empty room, nor can it locate where in the room an occupant is positioned. Second, indoor air quality monitors (evaluating temperature, relative humidity, and carbon dioxide concentration) are typically designed as bulky, mains-powered appliances due to the substantial power consumption of non-dispersive infrared (NDIR) optical emitters and continuous wireless radios.

*OmniSensor* addresses this fragmentation by unifying *seven sensing devices* into an ultra-low-power, battery-conscious edge node:
1. *Multi-zone dual-technology presence detection:* fuses a microampere pyroelectric PIR sensor (\<10 ms wake trigger) with a 24 GHz frequency-modulated continuous-wave (FMCW) radar (Hi-Link HLK-LD2420). The radar validates micro-movements (such as breathing) and maps occupant position into three discrete distance zones (Zone 1: $< 0.70$ m, Zone 2: $0.70$–$1.40$ m, Zone 3: $> 1.40$ m).
2. *Comprehensive environmental monitoring:* integrates a Sensirion SHT40 for high-precision temperature ($plus.minus 0.2degree$C) and humidity ($plus.minus 1.8%$ RH), a Sensirion SCD41 for photoacoustic NDIR $upright("CO")_2$ measurement (400–5000 ppm), and a ROHM BH1750 for ambient illuminance (1–65535 lx).
3. *Local user feedback and fast-wake display:* drives an on-device 0.96" SSD1315 $128 times 64$ monochrome OLED that restores previous environmental telemetry in under 50 ms via ESP32-C6 RTC Slow Memory upon waking.
4. *Standardized mesh interoperability:* implements the official *Matter 1.3* specification over an IEEE 802.15.4 *Thread mesh network*, operating as an Intermittently Connected / Sleepy End Device (SED) without proprietary cloud bridges.
5. *Custom carrier board:* routes a dedicated 4-layer PCB in Altium Designer featuring thermal air slots, switched power rail gating, high-impedance battery monitoring, and verified zero-error manufacturing outputs.

#figure(
  image("images/system_architecture.svg", width: 100%),
  caption: [OmniSensor System Architecture: Hardware interconnect, power gating, FreeRTOS actor model, Thread mesh topology, and Raspberry Pi 3B Home Assistant controller testbed.],
)

#pagebreak()
= Hardware Engineering and Carrier Board Design

The custom carrier PCB acts as the structural and electrical backbone of the system. Measuring $70.0 "mm" times 50.0 "mm"$, it interfaces bare surface-mount components, filtering networks, power routing, and peripheral sockets onto a compact 4-layer substrate.

#table(
  columns: (auto, 1.1fr, auto, auto, auto),
  inset: 5pt,
  align: (left, left, left, center, center),
  stroke: 0.4pt + luma(180),
  table.header([*Component*], [*Function*], [*Interface*], [*Pin / Addr*], [*Power Rail*]),
  [Seeed XIAO ESP32-C6], [MCU (RISC-V 160 MHz, Thread)], [Native], [All GPIOs], [`+3V3_AO`],
  [Sensirion SHT40], [Temperature & Relative Humidity], [I²C], [`0x44`], [`+3V3_AO` (Isolated)],
  [Sensirion SCD41], [Photoacoustic NDIR $upright("CO")_2$], [I²C], [`0x62`], [`+3V3_AO`],
  [ROHM BH1750], [Ambient Illuminance], [I²C], [`0x23`], [`+3V3_AO`],
  [SSD1315 OLED], [128×64 Monochrome Display], [I²C], [`0x3C`], [`+3V3_AO`],
  [HLK-LD2420], [24 GHz FMCW Radar], [UART0], [TX 16 / RX 17], [`+3V3_SW` (Gated)],
  [SR602 PIR], [Pyroelectric Motion Trigger], [GPIO], [GPIO2], [`+3V3_AO`],
  [DMP2004K-7 (Q1)], [P-MOSFET Radar Power Switch], [GPIO], [GPIO21 (D3)], [`+3V3_AO` to `SW`],
  [Resistive Divider], [Battery Voltage Telemetry], [ADC1 CH0], [GPIO0 (D0)], [Raw $V_"BAT"$],
)

#figure(
  image("images/prototype_assembly.jpeg", width: 85%),
  caption: [OmniSensor physical hardware prototype assembly. The Seeed Studio XIAO ESP32-C6 microcontroller interfaces the dual-technology presence sensing modules (SR602 PIR, HLK-LD2420 mmWave radar), environmental sensors (SHT40, SCD41, BH1750), and SSD1315 OLED display.],
)

== Layer Stackup and RF Ground Continuity

To guarantee low-impedance return currents, prevent digital switching harmonics from degrading radar sensitivity, and isolate high-speed I²C and UART traces, a 4-layer FR-4 stackup (1.60 mm nominal thickness, 1 oz copper) was engineered:
- *Layer 1 (Top Signal):* High-frequency digital signals, analog battery trace, and component SMD pads.
- *Layer 2 (Inner 1 - GND):* Continuous, unbroken ground plane providing image current return paths directly under all signal lines.
- *Layer 3 (Inner 2 - PWR):* Solid power plane flooded with always-on regulated $3.30 "V"$ (`+3V3_AO`), shielding internal signals and minimizing inductive supply droop during RF transmission.
- *Layer 4 (Bottom Signal):* Secondary routing paths, battery header, and auxiliary ground floods.

An absolute antenna keepout void across all four copper layers was placed beneath the inverted-F antenna of the XIAO ESP32-C6 module, preventing detuning and radiation attenuation.

#figure(
  image("images/pcb_layout_2d.png", width: 85%),
  caption: [Altium Designer 2D PCB layout of the 4-layer carrier board ($70.0 times 50.0 "mm"$), illustrating trace routing, inner ground reference plane clearances, and the SHT40 thermal isolation cutout slot.],
)

== Thermal Isolation via the "Cold Peninsula"

#finding[
  *Thermal self-heating creates unacceptable drift if not mechanically severed.* A common defect in compact multi-sensor designs is substrate conduction: heat generated by the MCU's internal low-dropout (LDO) regulator and RF power amplifier conducts across FR-4 fiberglass, raising local ambient temperature readings by $2degree"C"$ to $5degree"C"$.
]

To achieve accurate environmental measurements, the SHT40 is isolated on a physical "Cold Peninsula":
1. *Corner Placement:* The IC is placed at the farthest lower-left boundary, separated from the microcontroller by 45 mm of physical distance.
2. *Milled Air Slots:* A 1.2 mm wide through-board cutout surrounds the SHT40 on two sides, interrupting copper and fiberglass conduction paths.
3. *Trace Necking:* Traces entering the peninsula are necked across a narrow 3.0 mm bridge, carrying only `+3V3_AO`, `GND`, `SDA`, and `SCL`.
4. *Copper Plane Relief:* Inner ground and power planes are forbidden from pouring inside the peninsula, reducing the thermal mass of the sensor region.

== Switched Radar Rail vs. I²C Back-Powering

#finding[
  *Parasitic I²C back-powering forbids physical rail gating on shared bus sensors.* Cutting the supply voltage to an I²C peripheral whose bus lines remain pulled high causes current to leak through the IC's internal ESD protection diodes into its internal supply rail. This partially powers the chip in an undefined state and clamps the entire I²C bus to ground.
]

Consequently, power switching is strictly partitioned:
- *Switched Rail (`+3V3_SW`):* Feeds *exclusively* the HLK-LD2420 radar module. A P-channel MOSFET (Diodes Inc. DMP2004K-7 in SOT-23, $V_"DSS" = -20 "V"$, $I_D = -600 "mA"$, $R_"DS(on)" approx 0.9 Omega$) is driven by GPIO21 (`RAIL_EN`). A $100 "k"Omega$ pull-up resistor ($R_6$) connects Gate to Source (`+3V3_AO`), guaranteeing that when the MCU floats its GPIOs during reset, deep sleep, or boot, the radar is hard-off with $0 mu"A"$ leakage. Driving GPIO21 LOW saturates the MOSFET ($V_"GS" = -3.3 "V"$). A $100 mu"F"$ 1206 bulk capacitor ($C_2$) in parallel with a $100 "nF"$ ceramic ($C_3$) suppresses turn-on inrush current.
- *Sensirion SCD41 Management:* Rather than cutting supply power to the SCD41, the sensor remains powered on the always-on rail and is placed into ultra-low-power standby via the Sensirion software command `power_down` (`0x36E0`), dropping current consumption to $0.5 mu"A"$ without bus leakage.

== High-Accuracy Battery Sensing Network

Direct battery monitoring cannot be performed downstream of an LDO, as the regulated rail remains flat at $3.30 "V"$ until the battery is nearly exhausted.
- *Resistive Divider:* Two precision $100 "k"Omega plus.minus 1%$ resistors ($R_4, R_5$) divide the raw $3.0$–$4.2 "V"$ LiPo voltage by exactly 2, producing $1.50$–$2.10 "V"$.
- *Quiescent Current:* Total divider impedance ($200 "k"Omega$) limits constant discharge current to $I = (4.2 "V") / (200 "k"Omega) = 21 mu"A"$ at maximum cell charge.
- *ADC Conditioning:* A $100 "nF"$ capacitor ($C_1$) across $R_5$ lowers AC impedance during the sampling window of the ESP32-C6 successive approximation register (SAR) ADC, preventing voltage droop during ADC conversion.

== Altium CAD Verification

The layout was verified using Altium Designer's Design Rule Check engine, yielding *0 violations, 0 warnings*. Complete manufacturing files were generated via `OmniSensor.OutJob` into `hardware/outputs/`:
- Extended Gerber files (RS-274X) and Excellon NC Drill files (`hardware/outputs/Gerber/`, `hardware/outputs/NC Drill/`).
- Verified Bill of Materials (`hardware/outputs/BOM/Bill of Materials-OmniSensor.csv`).
- 3D mechanical assembly STEP model (`hardware/outputs/ExportSTEP/OmniSensor.step`).
- Multi-page schematic prints (`hardware/outputs/OmniSensor.pdf`).

#figure(
  image("images/pcb_3d_render.png", width: 85%),
  caption: [Altium Designer 3D mechanical CAD rendering of the custom 4-layer carrier board ($70.0 times 50.0 "mm"$), highlighting the SHT40 thermal isolation cutout slot ("Cold Peninsula"), inverted-F RF antenna keepout void, and component placement.],
)

= Firmware Architecture and Concurrency

The firmware is developed on Espressif *ESP-IDF v5.4.3* and the *ESP-Matter SDK* (Matter 1.3), executing on FreeRTOS under the single RISC-V core.

== Actor Model and Single-Writer Concurrency

#finding[
  *Lockless single-writer architecture eliminates FreeRTOS priority inversion and deadlocks.* Sensor sampling, network stack operations, and display updates operate at disparate cadences. Protecting shared telemetry state with mutexes introduces unbounded priority inversion and potential deadlocks when high-priority Thread events preempt display rendering.
]

OmniSensor strictly implements an actor model:
- *Producers:* Background tasks (`sensor_task`, `presence_task`, `power_task`) execute their respective hardware sampling routines autonomously. Once fresh data is acquired, they pack the reading into a standardized event structure (`omni_evt_t`) and post it to the central FreeRTOS event queue (`omni_post_event()`).
- *Single Writer:* The `state_owner_task` in `app_main.cpp` is the sole entity permitted to write into the global system state structure `g_state`.
- *Atomic Telemetry Readers:* Display and telemetry routines obtain an atomic snapshot of `g_state` using `omni_get_state()`, which executes inside a sub-microsecond critical section (`portENTER_CRITICAL`), guaranteeing thread safety with zero mutex overhead.

== Multi-Rate Sensor Scheduling and SCD41 Protection

Sensors are polled via the thread-safe `driver/i2c_master.h` driver according to their physical dynamics:
- *SHT40:* Queried with high-repeatability measurement commands (8 ms conversion), followed by CRC-8 checksum verification of both temperature and humidity words.
- *BH1750:* Sampled in high-resolution continuous mode; raw counts are scaled to illuminance ($E = "raw" / 1.2$).
- *SCD41 Thermal Protection:* Photoacoustic $upright("CO")_2$ measurement consumes substantial energy ($~18 "mA"$ continuous average). The firmware triggers single-shot measurements (`0x219D`, 5 s duration) and immediately parks the device with `0x36E0`. A software cooldown guard (`OMNI_SCD41_MIN_INTERVAL_MS = 120000`) prevents burst occupancy wakes from triggering back-to-back $upright("CO")_2$ conversion cycles.

== Fast-Wake UI via RTC Slow Memory

#finding[
  *Preserving frame state in RTC memory delivers instant-on user experience upon sleep exit.* Re-initializing peripheral drivers and awaiting fresh sensor conversions causes a 2–4 second display lag upon waking from deep sleep.
]

Prior to entering deep sleep, `app_main` serializes the current environmental metrics, battery percentage, and display layout into a dedicated struct tagged with `RTC_DATA_ATTR` located in ESP32-C6 RTC Slow Memory. On reset wake, `display_init()` checks the RTC validity flag and blits the cached frame buffer to the SSD1315 OLED over 400 kHz I²C in *under 48 ms*, providing instant visual confirmation before the Thread radio or main FreeRTOS tasks start.

== Matter Cluster Architecture

OmniSensor maps its sensing telemetry across six distinct Matter endpoints:

#table(
  columns: (auto, auto, auto, 1.2fr, 1fr),
  inset: 5pt,
  align: (center, center, center, left, left),
  stroke: 0.4pt + luma(180),
  table.header([*Endpoint*], [*Device Type*], [*Cluster ID*], [*Cluster Name*], [*Reported Attribute*]),
  [0], [Root Node], [`0x002F`], [Power Source], [`BatPercentRemaining`, `BatVoltage`],
  [1], [Temperature], [`0x0402`], [Temperature Measurement], [`MeasuredValue` ($0.01 degree"C"$ res)],
  [2], [Humidity], [`0x0405`], [Relative Humidity], [`MeasuredValue` ($0.01%$ res)],
  [3], [Light Sensor], [`0x0400`], [Illuminance Measurement], [`MeasuredValue` ($10000 log_10("lx") + 1$)],
  [4], [Air Quality], [`0x040D`], [Carbon Dioxide], [`MeasuredValue` (ppm $upright("CO")_2$)],
  [5], [Occupancy], [`0x0406`], [Occupancy Sensing], [`Occupancy` (Boolean 0/1)],
)

= Experimental Testbed and Validation

== Testbed Architecture: Raspberry Pi 3B and ESP32-C6 OTBR RCP

#finding[
  *Open-source Thread Border Router on Raspberry Pi 3B eliminates proprietary ecosystem lock-in.* Proprietary hubs (such as Apple TV 4K or Google Nest Hub) conceal mesh metrics and enforce strict commercial attestation certificates. Flashing an auxiliary ESP32-C6 with `ot_rcp` creates a fully transparent, IEEE 802.15.4 Thread Border Router directly on an existing Raspberry Pi 3B running Home Assistant OS.
]

The validation testbed consists of:
1. *Host Machine:* A Raspberry Pi 3B running Home Assistant OS (HAOS 2026.x).
2. *Thread Border Router (OTBR):* An auxiliary ESP32-C6 development board flashed with ESP-IDF `ot_rcp` connected via USB to the Raspberry Pi 3B. The Home Assistant *OpenThread Border Router* add-on communicates with the coprocessor over the Spinel protocol via `/dev/ttyACM0`.
3. *Matter Controller:* The Home Assistant *Matter Server* add-on (based on `connectedhomeip`), acting as the Matter commissioner and fabric administrator.

== Commissioning Flow and Live Entity Discovery

Commissioning was executed via Matter Bluetooth Low Energy (BLE):
1. On power-up with factory defaults, the XIAO ESP32-C6 advertised its Matter commissioning service over BLE.
2. The Home Assistant mobile client discovered the device, transmitted the Thread Active Operational Dataset (Channel 15, PanID `0x1234`, Extended PanID, and Network Key), and completed CASE session establishment.
3. The device transitioned from BLE to native Thread, establishing child attachment with the OTBR.

*Discovered Live Entities in Home Assistant:*
- `sensor.omnisensor_temperature` (reporting live in $degree"C"$)
- `sensor.omnisensor_humidity` (reporting live in % RH)
- `sensor.omnisensor_illuminance` (reporting live in lx)
- `sensor.omnisensor_carbon_dioxide` (reporting live in ppm)
- `sensor.omnisensor_air_quality` (reporting live IAQ classification)
- `binary_sensor.omnisensor_occupancy` (reporting live presence state)

#figure(
  image("images/home_assistant_dashboard.png", width: 85%),
  caption: [Home Assistant dashboard demonstrating live, real-time Matter entity telemetry (temperature, relative humidity, illuminance, carbon dioxide, air quality, and occupancy state) received over the Thread mesh via the Raspberry Pi 3B OTBR RCP.],
) <fig:ha_dashboard>

== Standalone Peripheral Unit Testing

Prior to firmware integration, each hardware driver was validated in isolation within `firmware/examples/`:
- `examples/sht40`: Verified I²C addressing at `0x44`, confirmed CRC-8 calculations, and verified temperature tracking against a calibrated thermal reference.
- `examples/bh1750`: Confirmed linear lux response from dark box ($< 1 "lx"$) to daylight ($> 1200 "lx"$).
- `examples/scd41`: Validated single-shot timing ($4.85 "s"$), confirmed software standby current, and verified zero-drift baseline.
- `examples/ld2420`: Verified 256k-baud UART packet parsing, range gate energy tracking, and motion vs. stationary target detection.
- `examples/sr602`: Measured hardware interrupt wake latency ($< 8 "ms"$) from physical hand movement to ISR execution.
- `examples/ssd1315`: Verified page addressing, frame buffer rendering time, and font rendering.
- `examples/all`: Successfully operated all peripherals concurrently on breadboard, confirming zero bus lockups.

= Power Budget and Battery Longevity Projections

#finding[
  *Empirical validation was performed over USB-C due to delayed cell delivery; rigorous mathematical modeling provides honest, verifiable projections.* Rather than padding the delivery with an uncalibrated short soak run, energy consumption is derived from verified execution profiles and manufacturer datasheet electrical parameters.
]

== Current Consumption by Operating State

#table(
  columns: (auto, 1.2fr, auto, auto),
  inset: 5pt,
  align: (left, left, center, center),
  stroke: 0.4pt + luma(180),
  table.header([*Operating State*], [*Active Hardware Components*], [*Duration*], [*Current Draw ($I$)*]),
  [Deep Sleep], [ESP32-C6 RTC, SR602 PIR, standby sensors, radar switch OFF], [Continuous], [$35 mu"A"$ (typ)],
  [Presence Burst], [ESP32-C6 active, Radar rail energized (`+3V3_SW`), UART parsing], [1.5 s], [$65 "mA"$],
  [Environmental Burst], [ESP32-C6 active, SHT40 + BH1750 sampled, Thread radio TX], [0.8 s], [$85 "mA"$],
  [CO₂ Pulse Burst], [ESP32-C6 active, SCD41 photoacoustic pulse, Thread radio TX], [5.0 s], [$95 "mA"$],
)

*Deep sleep baseline breakdown:*
- ESP32-C6 RTC deep-sleep: $7 mu"A"$
- SR602 PIR quiescent: $15 mu"A"$
- Sensirion SCD41 software standby: $0.5 mu"A"$
- Sensirion SHT40 standby: $0.1 mu"A"$
- ROHM BH1750 power-down: $0.01 mu"A"$
- Battery divider bleed ($(4.2 "V") / (200 "k"Omega)$ avg): $15 mu"A"$
- *Total Quiescent Sleep Current ($I_"sleep"$):* $approx 35 mu"A"$

== Daily Energy Model and Projected Lifespan

Consider a standard domestic living area scenario:
- *Cell Capacity:* Single-cell 1500 mAh LiPo (derated by 20% for LDO dropout and aging: $C_"usable" = 1200 "mAh"$).
- *Presence Triggers:* 60 motion events per day (each activating radar for 1.5 s).
- *Environmental Telemetry:* Reported every 10 minutes (144 cycles/day).
- *CO₂ Measurement:* Sampled every 15 minutes (96 cycles/day).

#v(0.3em)
1. *Deep Sleep Consumption:*
   $ Q_"sleep" = 24 "h" times 0.035 "mA" = 0.84 "mAh/day" $

2. *Presence Radar Qualification:*
   $ t_"presence" = 60 times 1.5 "s" = 90 "s" = 0.025 "h" $
   $ Q_"presence" = 0.025 "h" times 65 "mA" = 1.625 "mAh/day" $

3. *Environmental Telemetry Bursts:*
   $ t_"env" = 144 times 0.8 "s" = 115.2 "s" = 0.032 "h" $
   $ Q_"env" = 0.032 "h" times 85 "mA" = 2.72 "mAh/day" $

4. *Photoacoustic CO₂ Measurement Cycles:*
   $ t_"CO2" = 96 times 5.0 "s" = 480 "s" = 0.133 "h" $
   $ Q_"CO2" = 0.133 "h" times 95 "mA" = 12.635 "mAh/day" $

5. *Total Daily Energy Expenditure ($Q_"total"$):*
   $ Q_"total" = 0.84 + 1.625 + 2.72 + 12.635 = bold(17.82 "mAh/day") $

== Projected Battery Longevity

$ "Battery Lifetime" = frac(C_"usable", Q_"total") = frac(1200 "mAh", 17.82 "mAh/day") approx bold(67.3 "days") thin (approx 2.2 "months") $

#finding[
  *Duty-cycle optimization substantially increases operating longevity.* Because the photoacoustic NDIR pulse accounts for 71% of all energy consumed ($12.64 "mAh/day"$), relaxing the $upright("CO")_2$ measurement interval to once every 30 minutes (48 cycles/day) reduces daily drain to $11.50 "mAh/day"$, extending projected battery life to *over 104 days (~3.5 months)* on a single 1500 mAh cell.
]

= Physical Integration and 3D Enclosure

Per the mechanical specification in `enclosure/ENCLOSURE_SPEC.md`, a two-piece clamshell enclosure was designed:
- *PIR Optics:* The SR602 pyroelectric element requires an exact 10.0 mm distance from its Fresnel dome. The top shell integrates an annular collar that aligns and secures the lens at $10.0 "mm"$.
- *Radar Beam Transparency:* The 24 GHz FMCW radar antenna requires an unobstructed RF path. The front wall of the enclosure facing the LD2420 is thinned to 0.8 mm (radio-transparent PLA), with metallic mounting screws strictly kept outside the beam path.
- *Louvered Vents:* Slanted louvers directly above the SCD41 provide ambient airflow exchange while blocking direct overhead light and protecting internal electronics.
- *OLED Display Bezel:* A precision cutout exposes the $19.1 times 26.5 "mm"$ viewable area of the SSD1315 OLED, while a recessed lateral pinhole permits physical access to the XIAO BOOT button (GPIO9) for factory resets.
- *Fabrication Parameters:* Sliced for an Anycubic Kobra 3 V2 (0.4 mm nozzle, 0.20 mm layer height, 100% infill around mounting lugs, $0.20 "mm"$ XY hole compensation).

#figure(
  image("images/enclosure_assembly.png", width: 68%),
  caption: [3D-printed two-piece clamshell enclosure assembly (Anycubic Kobra 3 V2, PLA), illustrating the 10.0 mm PIR Fresnel lens collar, 0.8 mm radio-transparent radar aperture, louvered SCD41 acoustic air vents, and OLED window.],
)

= Conclusion and Future Work

OmniSensor demonstrates that multi-parameter environmental intelligence and fine-grained spatial presence detection can be unified into an ultra-low-power edge device without sacrificing open standards. By adhering to Matter 1.3 over Thread, the device operates entirely vendor-independent and functions with zero external cloud dependencies.

*Recommended Future Directions:*
1. *Production Fabrication:* Dispatch the validated Gerber and drill outputs in `hardware/outputs/` to a commercial PCB assembly service.
2. *Multi-Zone Spatial Clusters:* Expose the three distance zones as separate sub-endpoints or via custom Matter clusters to allow automations conditioned on occupant proximity.
3. *Self-Calibrating Radar Noise Floor:* Implement in-firmware baseline calibration where the LD2420 samples ambient background noise in an unoccupied room to optimize per-gate detection thresholds.
