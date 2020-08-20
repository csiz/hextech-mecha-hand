/* 24 Channel DC motor driver

PCB Features
------------

* Power control via the power button and high current mosfet.
* Voltage monitoring by a voltage divider 8.2k / (8.2k + 2k) ~= 0.804.
* Current monitoring via [ACS712-20A-T](https://www.sparkfun.com/datasheets/BreakoutBoards/0712.pdf) and 2k/2k == 0.5 voltage divider.
* 2 buttons, 1 joystick, and [SSD1306](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf) I2C OLED display as user interface.
* External pins for serial, secondary I2C interface and GPIO 0 (bootloader pin).
* 24 [DRV8870DDAR](https://www.ti.com/lit/ds/symlink/drv8870.pdf) motor drivers.
* Motor drivers controlled by [TLC59116IPWR](https://www.ti.com/lit/ds/symlink/tlc59116.pdf) PWM LED drivers @ 97kHz; I2C interface.
* Motor current measurement via 0.1ohm resistor and [AD7689BCPZRL7](https://www.analog.com/media/en/technical-documentation/data-sheets/AD7682_7689.pdf)
ADC; serial interface.
* Position measurement via [ADC128S102](https://www.ti.com/lit/ds/symlink/adc128s102.pdf); serial interface.
* 12 strain gauge measurements via [ADS1115](https://www.ti.com/lit/ds/symlink/ads1115.pdf) with AIN3 set to 1/2 voltage; I2C interface.
* Gyro and acceleration measurements via [MPU-6050](https://www.invensense.com/wp-content/uploads/2015/02/MPU-6000-Datasheet1.pdf); I2C interface.


Code Outline
------------

### Top level
* main.cpp: Top level setup and loop. Handles state updates.
* state.hpp: Filtered state of each channel, and commands. Common interface.
* ui.hpp: User interaction via buttons and the OLED screen.
* web.hpp: Web server and websocket comms.
### Hardware interface
* pins.hpp: Defines physical connections to the ESP32 chip, and I2C addresses for slave ICs.
* power.hpp: Power control and measurements.
* drivers.hpp: Control driving power for each motor.
* positions.hpp: Read positions corresponding to each motor.
* strains.hpp: Read strain gauges.
* currents.hpp: Read per motor current use.
### Utils
* pid.hpp: PID control logic.
* byte_encoding.hpp: Bit banging utilities for web serialization.
* timing.hpp: Time utilities.
* i2c.hpp: Utils to send/receive over I2C wires.
* spi.hpp: Utils to send/receive over spi.
* memory.hpp: Utils to store settings in offline chip memory.
*/


#include "i2c.hpp"
#include "ui.hpp"
#include "drivers.hpp"
#include "power.hpp"
#include "spi.hpp"
#include "currents.hpp"
#include "positions.hpp"
#include "strains.hpp"
#include "web.hpp"
#include "memory.hpp"
#include "pid.hpp"
#include "state.hpp"

// Time the main loop.
timing::LoopTimer timer;
// Don't update faster than 1ms.
const unsigned long min_loop_update_period = 1;

// Exponentially average all state measurements with 30ms half life.
const timing::ExponentialAverage state_exp_avg = {0.030};

// Average current use with a 1 second half life.
const timing::ExponentialAverage current_exp_avg = {1.0};

void setup(){
  // Keep the board turned on before anything else.
  power::setup();

  // Pull-up RX to prevent handling random noise bytes.
  pinMode(RX, INPUT_PULLUP);

  // Initialize inter-chip comms before the chips themselves.
  i2c::setup();
  spi::setup();

  // Initialize component chips.
  ui::setup();
  drivers::setup();
  currents::setup();
  positions::setup();
  strains::setup();

  // Setup internal memory, used by web and state.
  memory::setup();

  // Load state params into memory.
  state::setup();

  // Start webserver and its loop (they're configured to run in core 0).
  web::setup();

  // Start timing.
  timer.begin();
}

