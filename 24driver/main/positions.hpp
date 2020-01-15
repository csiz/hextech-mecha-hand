#pragma once

#include "pins.hpp"
#include "spi.hpp"


namespace positions {
  // Look-up for each converter's chip select pin.
  const gpio_num_t ADC_CS_PINS[3] = {POSITION0_CS, POSITION1_CS, POSITION2_CS};

  void setup(){
    for (size_t adc = 0; adc < 3; adc++){
      const gpio_num_t cnv_pin = ADC_CS_PINS[adc];

      pinMode(cnv_pin, OUTPUT);
      digitalWrite(cnv_pin, HIGH);
    }
  }

}