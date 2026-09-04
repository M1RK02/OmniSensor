# Module dimension survey

**Fill this in with calipers before the enclosure is modelled.** Every number below feeds directly into
`ENCLOSURE_SPEC.md`; guessing them produces a case that does not close. Measure the *module* — the
breakout PCB you actually own — not the bare sensor from the datasheet, because breakout outlines vary
between vendors for the same part.

Measure to 0.1 mm. Where a dimension is a hole pattern, give centre-to-centre spacing.

## What to measure

| # | Module | Board L × W (mm) | Max height incl. parts (mm) | Mounting holes (Ø, spacing) | Connector side | Notes |
|---|---|---|---|---|---|---|
| 1 | XIAO ESP32-C6 |21x17.5|5| None| USB-C edge | Height must include the USB-C shell |
| 2 | SHT40 breakout | 1x1.3| 1.2| 1 on a corner M3| Bottom side| |
| 3 | SCD41 breakout | 21.6X13.4|  8.1| None|On the short edge |The gas inlet is the white rectangular filter on top of the metal cube. It faces upwards (perpendicular to the top face of the board). |
| 4 | BH1750 breakout |18 x14| 2| 2 holes (approx. 3mm Ø) opposite connector| Short edge| Note which face the sensor window is on |
| 5 | SSD1315 OLED | 27.7x27.4|1.1 |4 holes (one in each corner M2 screws) |Top edge | The active glass area is 26.5 × 19.1 mm. By subtracting the glass width from the total board width (27.4 - 26.5 = 0.9 mm), we can determine the horizontal offset is exactly 0.45 mm from the left and right edges. The vertical offset isn't explicitly dimensioned, but the glass sits between the top pins and the bottom cutout. |
| 6 | LD2420 radar | 20x20| 1.2| None| Along one edge| The antenna is on the top/front face of the board, which needs to be pointed toward the detection area. Because radar waves can penetrate the back of the PCB, it is recommended to shield the back with a metal plate if you want to avoid detecting movement behind the sensor.
| 7 | SR602 PIR | 16 diameter circular| 11.6| None|Bottom | Lens height above board: 10 mm Lens dome diameter: 10 mm |

## Additional measurements

| Item | Value | Why it matters |
|---|---|---|
| SR602 lens dome outer diameter | 10mm| Sets the bore of the lens holder |
| SR602 lens height above PCB | 10mm| This *is* the focal spacing — it must not change in the case |
| OLED active glass area (L × W) | 19.1x26.5mm| Sizes the display window |
| OLED glass offset from board edge (X, Y) | X:0.45mm, Y:4.3mm| Positions the window relative to the mounting holes |
| USB-C connector width × height | 8.94x3.16mm| Sizes the charging cutout |
| USB-C centre height above XIAO PCB | 1.6mm| Positions the cutout vertically |
| Dupont/JST connector stack height | 14mm| Cable clearance — this is what makes the case tall |
| Typical wire bundle diameter | 4mm| Internal routing volume |
| XIAO BOOT button position | On top face, near USB-C end | Enclosure needs an access hole or actuator |

## Print parameters

| Item | Value |
|---|---|
| Printer model | Anycubic Kobra 3 V2|
| Nozzle diameter |0.4 mm |
| Filament |PLA |
| Typical XY hole tolerance on this printer (measured) | 0.20mm|

The last row matters more than it looks: it decides whether mounting holes are modelled at nominal size
or oversized. If you have not characterised it, print a 20 mm calibration cube with 3 mm and 4 mm holes
and measure them — 10 minutes now saves a failed 6-hour print on Friday.
