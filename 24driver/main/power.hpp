#pragma once

#include "pins.hpp"

#include "Arduino.h"
#include "esp_adc_cal.h"

#include "memory.hpp"


namespace power {


  // 2 series LiPo cell recommended battery cutoff + 0.1V leeway.
  float min_battery_voltage = 6.5;

  // Time of the last nominal voltage (millis).
  unsigned long last_nominal_voltage_time = 0;

  // Warn of low voltage for 5 seconds.
  const unsigned long low_voltage_warn_duration = 5000;

  // Voltage and current values.
  uint16_t raw_voltage = 0;
  uint16_t raw_current = 0;
  // Voltage at the ACS output pin.
  float raw_current_voltage = 0.0;

  // Scaled voltage, current, and power use.
  volatile float voltage = 0.0;
  volatile float current = 0.0;
  volatile float power = 0.0;


  inline bool low_battery(){
    return voltage <= min_battery_voltage;
  }


  // Default analog read is 12 bit resolution, define the max value we can read.
  const uint16_t FULL_SCALE_CODE = 0xFFF;
  // Corresponding to the attenuated voltage (note that we still can't exceed 3.3V driving votlage).
  const float ADC_11DB_FULL_SCALE_VOLTAGE = 3.9;

  // The voltage measurement is taken after a voltage divider with 10kohm and 1.0kohm resistors.
  const float VOLTAGE_SCALE = (1.0 + 10.0) / 1.0;


  // We measure the current via the ACS712-20A hall effect sensor. The sensor outputs a voltage
  // with 0 current point at 2.5V and sensitivity of 100mV/A. The sensor output passes through
  // a 2.0kohm - 2.0kohm voltage divider to bring it to a range the ESP32 can read.

  // Sensitivity of 0.1V/A.
  const float CURRENT_SENSITIVITY = 0.1;
  // 0A point at 2.5V.
  const float CURRENT_ZERO_POINT = 2.5;
  // To get the voltage of the ACS712 output, we need the scale.
  const float CURRENT_VOLT_SCALE = (2.0 + 2.0) / 2.0;


  // The ESP32 has a 1100mV reference which is not perfectly accurate. Fortunately the
  // factory measures the reference and stores it. Use that calibration to correct reads.
  const uint32_t DEFAULT_VREF = 1100;
  esp_adc_cal_characteristics_t calibration;


  volatile float power_button_voltage = 0.0;

  // Store the last time the power button was pressed so we can turn off
  // on a long press.
  unsigned long power_last_press = 0;
  void IRAM_ATTR power_button_interrupt() {
    power_last_press = millis();
  }

  void turn_off(){
    digitalWrite(POWER_CTRL, LOW);
  }

  void turn_on() {
    digitalWrite(POWER_CTRL, HIGH);
  }

  void shutdown_on_long_press(){
    // Power management; if power button has been held for 3 second, turn off.
    if (power_button_voltage > 3.0 and ((millis() - power_last_press) > 3000)) {
      turn_off();
    }
  }

  void save_power_limits () {
    memory::set_float("min_batt", min_battery_voltage);
  }

  void load_power_limits() {
    memory::get_float("min_batt", min_battery_voltage);
  }



  void setup() {
    // Initialize the pin and turn the main mosfet on at startup.
    pinMode(POWER_CTRL, OUTPUT);
    turn_on();

    // Initialize power measuring pins.
    pinMode(VOLTAGE_IN, ANALOG);
    adcAttachPin(VOLTAGE_IN);
    analogSetPinAttenuation(VOLTAGE_IN, ADC_11db); // Full scale range of 3.9V

    pinMode(CURRENT_IN, ANALOG);
    adcAttachPin(CURRENT_IN);
    analogSetPinAttenuation(CURRENT_IN, ADC_11db); // Full scale range of 3.9V

    // Initialize the power button pin.
    pinMode(POWER_BTN, ANALOG);
    adcAttachPin(POWER_BTN);
    analogSetPinAttenuation(POWER_BTN, ADC_11db); // Full scale range of 3.9V

    // Use the power button to shut off if held down.
    attachInterrupt(POWER_BTN, power_button_interrupt, RISING);


    // Populate the calibration struct.
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_11db, ADC_WIDTH_12Bit, DEFAULT_VREF, &calibration);

    // Assume we're nominal on startup.
    last_nominal_voltage_time = millis();
    // We started the system with the power button pressed. If we don't reset this it might
    // set-off the shutdown sequence on the first loop.
    power_last_press = millis();
  }

  void update() {
    // Potential mistake in circuit, read the voltage on the power button pin.
    power_button_voltage = 0.001 * esp_adc_cal_raw_to_voltage(analogRead(POWER_BTN), &calibration);
    // Nope, diode burnt to a closed circuit so the button always appears pressed... Need solution
    // to charge capacitor bank without burning the diode.
    // shutdown_on_long_press();


    // Update power usage.

    // We'll use the function provided to help calibration. Especially helpful since
    // it grabs some burnt in efuses and does non-linear interpolation. Complicated!

    raw_voltage = analogRead(VOLTAGE_IN);
    voltage = VOLTAGE_SCALE * 0.001 * esp_adc_cal_raw_to_voltage(raw_voltage, &calibration);

    // Compute the current (in A) from the raw readout.
    raw_current = analogRead(CURRENT_IN);
    // Get the voltage on the hall effect chip's output pin.
    raw_current_voltage = CURRENT_VOLT_SCALE * 0.001 * esp_adc_cal_raw_to_voltage(raw_current, &calibration);
    // Compute the current from the chip's output voltage.
    current = (raw_current_voltage - CURRENT_ZERO_POINT) / CURRENT_SENSITIVITY;

    // Compute current power use, ignoring negative current values.
    power = current > 0.0 ? voltage * current : 0.0;


    // Shutdown on low voltage.
    if (low_battery()) {
      // Shutdown the ESP32 if we're past the warning duration on low power!
      if (millis() - last_nominal_voltage_time > low_voltage_warn_duration) {
        turn_off();
      }
    } else {
      // We're not running on low power, so keep nominal voltage time up to date.
      last_nominal_voltage_time = millis();
    }
  }
}