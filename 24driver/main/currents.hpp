#pragma once

#include "pins.hpp"
#include "spi.hpp"

#include "Arduino.h"


namespace currents {

  // Raw ADC values from the AD7689 (16bit).
  uint16_t raw[24] = {0};
  // Per channel current values in ampere; assuming accurate sense resistors.
  volatile float current[24] = {0.0};


  // Look-up for each converter's CNV pin.
  const gpio_num_t ADC_CNV_PINS[3] = {CURR0_CNV, CURR1_CNV, CURR2_CNV};


  // This reference is set via a 8.2k/2.0k @ 5V voltage divider.
  const float VREF = 0.98;
  // Voltage value that the least significant bit represents. The AD7689 chip is 16 bit.
  const float VLSB = VREF / 0xFFFF;
  // Current sense resistance for each motor driver; in ohms.
  const float SENSE_RESITANCE = 0.1;
  // Current increment per result bit; in ampere.
  const float ILSB = VLSB / SENSE_RESITANCE;


  // * CFG[13] overwrite register.
  const uint16_t CFG_OVERWRITE = 0b1 << 13;
  // * CFG[12:0] unipolar referenced to GND.
  const uint16_t CFG_UNIPOLAR_GND = 0b111 << 10;
  const uint16_t CFG_UNIPOLAR_COM = 0b110 << 10;
  // * CFG[9:7] select channel IN7 as the last to read in the sequence.
  const uint16_t CFG_IN7 = 0b111 << 7;
  // * CFG[6] full bandwidth @ 250kHz.
  const uint16_t CFG_FULL_BW = 0b1 << 6;
  // * CFG[5:3] use external reference, enable internal buffer, disable internal reference and temperature sensor.
  const uint16_t CFG_EXT_REFERENCE_BUFF_EN_TEMP_DIS = 0b111 << 3;
  const uint16_t CFG_EXT_REFERENCE_BUFF_EN_TEMP_EN = 0b011 << 3;
  // * CFG[2:1] enable sequencer; scan IN0 to IN7.
  const uint16_t CFG_SCAN_NO_TEMP = 0b11 << 1;
  // * CFG[0] do not read config contents.
  const uint16_t CFG_NO_READBACK = 0b1 << 0;

  // Set 14bit configuration register for sequantial reads of all channels.
  // Because we send a 16bit value, we need to shift the configuration 2 bits.
  const uint16_t READ_SEQUENCE =
    ( CFG_OVERWRITE
    | CFG_UNIPOLAR_GND
    | CFG_IN7
    | CFG_FULL_BW
    | CFG_EXT_REFERENCE_BUFF_EN_TEMP_DIS
    | CFG_SCAN_NO_TEMP
    | CFG_NO_READBACK)
    << 2;


  // For the follow-up transfers we need to keep the data line LOW, ie. set config with zeroes.
  const uint16_t NO_OVERWRITE = 0x0000;

  // Use 20MHz clock speed, MSB first, clock output on falling edge and capture data on rising edge (SPI_MODE0).
  const SPISettings SPI_SETTINGS = {20000000, SPI_MSBFIRST, SPI_MODE0};


  inline void setup() {
    for (size_t adc = 0; adc < 3; adc++){
      const gpio_num_t cnv_pin = ADC_CNV_PINS[adc];

      pinMode(cnv_pin, OUTPUT);
      digitalWrite(cnv_pin, HIGH);
    }
  }

  inline uint16_t read_after_conversion(const gpio_num_t cnv_pin, const uint16_t cfg){
    // To read after conversion we need to wait tCONV after the last rising edge
    // of the CNV line. We ensure the wait time with a delay after each conversion.

    // Begin data transfer.
    digitalWrite(cnv_pin, LOW);
    // Send configuration and get last conversion result.
    const uint16_t result = spi::vspi.transfer16(cfg);
    // Wait at least tACQ = 1.8us for the acquisition phase to complete.
    delayMicroseconds(2);
    // End data transfer and beging conversion.
    digitalWrite(cnv_pin, HIGH);
    // Wait at least tCONV = 3.2us for the conversion phase to complete.
    delayMicroseconds(4);

    return result;
  }

  inline void update(){
    // Setup SPI for these devices.
    spi::vspi.beginTransaction(SPI_SETTINGS);

    // Read each converter in turn.
    for (size_t adc = 0; adc < 3; adc++){
      const gpio_num_t cnv_pin = ADC_CNV_PINS[adc];

      // On each series of data reads, we'll pretend the chip just powered up. According to the
      // datasheet, this requires 1 dummy conversion to initialize the chip. On the 2nd transfer,
      // we need to send the configuration. This causes the chip to acquire the IN0 signal on the
      // 3rd transfer, however the conversion is not yet done, so we can ignore the result. The
      // conversion result is finally available on the 4th transfer.
      //
      // Use the mode RAC (read after conversion) so we don't have to worry on the speed of our
      // SPI interface. The minimum speed for RDC (read during conversion) is 16MHz, which the
      // ESP32 should handle, but it's easier not to care, especially if using the Arduino wrapper.
      //
      // Use the Channel sequencer timing, described on page 33 of the [manual]. Note that in this
      // case the pin selection bits (CFG[9:7]) define the *last* channel in the sequence. After
      // reading IN0 on the 4th transfer, subsequent transfers read IN1, IN2...
      //
      // [manual]: https://www.analog.com/media/en/technical-documentation/data-sheets/AD7682_7689.pdf


      // Send dummy transfer to power up the chip.
      /* ignore = */ read_after_conversion(cnv_pin, NO_OVERWRITE);
      // Send configuration value, and ignore first result.
      /* ignore = */ read_after_conversion(cnv_pin, READ_SEQUENCE);
      // Skip one more result while the chip aquires IN0.
      /* ignore = */ read_after_conversion(cnv_pin, NO_OVERWRITE);
      // Read raw values from the chip.
      for (size_t in = 0; in < 8; in++){
        const uint16_t result = read_after_conversion(cnv_pin, NO_OVERWRITE);
        const size_t idx = in + adc * 8;
        raw[idx] = result;
        current[idx] = ILSB * result;
      }
    }

    spi::vspi.endTransaction();
  }

}