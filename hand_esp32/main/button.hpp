#pragma once

#include "Arduino.h"

void IRAM_ATTR button_interrupt(void * arg);

struct Button {
  const gpio_num_t pin;
  volatile uint8_t presses = 0;
  unsigned long last_press = 0;
  int min_delay = 100;
  const bool active_state;

  Button(gpio_num_t pin, bool active_state = HIGH) : pin(pin), active_state(active_state) {}

  void begin(){
    pinMode(pin, active_state == HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
    attachInterruptArg(pin, button_interrupt, this, active_state == HIGH ? RISING : FALLING);
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
  Button* button = static_cast<Button*>(arg);

  auto now = millis();
  if (now - button->last_press > button->min_delay) {
    button->presses += 1;
    button->last_press = now;
  }
}