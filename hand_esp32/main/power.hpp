#pragma once

#include "Arduino.h"
#include "pins.hpp"


namespace power {

  int raw_voltage = 0;
  int raw_current = 0;


  // Power button
  // ------------

  // Store the last time the power button was pressed so we can turn off
  // on a long press.
  unsigned long power_last_press = 0;
  void IRAM_ATTR power_button_interrupt() {
    power_last_press = millis();
  }

  // TODO: track total power use.


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
    raw_voltage = analogRead(VOLTAGE_IN);
    raw_current = analogRead(CURRENT_IN);
    // TODO: calibrate and scale voltage and current.
    // TODO: use these to compute power
  }

  void shutdown(){
    digitalWrite(POWER_CTRL, LOW);
  }

  void shutdown_on_long_press(){
    // Power management; if power button has been held for 1 second, turn off.
    if (digitalRead(POWER_BTN) and millis() - power_last_press > 1000) {
      shutdown();
    }
  }
}