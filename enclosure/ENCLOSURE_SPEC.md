# OmniSensor Enclosure Build Specification

This document describes how to model, print, and assemble the enclosure around the measured modules in `DIMENSIONS.md`. It is a parametric build specification: derive the final outside dimensions from the actual board placement in CAD rather than guessing a universal box size.

## 1. Enclosure Architecture

Build the enclosure as a two-piece clamshell:

- **Front shell:** carries the OLED window, PIR lens, light-sensor openings, radar sensing face, and user button.
- **Rear shell:** carries the XIAO ESP32-C6 and sensor boards on internal standoffs, provides cable routing, and closes the wiring cavity.
- **Perimeter joint:** use a continuous tongue-and-groove or stepped overlap. Do not rely on the printed edge alone to locate the two halves.
- **Fasteners:** use four M3 screws into captive nuts or heat-set inserts, one near each corner. Keep the fasteners outside the PCB footprints and cable path.

Keep the front and rear shells removable. The USB-C cutout, sensor openings, and display window must remain accessible without disassembling the sensor boards.

## 2. Establish the CAD Layout

1. Create a master XY sketch for the enclosure footprint and a Z datum on the inside of the rear shell.
2. Place the SSD1315 OLED first because its 27.7 x 27.4 mm board defines the display opening and occupies the largest flat face.
3. Place the XIAO ESP32-C6 behind the front panel with its USB-C edge aligned to the USB-C wall cutout. Reserve the full 21 x 17.5 mm board outline and 5 mm maximum component height.
4. Place the SCD41 with its 21.6 x 13.4 mm board parallel to the front shell and its 8.1 mm maximum height toward the enclosure interior. Put the gas inlet on the top side and give it a direct opening to ambient air.
5. Place the LD2420 so the antenna/front face points through the detection side of the enclosure. Keep the antenna-facing wall radio-transparent; do not place a metal insert over that face. Add a metal shield behind the PCB only if rear detection must be suppressed.
6. Place the BH1750 with its sensor window facing the light opening. The board is 18 x 14 mm and 2 mm high; confirm which face contains the window before finalizing the opening.
7. Place the SHT40 at an exposed edge or vent location. Its measured board value is 1 x 1.3 mm, 1.2 mm high, with one corner M3 mounting hole; verify this unusually small footprint against the physical breakout before modelling the boss.
8. Place the SR602 on the front shell with the circular 16 mm body concentric to a lens bore. The lens dome is 10 mm in diameter and rises 10 mm above the PCB; preserve that focal distance.

Use the real connector orientations when creating the board footprints. The connector sides are: XIAO USB-C edge, SHT40 bottom, SCD41 short edge, BH1750 short edge, SSD1315 top edge, LD2420 along one edge, and SR602 bottom.

## 3. Internal Clearance and Shell Construction

- Set the board pockets to the measured board outlines plus a small assembly clearance. Start with 0.3 mm per side and adjust after a fit test.
- Set every component pocket to the measured maximum height plus 0.5 mm vertical clearance. The tallest measured board component is the SCD41 at 8.1 mm; the wiring cavity must also clear the 14 mm Dupont/JST connector stack.
- Size the internal height from the actual board datum, connector stack, and shell wall thickness. The 14 mm connector stack, not the sensor boards, is the controlling cable-clearance dimension.
- Provide at least 4 mm of free routing diameter around the typical wire bundle. Add fillets or rounded cable channels rather than forcing wires against sharp shell corners.
- Use 2.0 to 2.5 mm nominal shell walls. Add local ribs around screw bosses, connector cutouts, and the OLED opening instead of making the whole shell unnecessarily thick.
- Add 0.5 to 1.0 mm edge relief around boards and connector bodies so tolerance does not preload the PCBs.
- Keep component tops from touching the lid. The measured maximum heights are: XIAO 5 mm, SHT40 1.2 mm, SCD41 8.1 mm, BH1750 2 mm, SSD1315 1.1 mm, LD2420 1.2 mm, and SR602 11.6 mm.

## 4. Openings and Front Features

### OLED window

