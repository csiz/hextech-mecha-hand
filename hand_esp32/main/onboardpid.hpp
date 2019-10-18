#pragma once

#include "pins.hpp"
#include "pid.hpp"

#include "driver/ledc.h"

#include "Arduino.h"


namespace onboardpid {

  // ADC value where it's likely a short (at 10 bit resolution).
  const int adc_low = 8; // ~0.06V
  const int adc_high = 856; // ~3.23V Note that 3.3V max input corresponds to 866.
  // TODO: ADC is connected to +5V


  // Input positions, 10bits. Note that the ESP32 max input voltage value is 866.
  int inputs[2] = {0, 0};

  // Target positions, 10bits.
  int targets[2] = {0, 0};

  // Enable input/output combination.
  bool enable[2] = {false, false};

  // Whether the controller seeks position, or only applies the timed drive.
  bool seeking[2] = {false, false};

  // Invert seeking outputs so that positive error leads to negative control.
  bool invert[2] = {false, false};

  // Amount of power to apply to each output (doesn't respect invert direction).
  int drive_power[2] = {0, 0};

  // Amount of time (in millis) to apply power for.
  int drive_time[2] = {0, 0};

  // Output indexes of driver units, -1 for unset.
  int8_t output_idx[2] = {0, 1};

  // Input indexes  of driver units -1 for unset.
  int8_t input_idx[2] = {0, 1};

  // PID controller for outputs. Indexed by driver units.
  HysterisisPID8bit pids[2];

  // Error flag.
  bool error_state = false;
  bool error_pin[2] = {false, false};



  // Exponentially average analog reads so we don't get such large fluctuations.
  inline void exp_avg_analog_read(const uint8_t pin, const uint8_t i) {
    int value = analogRead(pin);
    if (adc_low < value and value < adc_high) {
      error_pin[i] = false;
      inputs[i] = (inputs[i] * 6 + value * 4 + 5) / 10; // + 5 / 10 to round the value.
    } else {
      error_pin[i] = enable[i]; // leave it as non-error if disabled
    }
  }

  /* Setup the on-board PID drives. */
  void setup(){
    // Direction pins.
    pinMode(DIR0, OUTPUT);
    digitalWrite(DIR0, LOW);
    pinMode(DIR1, OUTPUT);
    digitalWrite(DIR1, LOW);
    pinMode(DIR2, OUTPUT);
    digitalWrite(DIR2, LOW);
    pinMode(DIR3, OUTPUT);
    digitalWrite(DIR3, LOW);

    // Input pins.
    pinMode(IN0, ANALOG);
    pinMode(IN1, ANALOG);

    // Read once to initialize input values.
    inputs[0] = analogRead(IN0);
    inputs[1] = analogRead(IN1);

    // Power control pins.
    ledcSetup(PWM0_C, 1000.0, 8);
    ledcSetup(PWM1_C, 1000.0, 8);
    ledcAttachPin(PWM0, PWM0_C);
    ledcAttachPin(PWM1, PWM1_C);

    // Error led pin.
    pinMode(IN_ERROR, OUTPUT);
    digitalWrite(IN_ERROR, LOW);
  }

  /* Read inputs and output controls for the on-board PID drives. */
  void loop_tick(const int elapsed_millis) {

    // Inputs and control
    // ------------------

    // Read inputs.
    exp_avg_analog_read(IN0, 0);
    exp_avg_analog_read(IN1, 1);


    // Compute required values for the direction pins; initialize to 0 power.
    bool direction[4] = {};
    int power[2] = {};

    for (int i = 0; i < 2; i++) {
      // Skip if unit not enabled.
      if (not enable[i]) continue;

      int control = 0;

      // Apply timed pressure regardless of seeking position.
      if (drive_time[i] > 0) {
        drive_time[i] -= elapsed_millis;

        control += drive_power[i];
      }

      // Update PID control if seeking to a position.
      if (seeking[i] and not error_pin[i] and input_idx[i] != -1) {
        const int position = inputs[input_idx[i]];
        pids[i].update(position, targets[i], elapsed_millis);
        control += (invert[i] ? -1 : +1) * pids[i].control;
      }

      // Get the output index.
      const int idx = output_idx[i];
      if (idx == -1) continue;

      // Run in control direction at control strength.
      power[idx] = min(abs(control), 255);
      direction[idx*2 + 0] = control > 0;
      direction[idx*2 + 1] = control < 0;
      // Note that for 0 control the motor will run freely untill stop.
    }

    // Set pin outputs to computed values.
    digitalWrite(DIR0, direction[0]);
    digitalWrite(DIR1, direction[1]);
    digitalWrite(DIR2, direction[2]);
    digitalWrite(DIR3, direction[3]);

    // Write pulse width modulation levels.
    ledcWrite(PWM0_C, power[0]);
    ledcWrite(PWM1_C, power[1]);

    // Error handling
    // --------------

    // Use a temporary flag so we update the global state in a single clock step.
    bool new_error_state = false;
    for (int i = 0; i < 2; i++) new_error_state |= error_pin[i];
    error_state = new_error_state;

    digitalWrite(IN_ERROR, error_state);
  }

  inline int get_input(int8_t drive_idx){
    if (drive_idx < 0 or 2 <= drive_idx) return -1;
    if (input_idx[drive_idx] == -1) return -1;
    return inputs[input_idx[drive_idx]];
  }

}