#pragma once

#include "pins.hpp"

#include "Arduino.h"


namespace power {


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

  void turnoff(){
    digitalWrite(POWER_CTRL, LOW);
  }

  void shutdown_on_long_press(){
    // Power management; if power button has been held for 1 second, turn off.
    if (digitalRead(POWER_BTN) and millis() - power_last_press > 1000) {
      turnoff();
    }
  }
}