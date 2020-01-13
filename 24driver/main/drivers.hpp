#pragma once

#include "pins.hpp"
#include "i2c.hpp"

#include <cmath>

namespace drivers {

  // Speed to drive each motor scaled as [-1.0, +1.0].
  float power[24] = {0.0};

  // Whether to short the motor (break) on the off-cycle of the PWM, or
  // leave free-wheeling (coast). Turns out, slow current decay (break)
  // is the much better option. Should probably always leave this `false`.
  bool coast = false;

  // Minimum and maximum PWM powers (1 bit away from fully on or fully off).
  const float min_pow = 1.0 / 256;
  const float max_pow = 1.0 - min_pow;


  // Mode registers.
  const uint8_t MODE1 = 0x00;
  const uint8_t MODE2 = 0x01;
  // Turn oscilator on, and LED all call off (last part is not necessary).
  const uint8_t MODE1_OSC_ON_ALLCALL_OFF = 0b00000000;

  // These 16 registers control the LED brightness.
  const uint8_t PWM0 = 0x02;
  // These 4 registers control the state of each LED channel.
  // Every 2 bits mean:
  //   * 0b00 channel is off.
  //   * 0b01 channel is on.
  //   * 0b10 channel controlled by PWM register.
  //   * 0b11 channel controlled by PWM and group PWM.
  const uint8_t LEDOUT0 = 0x14;

  // The TLC59116 has some special logic to increment registers. This is specified
  // by the first 3 bits of the "control register".
  const uint8_t AUTOINCREMENT = 0b10000000;


  // Default values for group PWM control. We'll set all registers from PWM and LEDOUT in a single
  // I2C transaction. We need these values since these registers are in the middle.
  const uint8_t GRPPWM_DEFAULT = 0b11111111;
  const uint8_t GRPFREQ_DEFAULT = 0b00000000;

  inline void setup(){
    // Startup all LED drivers.
    for (size_t driver = 0; driver < 3; driver++) {
      i2c::write_to(PWM_BASE_ADDRESS + driver, MODE1, MODE1_OSC_ON_ALLCALL_OFF);
    }
    // Wait for oscilators to turn on.
    delayMicroseconds(500);
  }

  // Send drive commands to the PWM drivers.
  inline void update(){
    // There are 3 LED drivers, each having 16 PWM outputs, controlling 8 motor drivers.
    for (size_t driver = 0; driver < 3; driver++) {

      // We'll set 22 registers in a single I2C transaction.
      // * 0-15: PWM for each channel.
      // * 16: GRPPWM.
      // * 17: GRPFREQ.
      // * 18-21: LEDOUT; 4 channels per register.
      uint8_t data[22] = {0};
      data[16] = GRPPWM_DEFAULT;
      data[17] = GRPFREQ_DEFAULT;

      for (size_t i = 0; i < 8; ++i) {
        // Note that turning on an LED channel will sink current, turning
        // the pull-up HIGH state to LOW. We have to invert logic outputs.

        const float pow = power[i + 8 * driver];
        const float abs_pow = std::abs(pow);
        const bool reverse = pow < 0.0;

        // Pluck the relevant registers.
        uint8_t & pwm1 = data[i*2];
        uint8_t & pwm2 = data[i*2 + 1];
        uint8_t & ledout_reg = data[18 + i / 2];
        const size_t ledout_bit_offset = (i % 2) * 4;

        // Set this channels power. With special cases for fully off and fully on.

        // Motor fully off.
        if (abs_pow <= min_pow) {

          // Turn in2 and in1 fully on (turning the motor driver inputs LOW).
          if (coast) ledout_reg |= 0b0101 << ledout_bit_offset;
          // Otherwise turn motor driver inputs HIGH, shorting the motor.
          else ledout_reg |= 0b0000 << ledout_bit_offset;

          // Doesn't really matter what PWMs are in this case.


        // Motor fully on.
        } else if (abs_pow >= max_pow) {

          // Also doesn't matter whether to coast, because we drive continusly.
          if (reverse) ledout_reg |= 0b0001 << ledout_bit_offset;
          else ledout_reg |= 0b0100 << ledout_bit_offset;

          // PWM doesn't matter in this case either.


        // Motor at fractional speed.
        } else {

          // Leave the motor to run freely during the off cycle of the PWM.
          if (coast) {
            // Control one PWM signal (via inverted logic), and turn the other motor driver input LOW.
            if (reverse) {
              ledout_reg |= 0b1001 << ledout_bit_offset;
              pwm2 = static_cast<uint8_t>((1.0 - abs_pow) * 256);
            } else {
              ledout_reg |= 0b0110 << ledout_bit_offset;
              pwm1 = static_cast<uint8_t>((1.0 - abs_pow) * 256);
            }


          // Short-circuit the motor during the off cycle of the PWM.
          } else {
            // Control the other PWM signal (via doubly inverted logic), and turn the motor driver input HIGH.
            if (reverse) {
              ledout_reg |= 0b0010 << ledout_bit_offset;
              pwm1 = static_cast<uint8_t>(abs_pow * 256);
            } else {
              ledout_reg |= 0b1000 << ledout_bit_offset;
              pwm2 = static_cast<uint8_t>(abs_pow * 256);
            }
          }
        }

      }


      // Send data to the drive.
      const uint8_t address = PWM_BASE_ADDRESS + driver;

      // TODO: handle i2c errors somewhere.
      i2c::write_bytes_to<22>(address, PWM0 | AUTOINCREMENT, data);

    }
  }

}