- Cut the display window to the active glass area: 19.1 x 26.5 mm.
- Centre the glass horizontally in the 27.4 mm board width. The measured left and right offset is 0.45 mm.
- Position the window vertically with the measured Y offset of 4.3 mm from the board edge.
- Add a 0.3 mm perimeter clearance around the active area, then add a shallow 0.5 mm recess for a cover lens or transparent panel if one is used.
- Keep all four M2 corner mounting holes accessible from the internal side; do not use the active glass as a structural surface.

### USB-C opening

- Cut the wall opening for the connector at 8.94 x 3.16 mm.
- Locate the opening so the connector centre is 1.6 mm above the XIAO PCB plane.
- Add 0.3 mm clearance around the measured connector envelope. Confirm cable-plug clearance separately; the connector shell measurement does not define the full plug envelope.

### Sensors

- Make the SR602 lens bore 10 mm nominal before printer compensation. Keep a 10 mm clear vertical distance from the lens reference to the PCB mounting plane.
- Give the SCD41 gas inlet its own open vent above the filter. Do not cover the inlet with a sealed window or route warm electronics exhaust across it.
- Give the BH1750 a direct light opening aligned to the confirmed sensor-window face. Use a thin protective grille only if it does not shade the window.
- Give the LD2420 an unobstructed opening or thin non-metallic wall toward the detection area. Keep metal, wiring bundles, and mounting hardware away from the antenna face.
- Vent the SHT40 to ambient air while protecting it from direct splash and from heat generated by the XIAO or voltage regulators.

### Button

- Provide an internal boss for the 6 x 6 x 5 mm button body.
- Align the plunger with a front-panel aperture and allow the measured 0.25 mm plunger travel without the button body carrying shell load.
- Add a small internal shoulder to retain the button, leaving enough clearance for the switch leads and wiring.

## 5. Mounting Details

- Use the SSD1315 four corner M2 holes for a dedicated display bracket or four printed posts. Keep the posts below the board and avoid the active glass area.
- Use the two BH1750 holes, approximately 3 mm diameter and opposite the connector, for two locating posts or M3 clearance holes after confirming their spacing.
- Use the SHT40 corner M3 hole only after verifying the breakout dimensions and hole location in hand.
- The XIAO, SCD41, LD2420, and SR602 have no measured mounting holes. Retain them with printed edge clips, shallow pockets, or a removable bracket; do not glue the sensor faces into the shell.
- Add a positive board stop in each pocket so screw tightening or cable insertion cannot shift sensor alignment.
- Keep all retaining features removable for firmware access and sensor replacement.

## 6. Print and Fit Procedure

1. Print a 20 mm calibration cube containing 3 mm and 4 mm holes. Measure the printed holes before committing to the enclosure.
2. Start with 0.20 mm XY hole compensation based on the measured printer tolerance. Apply the correction to screw holes and bores, not to the OLED active opening or sensor clearances without checking fit.
3. Print the rear shell with its PCB pockets and the front shell with its openings as separate parts. Print the front face flat when possible so the OLED window remains dimensionally stable.
4. Use a 0.4 mm nozzle and PLA on the Anycubic Kobra 3 V2. Orient screw bosses so their layers resist the screw pull direction.
5. Dry-fit every board, connector, button, and lens before installing fasteners. Check that the USB-C plug inserts fully, the OLED glass is not loaded, and the SR602 lens still has 10 mm focal spacing.
6. Check that the SCD41 inlet, BH1750 window, SHT40 vent, and LD2420 antenna all face their intended openings.
7. Route the wire bundle with at least its typical 4 mm diameter clearance and verify the 14 mm connector stack does not press against the lid.
8. Install inserts or nuts, assemble the clamshell, and perform a final sensor and button test before sealing the joint.

## 7. Dimensions Still Required Before Final CAD

The source measurements are sufficient to define the component envelopes and openings, but not the final outside length, width, shell split location, hole spacing, or board-to-board coordinates. Before producing the final CAD/STL files, measure or decide:

- overall board placement and centre-to-centre spacing;
- the exact BH1750 hole spacing and the SHT40 hole position;
- wall thickness and the desired external form factor;
- screw size and exact corner-hole locations;
- the final shell overlap, insert dimensions, and gasket or seal requirement;
- the confirmed BH1750 window face and the full USB-C plug envelope.
