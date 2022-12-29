Next generation hand ideas
==========================

[v] Custom order coreless motors with planetary gearbox that can withstand 10kg force on the tendons!
[ ] Add rotation encoder to motors and connect via flex circuit to an FPC connector on the driver board. Maybe add temperature sensor directly on top of motor too. No more soldering!
[ ] Use STM32 chips to control pairs of 2 motors and read all the inputs from them. We'll need them to be able to run the fast current control loop without interruptions from main micro controller.
[ ] Directly assemble the ESP32 chip, use the one with the integrated USB C driver.
[ ] Program in remote updates for the ESP32 chips, and also program the ESP32 master controller to be able to program the STM32 motor controllers.
[ ] Test paper display and switch to it, use FPC connector on board again.
[?] Fan output from main board. Maybe temperature sensor somewhere in middle, but not needed if we put the temp sensors on the motors.
^ Might not need the above because the new motors are strong enough within their continuous operating amperage of 0.5A.
[?] ROS compatibility. Only if anyone asks for it.


Previous gen done
=================

[v] Differential bevel gears on a U joint for the wrist. Big space for wires in the middle, very sturdy.
[v] Switch to coreless DC motors for quieter operation and greatly simplified circuitry. There's no way to output max torque from brushless motors at standstill without a position encoder on the motor shaft. Speed at which it starts working is about 1Hz electrical rotation. Coreless motors are lighter and quieter and should work just fine.
Coreless is near mandatory for backdrivabialiy.
[v] Alternatively 4 extra motors physically connected to each other as the 2 sides of the spindle. Tendon wires go out the middle and to the wrist and hand roll joints. Tensioning done by driving the gears.
Yep, works good.
motors in opposite sides then sliding and fixing the motors together.
[v] 4 spare inputs put on the tip joints (distal-medial) of non-thumb fingers. Tips are connected to driven joint (medial-proximal) by a spring. Difference in joint angles should be an accurate measure of tip force. More accurate then motor current feedback, will work passively too.
OMG it works!
[v] Switch to hall effect position sensors.
[v] Single ribbon cable between palm and wrist sensors to driver hand.
Works perfectly, needs proper channel to pass through nicely so it doesn't get pinched.
[v] CAN bus interface? Potential to drive robot arm, or be driven by robot arm. In any case robot arm needs separate control boards or daisy chained CAN bus.
Definitely needed.