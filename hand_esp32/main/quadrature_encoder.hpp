#pragma once

#include "Arduino.h"
#include "esp32-hal-gpio.h"
#include "esp_attr.h"

// Inspired by code in:
// https://makeatronics.blogspot.com/2013/02/efficiently-reading-quadrature-with.html

const int8_t encoder_lookup_table[] = {0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};

void IRAM_ATTR encoder_interrupt(void * arg);

struct Encoder {
  const gpio_num_t a;
  const gpio_num_t b;
  volatile int position = 0;
  volatile bool a_value;
  volatile bool b_value;

  Encoder(const gpio_num_t a, const gpio_num_t b) : a(a), b(b) {}

  void begin(){
    pinMode(a, INPUT_PULLDOWN);
    pinMode(b, INPUT_PULLDOWN);

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

  const uint state = encoder.a_value << 3 | encoder.b_value << 2 | new_a_value << 1 | new_b_value;

  encoder.position += encoder_lookup_table[state];

  encoder.a_value = new_a_value;
  encoder.b_value = new_b_value;
}