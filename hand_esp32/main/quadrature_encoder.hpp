#pragma once

#include "Arduino.h"
#include "esp32-hal-gpio.h"
#include "esp_attr.h"

// Inspired by code in:
// https://makeatronics.blogspot.com/2013/02/efficiently-reading-quadrature-with.html

const int8_t encoder_lookup_table[] = {
   0, -1,  1,  0,
   1,  0,  0, -1,
  -1,  0,  0,  1,
   0,  1, -1,  0};

void IRAM_ATTR encoder_interrupt(void * arg);

struct Encoder {
  const gpio_num_t a;
  const gpio_num_t b;
  volatile int position = 0;
  volatile bool a_value;
  volatile bool b_value;

  // Set the encoder to be an active LOW or active HIGH state.
  const bool active_state;

  Encoder(const gpio_num_t a, const gpio_num_t b, const bool active_state = HIGH) : a(a), b(b), active_state(active_state) {}

  void begin(){
    pinMode(a, active_state == HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
    pinMode(b, active_state == HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);

    a_value = digitalRead(a);
    b_value = digitalRead(b);

    attachInterruptArg(a, encoder_interrupt, this, CHANGE);
    attachInterruptArg(b, encoder_interrupt, this, CHANGE);
  }

  void end() {
    detachInterrupt(a);
    detachInterrupt(b);
  }
};

void IRAM_ATTR encoder_interrupt(void * arg) {
  auto & encoder = *static_cast<Encoder*>(arg);

  bool new_a_value = digitalRead(encoder.a);
  bool new_b_value = digitalRead(encoder.b);

  // Flip the states if we use pull-up as it's active LOW.
  const bool active_state = encoder.active_state;

  const uint state =
    (encoder.a_value == active_state) << 3 |
    (encoder.b_value == active_state) << 2 |
    (new_a_value == active_state) << 1 |
    (new_b_value == active_state);

  encoder.position += encoder_lookup_table[state];

  encoder.a_value = new_a_value;
  encoder.b_value = new_b_value;
}