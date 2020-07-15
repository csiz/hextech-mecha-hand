#pragma once

#include "pins.hpp"
#include "spi.hpp"

#include "Arduino.h"


// [MCP3208][manual] 12bit 50ksps chip to read voltage levels of the shunt resistors.
// [manual]: http://ww1.microchip.com/downloads/en/devicedoc/21298e.pdf

namespace currents {

  // Raw ADC values from the MCP3208 (12bit).
  uint16_t raw[24] = {0};
  // Per channel current values in ampere; assuming accurate sense resistors.
  volatile float current[24] = {0.0};


  // Look-up for each converter's CNV pin.
  const gpio_num_t ADC_CNV_PINS[3] = {CURR0_CNV, CURR1_CNV, CURR2_CNV};


  // This reference is set via a 10k/2.0k @ 3V3 voltage divider.
  const float VREF = 0.55;
  // Voltage value that the least significant bit represents. The MCP3208 chip is 12 bit.
  const float VLSB = VREF / 0x1000;
  // Current sense resistance for each motor driver; in ohms.
  const float SENSE_RESITANCE = 0.1;
  // Current increment per result bit; in ampere.
  const float ILSB = VLSB / SENSE_RESITANCE;

  const uint16_t MAX_VALID = 0x0FFF;


  // Use 1MHz clock speed, MSB first, clock output on falling edge and capture data on rising edge (SPI_MODE0).
  // Using 1Mhz as it is 20x sampling rate of 50ksps the MCP3208 can output at 2.7V power.
  const SPISettings SPI_SETTINGS = {1000000, SPI_MSBFIRST, SPI_MODE0};


  inline void setup() {
    for (size_t adc = 0; adc < 3; adc++){
      const gpio_num_t cnv_pin = ADC_CNV_PINS[adc];

      pinMode(cnv_pin, OUTPUT);
      digitalWrite(cnv_pin, HIGH);
    }
  }

  inline uint16_t read_analog_input(const gpio_num_t cnv_pin, const size_t in){
    // Begin data transfer.
    digitalWrite(cnv_pin, LOW);
    // Wait 100ns from CS low to first clock (16 noops).
    NOP();NOP();NOP();NOP(); NOP();NOP();NOP();NOP(); NOP();NOP();NOP();NOP(); NOP();NOP();NOP();NOP();

    // According to the [manual][manual] Fig 6-1 on page 21, we need to send 2 config bytes, and read
    // 3 total bytes, the last 2 containing the conversion result.

    // First config byte is zeroes followed by start bit 1 and single mode selctor (SGL value is 1);
    // and finally the 3rd bit of the channel D2.
    const uint8_t cfg0 = 0b00000110 | ((in >> 2) & 0b1);
    // Second config byte starts with the last 2 bits of the channel, D1 D0.
    const uint8_t cfg1 = (in & 0b11) << 6;
    // Last byte doesn't matter.
    const uint8_t cfg2 = 0x00;

    // Send the 3 config bytes and get the analog conversion result.
    spi::vspi.transfer(cfg0);
    const uint8_t result1 = spi::vspi.transfer(cfg1);
    const uint8_t result2 = spi::vspi.transfer(cfg2);

    // Result is contained in the last 13 bits of data. The 3th should be 0 for a succesfull conversion.
    uint16_t result = static_cast<uint16_t>(result1 & 0b00011111) << 8 | result2;


    digitalWrite(cnv_pin, HIGH);
    // Wait 500ns from CS high till we can start the next read.
    delayMicroseconds(1);

    return result;
  }

  inline void update(){
    // Setup SPI for these devices.
    spi::vspi.beginTransaction(SPI_SETTINGS);

    // Read each converter in turn.
    for (size_t adc = 0; adc < 3; adc++){
      const gpio_num_t cnv_pin = ADC_CNV_PINS[adc];

      // Read raw values from the chip.
      for (size_t in = 0; in < 8; in++){
        // For PCB convenience we wired the inputs in reverse order to the labels; hence 7-in.
        const uint16_t result = read_analog_input(cnv_pin,  (7 - in));

        // Skip if invalid due to missing null bit.
        if (result > MAX_VALID) continue;

        const size_t idx = in + adc * 8;
        raw[idx] = result;
        current[idx] = ILSB * result;
      }
    }

    spi::vspi.endTransaction();
  }

}