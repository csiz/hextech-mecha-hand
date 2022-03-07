Circuit
-------

[ ] Very important, move logo 3 mm to the side on the fingertip so it can be seen from outside.
[ ] Totally not important, default the motor drivers to sleep state when controller off also cause it's
a strapping pin for the ESP32...
[ ] Slight increase to spacing of fingertip pressure sensors. Fix top folding, just offset pads a bit and
bend during assembly.



Done
====


[v] Finish first fully working prototype (with remote hand mimicking).
[x] Write pending blogposts on modeling, circuit, and mk 4&5 hands.
[v] Update PCB with compact design, all 3V3 logic, 5V DC-DC converter (replacing volt reg also leads to 24V rating).
[v] Write bill of materials.
[x] Document assembly.
[x] Publish first working version.
[v] Connect the pid drive shift register to the reset line. After a reset the arduino pulls the pins high, which causes the motors to move if the shift register outputs are also high.
[x] To potentially make the reset programmable, leave SR reset alone, but add pull-down resistors to the all motor driver inputs.
    Inbuilt pull down resistors, now all inputs are connected to shift registers which are explicitly 0ed at power up.
[v] Fix the PID drive arduino programming deal. The arduino powers the motor drivers when connected to 5V internally, this causes the motors to work. Need to pull the PWM or RX/TX output
signals low to prevent motors working during programming.
    Done, arduino no longer turns motors at startup, and RX/TX lines are free.
[v] Fix the main board encoders. They need a stronger pull up resistor (10K ohm), otherwise the esp32 keeps interrupting itself. Also a low pass filter or something.
    Pull-ups and low passes for all inputs.
[v] Change voltage to 7V4, cause I misremembered.
    Changed on driver board. Changed on main board.
[v] ! Fix the address pins on the nano. Dual use doesn't work if there's a LED on the same pin. Also avoid pin 13 as it has the internal LED. Increasing the LED resistance to 10K ohm works, but it's a bit dodgy.
    Fixed, LED moves to it's own pin.
[v] Not a good idea to connect the main chip 5V to the driver chip 5V, not the same 5V level because of different regulators. Also since a reset of
the main chip shuts down current for the driver chips, we might not need a reset line? TLDR: Redesign connectors to be more streamline.
    Connectors for the PID6 drive are now SCL SDA RST VCC GND, which is all lines needed as inputs.
[v] Change to 3V3 voltage for the inputs to the esp board. ESP can't read higher than it's working voltage.
[v] Enlarge the support holes for the encoders.
[v] Test soldering address pads.
    Tested, it's pretty difficult. Enhanced the solder mask to allow a bridge between the pads.
[v] Double check current sensor pins are properly upside down.


Improvements
------------

[v] Sturdier finger covers, maybe screwed on. Make them easier to put on (no inside prongs). Try to cover joints too?
[v] Integrate finger covers as structural outer shell.
[v] Joint potentiometer housing should also have mounting channels for the wires. Was a good idea!
[x] Flatten hand design:
  * Pitch axis further back and in-line with yaw.
  * Fingers yaw pivot on top of abduct. No need for big hole in finger cover.
  * Bring fingers lower, and cover bottom of pinky/thumb pivots.
[v] Compact hand design, with inline tendon wires and inline thumb and pinky pivots.
[v] Housing improvements:
  * Smaller housing for compacter PCB.
  * Hand interface as separate piece that screws on the bottom of the main chassis. Easier to screw from below.
  * Mounting pins from interface to PCB housing.
[v] Switch to 12V using 4s LIPO battery, 12V motors and improved PCB (DC-DC buck converter).
[v] Better pressure sensor design:
  * Put gauges on opposite sides, directed so one stretches and one compresses, doubling signal.
  * Is sensitive enough to put on main structure?
  * Make sensor easier to build/solder. Maybe individual wire channels? Flatter gluing area.


Driver Redesign
---------------

[v] Try to redesign the driver as a single PCB with 24 driving channels + sensors option.
[v] Smoothing capacitor is connected in parallel. Absolutely no use with the short circuit behaviour of the TB6612FNG...
  Still including it in, but smaller 4.7uF cap to smooth out 100kHz frequency. Can use both quick brake and slow brake mode with new driver chips.
[v] De-solder smoothing capacitors, was stupid idea cause it crosses the short-circuit current limit within a 100kHz cycle.


ESP
---

[v] Select input to which joint.
[v] Select output to which joint.
[v] Select min and max of each joint target.
[v] Same as above, but for the I2C nano joints.
[v] Save joint config on NVS on the ESP32.
[v] Measure power. And calibrate.
[v] Connect to wifi. If I can, go straight via the BLE api.
[v] Websocket with joint position, target, min, max via json.
[v] Measure tip pressure via precise ADC IC.


### PCB

[v] Draw board layout.
[v] Place parts on printed PCB and order.
[x] Maybe put a coil in series with the power so we limit the current on turning on.
Since motor capacitance will charge up on power-up.
[v] P-channel mosfet power switch. Hmm.
[x] Re-design and order the sensors mini board. No need, sensors board is enough.



Nano
----

[v] Select which input to which joint.
[v] Select which output to which joint.
[v] PID drive joint to target.
[v] Set P, I, D, threshold, overshoot, direction settings for each joint.
[v] Drive joint for duration at power.
[v] Set different 0 output point to allow some way of adding pressure with the motors.
[v] Get joint position.
[v] 2 input pins for address selection so only need 1 program to upload.
[v] Error handling for the I2C.


### PCB


[v] Draw board layout.
[v] Build board for the copper prototype pads.
[v] Place parts on printed PCB and order.
[v] Assemble boards.
[v] Test boards.


Demo Page
---------

[v] Page with joint position graph and control slider underneath.
[v] Connect via websocket to hand.


Hand
----

[x] Make spindle shorter and better middle rail so nylon doesn't slip even on when over-driven. Maybe reduce the tensioner angle?
[v] Make holes for spindle wires so it channels the wire better.
[x] Edit motor holder to have top support for motor ... (made holder less bendy instead)
[v] ... and higher/tighter spindle blocker.
[v] Re-make hand using new fingers.
[v] Better leverage for thumb and pinkie carriages.

