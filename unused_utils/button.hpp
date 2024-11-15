#pragma once

#include "Arduino.h"

void IRAM_ATTR button_interrupt(void * arg);

struct Button {
  const gpio_num_t pin;
  volatile uint8_t presses = 0;
  volatile uint8_t releases = 0;
  volatile uint8_t interrupts = 0;
  volatile bool pressed = false;
  unsigned long last_change = 0;
  int min_delay = 10;
  const bool active_state;

  Button(gpio_num_t pin, bool active_state = HIGH) : pin(pin), active_state(active_state) {}

  void begin(){
    pinMode(pin, INPUT);
    // pinMode(pin, active_state == HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
    attachInterruptArg(pin, button_interrupt, this, CHANGE);
  }

  void end(){
    detachInterrupt(pin);
  }

  size_t collect_presses() {
    // Might definitely be a better way, but get presses and subtract from
    // stored presses. If interrupted mid-way we leave them for next time.
    uint8_t tmp = presses;
    presses -= tmp;
    return tmp;
  }
};

// Button debounce handling.
void IRAM_ATTR button_interrupt(void * arg) {
  Button & button = *static_cast<Button*>(arg);

  button.interrupts += 1;

  auto now = millis();
  if (now - button.last_change > button.min_delay) {

    button.pressed = digitalRead(button.pin) == button.active_state;

    if(button.pressed) button.presses += 1;
    else button.releases += 1;

    button.last_change = now;
  }
}