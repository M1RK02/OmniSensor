# OmniSensor

**A battery-powered, multi-zone presence and air-quality sensor — Matter over Thread, built on the Seeed Studio XIAO ESP32-C6.**

OmniSensor packs seven sensing devices into a single low-power node. It reports temperature, humidity,
CO₂ and ambient light to any Matter ecosystem (Apple Home, Home Assistant, Google Home) while a PIR and a
24 GHz mmWave radar work together to detect *where* in a room someone is — not just whether the room is
occupied. A local OLED shows the current readings without needing a phone.

The design target is a device that spends almost all of its life in deep sleep, drawing single-digit
microamps, and wakes instantly when someone walks in.

> **Status:** Integrated firmware running. Matter commissioned against Home Assistant.
> Altium 4-layer carrier board design and manufacturing outputs complete.
> Enclosure 3D modeling in progress. See [docs/DELIVERY_CHECKLIST.md](docs/DELIVERY_CHECKLIST.md) for the schedule.

---

## Hardware

| Part | Function | Interface | Address / Pin |
|---|---|---|---|
| Seeed XIAO ESP32-C6 | MCU — RISC-V, Wi-Fi 6, 802.15.4 (Thread) | — | — |
| Sensirion SHT40 | Temperature + humidity | I²C | `0x44` |
| Sensirion SCD41 | CO₂ (photoacoustic NDIR) | I²C | `0x62` |
| ROHM BH1750 | Ambient light (lux) | I²C | `0x23` |
| SSD1315 | 128×64 monochrome OLED | I²C | `0x3C` |
| Hi-Link LD2420 | 24 GHz mmWave presence radar | UART | TX 16 / RX 17 |
| SR602 | PIR motion (deep-sleep wake source) | GPIO | `GPIO_NUM_2` |
| Diodes Inc. DMP2004K / SI2301 | P-MOSFET radar power switch | GPIO | D3 (GPIO21) |
| BOOT button | Factory reset + refresh | GPIO | GPIO9 (on-module) |

Shared I²C bus on **SDA 22 / SCL 23**. Full pin map and electrical design in
[project_description.md](project_description.md) and [docs/HARDWARE_DESIGN.md](docs/HARDWARE_DESIGN.md).

## Repository layout

```
OmniSensor/
├─ firmware/
│  ├─ hub/                  # integrated OmniSensor firmware (Matter over Thread)
│  └─ examples/             # standalone, one-sensor-at-a-time validation projects
│     ├─ sr602/             # PIR + GPIO deep-sleep wakeup
│     ├─ sht40/             # temperature + humidity, CRC checked
│     ├─ bh1750/            # ambient light
│     ├─ scd41/             # CO2 single-shot measurement
│     ├─ ld2420/            # mmWave radar over UART
│     ├─ ssd1315/           # OLED bring-up
│     └─ all/               # all seven together, with RTC-memory fast wake
├─ hardware/
│  ├─ altium/               # Altium Designer project files (.PrjPcb, .SchDoc, .PcbDoc, .OutJob)
│  ├─ datasheets/           # component reference sheets
│  └─ outputs/              # Gerbers, NC drill, BOM CSV, 3D STEP, schematic PDF
├─ enclosure/               # 3D-printable case (CAD sources + STLs)
└─ docs/                    # software architecture, hardware design, delivery schedule
```

## Quick start

Requires **ESP-IDF v5.4.3** targeting `esp32c6`. Full toolchain setup — including the Matter SDK —
is in [docs/SOFTWARE_DESIGN.md](docs/SOFTWARE_DESIGN.md).

```bash
get_idf                                   # activates ESP-IDF (alias, see docs/SOFTWARE_DESIGN.md)
cd firmware/examples/all
idf.py set-target esp32c6
idf.py build flash monitor
```

Each example under `firmware/examples/` is a self-contained ESP-IDF project and builds the same way.
Start with `sht40` if you just want to confirm your toolchain and wiring work.

## Documentation

| Document | Contents |
|---|---|
| [project_description.md](project_description.md) | Full technical description: architecture, BOM, pin map, power budget, Matter mapping |
| [docs/SOFTWARE_DESIGN.md](docs/SOFTWARE_DESIGN.md) | Firmware architecture, concurrency, presence/sensor subsystems, Matter model, and setup guide |
| [docs/HARDWARE_DESIGN.md](docs/HARDWARE_DESIGN.md) | Power management, load switching, battery sensing, PCB and enclosure design |
| [docs/DELIVERY_CHECKLIST.md](docs/DELIVERY_CHECKLIST.md) | Project schedule, scope, milestones and delivery plan |

## References

* [Seeed Studio XIAO ESP32-C6 Wiki](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/)
* [ESP-Matter SDK Documentation](https://docs.espressif.com/projects/esp-matter/en/latest/esp32/)
* [ESP-IDF Programming Guide (ESP32-C6)](https://docs.espressif.com/projects/esp-idf/en/v5.4.3/esp32c6/)

## Contact

Contributions, questions and suggestions are welcome — send me an
[email](mailto:picamirko02@gmail.com).

## License

Released under the MIT license. See [LICENSE](LICENSE).