void loop(){
  // Time and throttle updates.
  timer.update(min_loop_update_period);

  // Update power measurements, and check if we need to turn-off.
  power::update();

  // Get updates from input chips.
  currents::update();
  positions::update();
  strains::update();


  // Updates to centralized state.
  using state::state;
  const float elapsed = timer.loop_duration;
  using mystd::clamp;

  // Update state basics.
  state.current = state_exp_avg(power::current, state.current, elapsed);
  state.voltage = state_exp_avg(power::voltage, state.voltage, elapsed);
  state.power = state_exp_avg(power::power, state.power, elapsed);
  state.energy += state.power * elapsed;
  state.fps = timer.fps;
  state.max_loop_duration = timer.max_loop_duration;
  state.update_time = timer.update_time;

  // Update driver channels.
  for (size_t i = 0; i < 24; i++){
    auto & channel = state.channels[i];

    // Get position, invert and smooth out.
    auto const& raw_position = positions::position[i];
    const float position = channel.reverse_input ? (1.0 - raw_position) : raw_position;
    channel.position = state_exp_avg(position, channel.position, elapsed);

    // Store smoothed current measurements.
    channel.current = state_exp_avg(currents::current[i], channel.current, elapsed);

    // Store slowly average current measurements.
    channel.avg_current = current_exp_avg(channel.current, channel.avg_current, elapsed);

    // Power per drive channel is a combination seeking and base power.
    float power = channel.power_offset;

    // PID control if channel is seeking.
    if (channel.seek != -1.0) {
      // Clamp position seeking to maximum values we can reach; as calibrated by user.
      const float seek = clamp(channel.seek, channel.min_position, channel.max_position);
      // Add to base power; note that base power would usually be 0.
      power += channel.pid.update(channel.position, seek, elapsed);
    }

    // Apply minimal power or exactly 0. Between 0 and min power friction overcomes any movement.
    if (power != 0.0 and std::abs(power) < channel.min_power) {
      power = power < 0.0 ? -channel.min_power : +channel.min_power;
    }

    // Compute maximum power. Either 100%, or clamped by current limits.
    float max_power = +1.0;

    // Use last power setting to compute the max power we can use whilst staying within current specs.
    if (channel.current > 0.001 and channel.power != 0.0) {
      // Only if the last setting was not 0, and we use more than a milliamp.
      max_power = std::min(max_power,
        std::abs(channel.power) * channel.max_current * state.current_fraction / channel.current);
    }

    // Same limit, but using the 1 second rolling average of abs power and current.
    if (channel.avg_current > 0.001 and channel.avg_abs_power != 0.0) {
      max_power = std::min(max_power,
        channel.avg_abs_power * channel.max_avg_current * state.current_fraction / channel.avg_current);
    }

    // Maximum power must be positive.
    max_power = std::max(0.0f, max_power);

    // Clamp output to maximum 1, or the current limits, whichever is lowest.
    power = clamp(power, -max_power, +max_power);


    // **Only power when enabled!**
    if (not channel.enabled) power = 0.0;

    // Update state power; ignoring direction.
    channel.power = power;
    // Update the rolling average abs power.
    channel.avg_abs_power = current_exp_avg(std::abs(power), channel.avg_abs_power, elapsed);

    // Set driver power, clampped and potentially inverted.
    drivers::power[i] = channel.reverse_output ? -power : power;
  }

  // Update gauges.
  for (size_t i = 0; i < 12; i++){
    auto & gauge = state.gauges[i];

    // Scale the voltage to appropriate units; as calibrated by the user.
    const float scaled_strain = (strains::strain[i] - gauge.zero_offset) * gauge.coefficient;
    // Smooth out strain gauge measurements.
    gauge.strain = state_exp_avg(scaled_strain, gauge.strain, elapsed);
  }


  // Send updates to driver chips.
  drivers::update();


  // Throttled update of the screen and buttons.
  ui::update();
}