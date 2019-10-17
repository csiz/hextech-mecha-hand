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
  volatile unsigned long last_interrupt_micros;

  int n_interrupts = 0;
  int last_position = 0;

  // Set the encoder to be an active LOW or active HIGH state.
  const bool active_state;

  Encoder(const gpio_num_t a, const gpio_num_t b, const bool active_state = HIGH) : a(a), b(b), active_state(active_state) {}

  void begin(){
    pinMode(a, active_state == HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);
    pinMode(b, active_state == HIGH ? INPUT_PULLDOWN : INPUT_PULLUP);

    a_value = digitalRead(a) == active_state;
    b_value = digitalRead(b) == active_state;
    last_interrupt_micros = micros();

    attachInterruptArg(a, encoder_interrupt, this, CHANGE);
    attachInterruptArg(b, encoder_interrupt, this, CHANGE);
  }

  void end() {
    detachInterrupt(a);
    detachInterrupt(b);
  }

  int collect_change(){
    int change = position - last_position;
    last_position += change;
    return change;
  }
};

void IRAM_ATTR encoder_interrupt(void * arg) {
  auto & encoder = *static_cast<Encoder*>(arg);

  // Debounce interrupts if within a very short time.
  const unsigned long diff = micros() - encoder.last_interrupt_micros;
  // Note that when micros <  last_interrupt_micros because of the
  // time overflow, then diff should also underflow into a high value.
  if (diff < 100) return;

  // Flip the states if we use pull-up as it's active LOW.
  const bool active_state = encoder.active_state;

  bool new_a_value = digitalRead(encoder.a) == active_state;
  bool new_b_value = digitalRead(encoder.b) == active_state;

  const uint state =
    encoder.a_value << 3 |
    encoder.b_value << 2 |
    new_a_value << 1 |
    new_b_value;

  encoder.position += encoder_lookup_table[state];

  encoder.a_value = new_a_value;
  encoder.b_value = new_b_value;

  encoder.last_interrupt_micros = micros();

}