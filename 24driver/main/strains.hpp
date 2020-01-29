#pragma once

#include "pins.hpp"
#include "i2c.hpp"


namespace strains {
  // H-bridge voltage (in volts) between 2 strain gauges and 3.3V/2 half point reference.
  volatile float strain[12] = {0.0};

  // Raw 16bit two's complement values from the ADC.
  int16_t raw[12] = {0};

  // Number of sample read from each chip, rolling over to 0 on overlfow.
  size_t samples[4] = {0};

  // The ADS1115 is 16bit two's complement. Full scal goes from -0x8000 to +0x7FFF.
  const uint16_t FULL_SCALE_CODE = 0x8000;

  // Conversion result register address
  const uint16_t CONVERSION_REGISTER = 0x00;

  // Config register address.
  const uint16_t CONFIG_REGISTER = 0x01;

  // Config[15]write start single conversion from power down state.
  const uint16_t START_CONV = 0b1 << 15;
  // Config[15]read, device is not performing a conversion (ie. result si ready if previously configured).
  const uint16_t DEVICE_READY = 0b1 << 15;

  // Config[14:12] multiplexer configuration.
  const uint16_t AIN0_AIN1 = 0b000 << 12;
  const uint16_t AIN0_AIN3 = 0b001 << 12;
  const uint16_t AIN1_AIN3 = 0b010 << 12;
  const uint16_t AIN2_AIN3 = 0b011 << 12;
  const uint16_t AIN0_GND = 0b100 << 12;
  const uint16_t AIN1_GND = 0b101 << 12;
  const uint16_t AIN2_GND = 0b110 << 12;
  const uint16_t AIN3_GND = 0b111 << 12;

  // Multiplexer setting for readying INx - IN3 from the ADC.
  const uint16_t MULTIPLEXER_CONFIG_FOR_IN[3] = {AIN0_AIN3, AIN1_AIN3, AIN2_AIN3};

  // Config[11:9] programmable gain amplifier set to +-0.256 full scale range.
  const uint16_t FSR_0V256 = 0b101 << 9;

  // Full scale range and scaling factor.
  const float FSR = 0.256;
  const float SCALE = FSR / FULL_SCALE_CODE;

  // Config[8] single shot mode.
  const uint16_t SNGL_SHOT = 0b1 << 8;

  // Config[7:5] 860SPS data rate.
  const uint16_t RATE_860SPS = 0b111 << 5;

  // Config[4:0] we don't use comparator/alert pin so we'll disable it.
  const uint16_t DISABLE_COMP = 0b00011 << 0;


  // Configuration except for the input multiplexer.
  const uint16_t CONFIG_SINGLE_READ_860SPS_0V256
    = START_CONV
    | FSR_0V256
    | SNGL_SHOT
    | RATE_860SPS
    | DISABLE_COMP;



  // Configuration set-up for readying input x, or 0xFFFF if not set. For each of the 4 chips.
  const size_t UNCONFIGURED = 0xFFFF;
  size_t input_configured[4] = {UNCONFIGURED};

  void update(){
    for (size_t adc = 0; adc < 4; adc++) {
      // If the chip is not configured, then initialize conversion for IN0.
      if (input_configured[adc] == UNCONFIGURED) {
        const uint16_t config = CONFIG_SINGLE_READ_860SPS_0V256 | MULTIPLEXER_CONFIG_FOR_IN[0];
        i2c::write_int16_to(STRAIN_BASE_ADDRESS + adc, CONFIG_REGISTER, config);
        input_configured[adc] = 0;

      // Otherwise the chip was configured to read an input. Read the conversion
      // and configure to read the next input, looping around to IN0 after IN3.
      } else {
        const size_t in = input_configured[adc];

        // Check if the chip is ready, and configured correctly.
        const auto current_config = i2c::read_int16_from(STRAIN_BASE_ADDRESS + adc, CONFIG_REGISTER);

        // Read the result if the conversion is done and the chip is idle.
        if (current_config & DEVICE_READY) {
          // Check that the scaling and multiplexer are configured as expected.
          if (
            ((current_config & (0b111<<9)) == FSR_0V256) and
            ((current_config & (0b111<<12)) == MULTIPLEXER_CONFIG_FOR_IN[in])
          ) {
            // Read the result.
            const int result = i2c::read_int16_from(STRAIN_BASE_ADDRESS + adc, CONVERSION_REGISTER);
            const size_t idx = in + adc * 3;
            raw[idx] = result;
            strain[idx] = SCALE * result;
            samples[adc] += 1;

            // Configure the next read; but first set the uncofigured flag in case configuration fails.
            input_configured[adc] = UNCONFIGURED;
            const size_t next_in = (in+1) % 3;
            const uint16_t config = CONFIG_SINGLE_READ_860SPS_0V256 | MULTIPLEXER_CONFIG_FOR_IN[next_in];
            i2c::write_int16_to(STRAIN_BASE_ADDRESS + adc, CONFIG_REGISTER, config);
            // If succsefull, set the configured flag with the next channel.
            input_configured[adc] = next_in;

          //  Otherwise re-configure for the current measure.
          } else {
            // Set the uncofigured flag until we succsefully write.
            input_configured[adc] = UNCONFIGURED;
            // Configure for the current channel.
            const uint16_t config = CONFIG_SINGLE_READ_860SPS_0V256 | MULTIPLEXER_CONFIG_FOR_IN[in];
            i2c::write_int16_to(STRAIN_BASE_ADDRESS + adc, CONFIG_REGISTER, config);
            // If succsefull, set the configured flag with the next channel.
            input_configured[adc] = in;
          }

        // If this device is not ready, then skip a read and wait for the next update call.
        } else {
          continue;
        }
      }
    }
  }

  // These don't really need a setup, just run update once.
  void setup(){
    update();
  }

}