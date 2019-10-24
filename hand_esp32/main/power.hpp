#pragma once

#include "Arduino.h"
#include "pins.hpp"


// Shutdown proceedure defined in main.
void shutdown();

namespace power {

  // Current power use values.
  float voltage = 0; // Volts
  float current = 0; // Amps
  float power = 0; // Watts
  // Total energy used since startup.
  float energy = 0; // Joules


  // Raw values read from the analog inputs.
  int voltage_raw = 0;
  int current_raw = 0;

  // Guess that needs calibrating; in per million increments cause we can only save ints.
  const int default_voltage_scale = 10000; // uVolts per 10bit inputs.
  const int default_current_scale = 500; //  uAmps per 10bit inputs.
  int voltage_scale = default_voltage_scale;
  int current_scale = default_current_scale;

  // Assume initial guess is reasonable, set the increments to a smaller value.
  int voltage_scale_inc = voltage_scale / 50;
  int current_scale_inc = current_scale / 50;


  // Power button
  // ------------

  // Store the last time the power button was pressed so we can turn off
  // on a long press.
  unsigned long power_last_press = 0;
  void IRAM_ATTR power_button_interrupt() {
    power_last_press = millis();
  }


  void setup() {
    // Initialize the power pins.
    pinMode(POWER_BTN, INPUT_PULLDOWN);
    pinMode(POWER_CTRL, OUTPUT);
    pinMode(VOLTAGE_IN, INPUT);
    pinMode(CURRENT_IN, INPUT);

    // Turn the power supply mosfet on.
    digitalWrite(POWER_CTRL, HIGH);

    // Use the power button to shut off if held down.
    attachInterrupt(POWER_BTN, power_button_interrupt, RISING);
  }

  void measure_and_update(const int elapsed_millis) {
    voltage_raw = analogRead(VOLTAGE_IN);
    current_raw = analogRead(CURRENT_IN);

    // Scales are in micro-units so they can be stored as ints.
    voltage = 1e-6 * voltage_raw * voltage_scale;
    current = 1e-6 * current_raw * current_scale;
    power = voltage * current;
    energy += power * elapsed_millis / 1e3;
  }

  void turnoff(){
    digitalWrite(POWER_CTRL, LOW);
  }

  void shutdown_on_long_press(){
    // Power management; if power button has been held for 1 second, turn off.
    if (digitalRead(POWER_BTN) and millis() - power_last_press > 1000) {
      shutdown();
    }
  }
}