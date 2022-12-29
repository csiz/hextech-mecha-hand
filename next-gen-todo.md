Next generation hand ideas
==========================

For next generation hand we also need to consider manufacturing challenges. Need to greatly simplify wiring and assembly. Need a specialized tool for tendon tensioning too, as this cannot automated soon (possible challenge for hands themselves?).

[v] Is it possible to do gimbal/ball joint design similar to the CoreXY 3D printer design? The idea is to have a belt going around the pitch axis and tying on the yaw axis. Driving both belts together results in desired motion. Similar driving design as the differential bevel gears, however a bevel gear wouldn't work in the hand as it will hook on the tendons and require in place motors.
Done and moved on to differential bevel gears, a lot simpler to wire up and assemble.
[v] Switch to coreless DC motors for quieter operation and greatly simplified circuitry. There's no way to output max torque from brushless motors at standstill without a position encoder on the motor shaft. Speed at which it starts working is about 1Hz electrical rotation. Coreless motors are lighter and quieter and should work just fine.
Coreless is near mandatory for backdrivabialiy.
[x] Resin printed miniature cycloidal gear for the hand motors would be cool. Using the inner pivots as the output shaft with bearings on either side of spindle. Could manufacture it using thin bolts with brass spacers as the critical cycloid structure. Integrated spindle with the output shaft for compact dimensions even if bigger motor.
Doesn't work, very hard to manufacture something that thin and it needs to be made of metal almost guaranteed.
[x] 4 bigger motors for wrist, thumb pivot and hand roll; can be wired in parallel to the 4 extra driver outputs.
[v] Alternatively 4 extra motors physically connected to each other as the 2 sides of the spindle. Tendon wires go out the middle and to the wrist and hand roll joints. Tensioning done by driving the
Yep, works good.
motors in opposite sides then sliding and fixing the motors together.
[v] 4 spare inputs put on the tip joints (distal-medial) of non-thumb fingers. Tips are connected to driven joint (medial-proximal) by a spring. Difference in joint angles should be an accurate measure of tip force. More accurate then motor current feedback, will work passively too.
OMG it works!
[x] Silicone cast fingertips with embedded pressure sensors. Pressure sensors arranged for maximum sensitivity to friction forces rather than pressure. Perpendicular sensors to sense direction of friction force. Tip pressure deduced from tip position encoder difference, and secondarily from motor current use (but with errors from tendon/gearbox friction...).
Nope, got a different sort of pressure sensors in.
[v] Switch to hall effect position sensors. 4 position sensors + 2 custom in-design resistive strain gauges on a flexible PCB per finger with long ribbon ending that can plug into palm PCB. Ribbon cable between palm PCB with position sensing and driver PCB on forearm (also potentially a long cable for wrist rotation if placed on the mount to the upper arm). Same price overall with easier finger assembly. Significantly fewer cables. Allows waterproofing the fingers, together with moving wrist motors up to forearm, allows waterproofing entire hand.
Yep yep yep.
[x] Free space between hall effect sensor and magnet allows for efficient tendon pathing.
No need.
[?] Need inline resistance for the turn on diode of the driver PCB to absorb capacitor bank charging power.
[v] Single ribbon cable between palm and wrist sensors to driver hand.
Works perfectly, needs proper channel to pass through nicely so it doesn't get pinched.
[v] CAN bus interface? Potential to drive robot arm, or be driven by robot arm. In any case robot arm needs separate control boards or daisy chained CAN bus.
Definitely needed.
[?] ROS compatibility. Only if anyone asks for it.
[x] Tool assisted tensioning via locked spindle halves and a tool that can pull them to unlock, then hold on and turn (like screwdriver) to tension until threshold. Could have all motors facing outside via staggered pentagonal placing of modules on forearm, for easy construction and repairs. No need for loop buckles, finger tendons can go directly through a spindle hole on each half; then spun tight by the motor and tensioning tool. Finally a cover to prevent the spindles halves coming apart and to cover the knob used for tensioning.
Good idea, but meh, still too hard to tension, need a different strategy which eliminates the need for special tool.
[ ] Fan output from main board; also temperature sensor somewhere in middle.


2x more powerful hand, 2x structurally stronger, back driven, 2x pressure sensing, waterproofing, cost increase from motors 50, cost increase from separate pcbs 100, cost freed from labour 150. Net 0 cost change for improved hand. Could sell for $1000 per hand.

[x] Switch to small drone brushless motors with planetary gear machined inside. Allows for 2x strength, 2x speed, 3x structural integrity, can be back driven, more sensitive force feedback. We get more strength and speed from efficiency of brushless motors, but also at cost of 2x increase in power use. Planetary gear with 3 planets allows for 3x increase in mechanical strength; compared to weak point of current design is last gear in the gearbox, which rotates on a thin 1mm shaft without bearings. Additionally can machine output shaft and gears to rest on bearings as well as thin, all metal spindle, taking 2x duty of the gear reduction compared to current design, freeing the gear box.
[x] 4x motors placed on 1 drone control driver board, with CAN bus, directly soldered into a module. 5 modules for hand, with wrist actuators moved to forearm reducing palm size to human thickness and width. (No because drivers for brusheless motors are too expensive, complicated and large).
