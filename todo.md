Fixme
-----

[x] Connect the pid drive shift register to the reset line. After a reset the arduino pulls the pins high, which causes the motors to move if the shift register outputs are also high.
[ ] To potentially make the reset programmable, leave SR reset alone, but add pull-down resistors to the all motor driver inputs.
[ ] Fix the PID drive arduino programming deal. The arduino powers the motor drivers when connected to 5V internally, this causes the motors to work. Need to pull the PWM or RX/TX output
signals low to prevent motors working during programming.
[ ] Fix the main board encoders. They need a stronger pull up resistor (10K ohm), otherwise the esp32 keeps interrupting itself. Also a low pass filter or something.
[ ] Change voltage to 7V4, cause I misremembered.
[ ] ! Fix the address pins on the nano. Dual use doesn't work if there's a LED on the same pin. Also avoid pin 13 as it has the internal LED. Increasing the LED resistance to 10K ohm works, but it's a bit dodgy.
[ ] Not a good idea to connect the main chip 5V to the driver chip 5V, not the same 5V level because of different regulators. Also since a reset of
the main chip shuts down current for the driver chips, we might not need a reset line? TLDR: Redesign connectors to be more streamline.


ESP
---

[v] Select input to which joint.
[v] Select output to which joint.
[v] Select min and max of each joint target.
[v] Same as above, but for the I2C nano joints.
[ ] Save joint config on NVS on the ESP32.
[ ] Measure power. And calibrate.
[ ] Connect to wifi. If I can, go straight via the BLE api.
[ ] Websocket with joint position, target, min, max via json.
[ ] Measure tip pressure via precise ADC IC.

### PCB

[v] Draw board layout.
[v] Place parts on printed PCB and order.

[x] Maybe put a coil in series with the power so we limit the current on turning on.
Since motor capacitance will charge up on power-up.

[v] P-channel mosfet power switch. Hmm.


[ ] Re-design and order the sensors mini board.


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

[ ] Page with joint position graph and control slider underneath.
[ ] Connect via websocket to hand.


Hand
----

[x] Make spindle shorter and better middle rail so nylon doesn't slip even on when over-driven. Maybe reduce the tensioner angle?
[v] Make holes for spindle wires so it channels the wire better.
[x] Edit motor holder to have top support for motor ... (made holder less bendy instead)
[v] ... and higher/tighter spindle blocker.
[v] Re-make hand using new fingers.
[v] Better leverage for thumb and pinkie carriages.


Project
-------

[ ] Write bill of materials.
[ ] Document assembly.
[ ] Publish first working version.

