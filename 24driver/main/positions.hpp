#pragma once

#include "pins.hpp"
#include "spi.hpp"


namespace positions {

  // Raw ADC values from the ADC128 (12bit).
  uint16_t raw[24] = {0};

  // Per channel positions as real number between 0 and 1.0 at full scale.
  volatile float position[24] = {0.0};

  // Scale is 2^-12, assuming the voltage reference is also used as the high voltage on the potentiometers.
  const uint16_t MAX_VALUE = 0xFFF;
  const float SCALE = 1.0 / MAX_VALUE;

  // Look-up for each converter's chip select pin.
  const gpio_num_t ADC_CS_PINS[3] = {POSITION0_CS, POSITION1_CS, POSITION2_CS};


  // Use 16MHz clock speed, MSB first, clock output on falling edge and capture data on rising edge (SPI_MODE0).
  // Working frequency for this ADC is 8MHz to 16MHz, use fastest mode, giving us 1MSPS.
  const SPISettings SPI_SETTINGS = {16000000, SPI_MSBFIRST, SPI_MODE0};


  void setup(){
    for (size_t adc = 0; adc < 3; adc++){
      const gpio_num_t cnv_pin = ADC_CS_PINS[adc];

      pinMode(cnv_pin, OUTPUT);
      digitalWrite(cnv_pin, HIGH);
    }
  }

  void update(){
    // Setup SPI for these devices.
    spi::vspi.beginTransaction(SPI_SETTINGS);

    for (size_t adc = 0; adc < 3; adc++){
      const gpio_num_t cs_pin = ADC_CS_PINS[adc];

      // To get data we need to set the first address (even if the default
      // of IN0 is fine). On the second transfer we read the conversion on
      // this channel and prepare the next.


      // Begin data transfer.
      digitalWrite(cs_pin, LOW);
      // Setup to read channel IN0.
      /* ignore = */ spi::vspi.transfer16(0x00);
      for (size_t in = 0; in < 8; in++){
        const size_t idx = in + adc * 8;

        // Read the conversion for the current channel and prepare the next. We're
        // using 16bit cycles, but the chip expects 8 bits for the register. Therefore
        // shift the register 8, most significant bits first. ADD0 is bit 3 on the
        // register, so we also need to shift the channel up 3 bits.
        uint16_t result = spi::vspi.transfer16((static_cast<uint16_t>(in+1) << 3) << 8);

        // Discard the result if the first 4 bits were non-null.
        if (result > MAX_VALUE) continue;

        raw[idx] = result;
        position[idx] = SCALE * result;
      }

      // End conversion burst.
      digitalWrite(cs_pin, HIGH);

    }

    spi::vspi.endTransaction();
  }

}