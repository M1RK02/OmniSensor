# Development Environment Setup

OmniSensor targets the **ESP32-C6** and is built with **ESP-IDF v5.4.3**. The integrated firmware
additionally needs the **ESP-Matter SDK**; the standalone examples under `firmware/examples/` do not, so
if you only want to run those you can stop after step 3.

> **Version pinning matters.** The firmware uses the `driver/i2c_master.h` bus API introduced in
> IDF 5.2 and will not compile against older releases. ESP-Matter is also sensitive to the IDF version it
> was checked out against — mixing versions is the most common cause of build failures.

## 1. Install prerequisites

**macOS**

```bash
brew install cmake ninja dfu-util ccache python
```

**Debian / Ubuntu / WSL**

```bash
sudo apt-get update
sudo apt-get install -y git wget flex bison gperf python3 python3-pip python3-venv \
                        cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
```

## 2. Install ESP-IDF v5.4.3

```bash
mkdir -p ~/esp
cd ~/esp
git clone -b v5.4.3 --recursive https://github.com/espressif/esp-idf.git
```

Install the toolchain for the ESP32-C6 only — this skips downloading toolchains for every other
Espressif target and saves several gigabytes:

```bash
cd ~/esp/esp-idf
./install.sh esp32c6
```

Activate the environment in the current shell:

```bash
source ./export.sh
```

## 3. Install the ESP-Matter SDK

Only required for `firmware/hub/`. This is a large checkout; the flags below keep it as small as
possible.

```bash
cd ~/esp
git clone --depth 1 https://github.com/espressif/esp-matter.git
cd esp-matter
git submodule update --init --depth 1
cd ./connectedhomeip/connectedhomeip
./scripts/checkout_submodules.py --platform esp32 linux --shallow
cd ../..
./install.sh --no-host-tool
```

`--no-host-tool` skips building the host-side Matter tools (`chip-tool` and friends). If you later want
to commission or debug the device from the command line rather than from Apple Home or Home Assistant,
re-run `./install.sh` without that flag.

## 4. Shell aliases

Sourcing the export scripts by hand gets old quickly. Add these to `~/.zshrc` (macOS) or `~/.bashrc`
(Linux / WSL):

```bash
alias get_idf='export IDF_CCACHE_ENABLE=1 && source ~/esp/esp-idf/export.sh'
alias get_matter='get_idf && source ~/esp/esp-matter/export.sh'
```

Then `get_idf` prepares a plain ESP-IDF environment, and `get_matter` prepares ESP-IDF **and** Matter.
`IDF_CCACHE_ENABLE=1` makes rebuilds substantially faster.

Reload your shell (`exec $SHELL`) for the aliases to take effect.

## 5. Build, flash and monitor

Every project under `firmware/examples/` is a self-contained ESP-IDF project. From the repository root:

```bash
get_idf
cd firmware/examples/sht40
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

`set-target` only needs running once per project; it is what generates the local `sdkconfig` from the
committed `sdkconfig.defaults`. Exit the monitor with **Ctrl+]**.

Serial port naming:

| Platform | Typical port |
|---|---|
| Linux / WSL | `/dev/ttyACM0` |
| macOS | `/dev/cu.usbmodem*` |
| Windows | `COM3`, `COM4`, … |

Omitting `-p` lets `idf.py` autodetect, which usually works when only one board is attached.

### Suggested order

Start with `sht40` — it is the simplest I²C device and confirms your toolchain, wiring and pull-ups are
sound. Then `bh1750`, `scd41`, `ssd1315`, `sr602`, `ld2420`, and finally `all` for everything together.

## 6. Build the integrated firmware

```bash
get_matter                # note: get_matter, not get_idf
cd firmware/hub
idf.py set-target esp32c6
idf.py build flash monitor
```

## Troubleshooting

**Board is not detected.** The XIAO ESP32-C6 exposes a native USB-Serial/JTAG interface, so no external
adapter is needed. If it does not enumerate, hold **BOOT**, tap **RESET**, release **BOOT** to force the
bootloader. On Linux, add yourself to the `dialout` group (`sudo usermod -aG dialout $USER`) and log out
and back in.

**WSL cannot see the serial port.** WSL2 does not pass USB through by default. Use
[usbipd-win](https://github.com/dorssel/usbipd-win) to attach the device, or build in WSL and flash from
Windows.

**I²C device not found.** Check that SDA is on pin 22 and SCL on pin 23, and that the device is powered.
The examples enable the ESP32-C6's internal pull-ups, which are weak — with several devices or long
wires you may need external 4.7 kΩ resistors.

**`i2c_master.h` not found.** Your ESP-IDF is older than v5.2. Check with `idf.py --version`.

**Matter build fails after updating ESP-IDF.** The Matter SDK is bound to the IDF version it was
installed against. Re-run `./install.sh --no-host-tool` in `~/esp/esp-matter`.

**Commissioning finds no device.** Matter over Thread requires a Thread Border Router on your network —
an Apple TV 4K, a HomePod mini, or a Home Assistant SkyConnect/Yellow. A Matter controller alone is not
enough.
