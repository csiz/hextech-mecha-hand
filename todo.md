Fixme
-----

[ ] Connect the pid drive shift register to the reset line. After a reset the arduino pulls the pins high, which causes the motors to move if the shift register outputs are also high.


ESP
---

[ ] Select input to which joint.
[ ] Select output to which joint.
[ ] Select min and max of each joint target.
[ ] Same as above, but for the I2C nano joints.
[ ] Connect to wifi. If I can, go straight via the BLE api.
[ ] Websocket with joint position, target, min, max via json.

[ ] Measure power.
[ ] Measure tip pressure via precise ADC IC.

### PCB

[ ] Draw board layout.
[ ] Place parts on printed PCB and order.

[ ] Maybe put a coil in series with the power so we limit the current on turning on.
Since motor capacitance will charge up on power-up.

[ ] P-channel mosfet power switch. Hmm.

Nano
----

[x] Select which input to which joint.
[v] Select which output to which joint.
[v] PID drive joint to target.
[v] Set P, I, D, threshold, overshoot, direction settings for each joint.
[ ] Drive joint for duration at power.
[v] Get joint position.
[v] 2 input pins for address selection so only need 1 program to upload.
[v] Error handling for the I2C.


### PCB


[v] Draw board layout.
[v] Build board for the copper prototype pads.
[v] Place parts on printed PCB and order.
[ ] Assemble boards.
[v] Test boards.


Demo Page
---------

[ ] Page with joint position graph and control slider underneath.
[ ] Connect via websocket to hand.


Hand
----

[x] Make spindle shorter and better middle rail so nylon doesn't slip even on when over-driven. Maybe reduce the tensioner angle?
[ ] Make holes for spindle wires so it channels the wire better.
[ ] Edit motor holder to have top support for motor, and higher/tighter spindle blocker.
[ ] Re-make hand using new fingers.
[ ] Better leverage for thumb and pinkie carriages.


Project
-------

[ ] Write bill of materials.
[ ] Document assembly.
[ ] Publish first working version.
