#pragma once
// Driver for ADS1115 ADC converter: https://www.ti.com/lit/ds/symlink/ads1114.pdf


#include "i2c.hpp"

#include "Arduino.h"
#include "driver/gpio.h"

#define ADS_ADDRESS 0x48

#define ADS_RESULT 0x00
#define ADS_CONFIG 0x01
#define ADS_LO_THRESH 0x02
#define ADS_HI_THRESH 0x03

// Configuration flags.
#define ADS_START_SINGLE (0b1 << 15)

#define ADS_AIN0_AIN1 (0b000 << 12)
#define ADS_AIN0_AIN3 (0b001 << 12)
#define ADS_AIN1_AIN3 (0b010 << 12)
#define ADS_AIN2_AIN3 (0b011 << 12)
#define ADS_AIN0_GND (0b100 << 12)
#define ADS_AIN1_GND (0b101 << 12)
#define ADS_AIN2_GND (0b110 << 12)
#define ADS_AIN3_GND (0b111 << 12)

#define ADS_FSR_6V144 (0b000 << 9)
#define ADS_FSR_4V096 (0b001 << 9)
#define ADS_FSR_2V048 (0b010 << 9)
#define ADS_FSR_1V024 (0b011 << 9)
#define ADS_FSR_0V512 (0b100 << 9)
#define ADS_FSR_0V256 (0b101 << 9)

#define ADS_SINGLE_MODE (0b1 << 8)

#define ADS_RATE_8SPS (0b000 << 5)
#define ADS_RATE_16SPS (0b001 << 5)
#define ADS_RATE_32SPS (0b010 << 5)
#define ADS_RATE_64SPS (0b011 << 5)
#define ADS_RATE_128SPS (0b100 << 5)
#define ADS_RATE_250SPS (0b101 << 5)
#define ADS_RATE_475SPS (0b110 << 5)
#define ADS_RATE_860SPS (0b111 << 5)

#define ADS_TRADITIONAL_COMP (0b0 << 4)
#define ADS_WINDOW_COMP (0b1 << 4)

#define ADS_ACTIVE_LOW (0b0 << 3)
#define ADS_ACTIVE_HIGH (0b1 << 3)

#define ADS_NONLATCHING (0b0 << 2)
#define ADS_LATCHING (0b1 << 2)

#define ADS_ASSERT_ONE 0b00
#define ADS_ASSERT_TWO 0b01
#define ADS_ASSERT_THREE 0b10
#define ADS_DISABLE_ALERT 0b11



void IRAM_ATTR ads_ready_interrupt(void * arg);

struct ADS1115_3In_1Ref {
  const gpio_num_t ready_pin;
  const byte address;

  float in0 = 0.0;
  float in1 = 0.0;
  float in2 = 0.0;

  bool result_began = false;
  bool result_ready = false;
  int begin_pair = ADS_AIN0_AIN3;
  int result_pair = ADS_AIN0_AIN1; // Defaults to this.

  const float scale = 0.256 / (1 << 15);

  ADS1115_3In_1Ref(gpio_num_t ready_pin, byte address) : ready_pin(ready_pin), address(address) {}


  void begin(){
    pinMode(ready_pin, INPUT_PULLDOWN);
  }

  bool begin_read_pair(const int pair) {
    // Ensure it is not currently perfoming a conversion.
    int prev_config = -1;
    if (read_int16_from(address, ADS_CONFIG, prev_config)) return true;


    // Device is currently performing a conversion; abort and try again next time.
    if (not (prev_config & (0b1 << 15))) return true;


    // The conversion-ready function of the ALERT/RDY pin is
    // enabled by setting the Hi_thresh register MSB to 1 and
    // the Lo_thresh register MSB to 0.
    if (write_int16_to(address, ADS_HI_THRESH, 0b1 << 15)) return true;
    if (write_int16_to(address, ADS_LO_THRESH, 0b0 << 15)) return true;

    const int config =
      ADS_START_SINGLE |
      pair |
      ADS_FSR_0V256 |
      ADS_SINGLE_MODE |
      ADS_RATE_860SPS |
      ADS_TRADITIONAL_COMP |
      ADS_ACTIVE_HIGH |
      ADS_ASSERT_ONE;

    if(write_int16_to(address, ADS_CONFIG, config)) return true;

    // Succesfully requested some results.
    result_pair = pair;

    // Return 0 when no errors occured.
    return false;
  }

  /* Update the ads chip, return true if a read was succesfull. */
  bool update() {
    bool success = false;

    // Read results if ready.
    if (digitalRead(ready_pin)) {

      // Conversion ready.
      int result = 0;

      // Reset flags even if read will be unsuccessfull.
      result_began = false;

      switch(result_pair){
        // In either case if we don't succesfully read the result, we remain at
        // the same pair and will begin another read.

        case ADS_AIN0_AIN3: {
          if (read_int16_from(address, ADS_RESULT, result)) break;
          in0 = result * scale;
          begin_pair = ADS_AIN1_AIN3;
          success = true;
          break;
        }
        case ADS_AIN1_AIN3: {
          if (read_int16_from(address, ADS_RESULT, result)) break;
          in1 = result * scale;
          begin_pair = ADS_AIN2_AIN3;
          success = true;
          break;
        }
        case ADS_AIN2_AIN3: {
          if (read_int16_from(address, ADS_RESULT, result)) break;
          in2 = result * scale;
          begin_pair = ADS_AIN0_AIN3;
          success = true;
          break;
        }
      }
    }


    if (not result_began) {
      // We began a read if it was not an error.
      if (begin_read_pair(begin_pair) == 0) result_began = true;
    }

    return success;
  }

};
