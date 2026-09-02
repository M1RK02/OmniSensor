# Module dimension survey

**Fill this in with calipers before the enclosure is modelled.** Every number below feeds directly into
`ENCLOSURE_SPEC.md`; guessing them produces a case that does not close. Measure the *module* — the
breakout PCB you actually own — not the bare sensor from the datasheet, because breakout outlines vary
between vendors for the same part.

Measure to 0.1 mm. Where a dimension is a hole pattern, give centre-to-centre spacing.

## What to measure

| # | Module | Board L × W (mm) | Max height incl. parts (mm) | Mounting holes (Ø, spacing) | Connector side | Notes |
|---|---|---|---|---|---|---|
| 1 | XIAO ESP32-C6 | | | | USB-C edge | Height must include the USB-C shell |
| 2 | SHT40 breakout | | | | | |
| 3 | SCD41 breakout | | | | | Note which face the gas inlet is on |
| 4 | BH1750 breakout | | | | | Note which face the sensor window is on |
| 5 | SSD1315 OLED | | | | | Also measure the **active glass area** and its offset from the board edges |
| 6 | LD2420 radar | | | | | Note which face the antenna is on |
| 7 | SR602 PIR | | | | | Measure the **lens dome diameter** and **lens height above the board** separately |

## Additional measurements

| Item | Value | Why it matters |
|---|---|---|
| SR602 lens dome outer diameter | | Sets the bore of the lens holder |
| SR602 lens height above PCB | | This *is* the focal spacing — it must not change in the case |
| OLED active glass area (L × W) | | Sizes the display window |
| OLED glass offset from board edge (X, Y) | | Positions the window relative to the mounting holes |
| USB-C connector width × height | | Sizes the charging cutout |
| USB-C centre height above XIAO PCB | | Positions the cutout vertically |
| Dupont/JST connector stack height | | Cable clearance — this is what makes the case tall |
| Typical wire bundle diameter | | Internal routing volume |
| Button body L × W × H, plunger travel | | Button aperture and boss depth |

## Print parameters

| Item | Value |
|---|---|
| Printer model | |
| Nozzle diameter | |
| Filament | |
| Typical XY hole tolerance on this printer (measured) | |

The last row matters more than it looks: it decides whether mounting holes are modelled at nominal size
or oversized. If you have not characterised it, print a 20 mm calibration cube with 3 mm and 4 mm holes
and measure them — 10 minutes now saves a failed 6-hour print on Friday.
