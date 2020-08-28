Building Hextech Mechahand mk 10
================================

Components and tools
--------------------

Buy all the parts required in the bill of materials.

Order mostly assembled driving PCBs using the gerber files from `24drive`. Designed for assembly by jlcpcb.com, the
pick and place and electronics bill of materials are also available in the required formats. Manual assembly of the
driving PCB would require separately purchasing and soldering surface mount components. Machine assembly is better!

You should also prepare the following tools, and know how to use them:
* Protective glasses! PLA and nylon wires break suddenly and throw shards.
* 3D printer. Components are designed to be printed with 0.4mm nozzle FDM printer. Resin printers should also work.
* Soldering iron, solder, flux, and bronze sponge. Position encoders are embedded in plastic and soldered in place, practice fast soldering.
* Voltmeter. Need to test all connections after soldering, especially the strain gauges.
* LiPo charger and preferably a LiPo fireproof safe.
* Pliers, cutters, and tweezers. Multiple types of pliers recommended; flat for placing parts, round for handling wires, sharp for cutting tough nylon braid.
* Screwdriver or hex key with M3 head. Preferably a motorized screwdriver, there's going to be a lot of screwing.
* Crimping tool.
* Hammer!


Electronics
-----------

Order assembled PCBs from jlcpcb.com or your choice of supplier using design files in the `24driver/PCB`
directory. Place and solder the ESP32, the OLED screen andthe button on the populated side of the PCB.
Trim the legs of the Buck converter and solder it to the bottom side. Skip the capacitor bank! Place and
solder 6 JST PH 3-pin connectors for the pressure sensors, leave the 6 center-right connectors free and
cover with electric tape; the wrist roll motor will rest directly underneath this space. Place and solder
24 JST PH 2-pin and 3-pin connectors for the 6 rows of position and motor channels.


Mechanics
---------

Print all 3D parts from the latest `hand_model`. Can use the gcode directly for a Mk3 Prusa printer, or
the `3mf` design files. Need 5 fingers and finger skins, 5 motor systems, 2 mini worm joints, 2 worm joints,
and 1 of everything else.


### 1. Case

1. Place the plastic button cover on the electronic button and place the board into the PCB case with the hexagonal decal.
2. Slide the case and case shelf piece together and fix with 4 8mm hex screws and embedded nuts.

TODO: finish instructions...






