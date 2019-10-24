#include <stdio.h>

#include "Esp.h"
#include "Arduino.h"
#include "Wire.h"

#include "esp_attr.h"
#include "driver/gpio.h"


#include "sdkconfig.h"

#include "pid6drive_interface.hpp"
#include "lcd.hpp"
#include "pins.hpp"
#include "onboardpid.hpp"
#include "joints.hpp"
#include "utils.hpp"
#include "ui.hpp"
#include "power.hpp"
#include "memory.hpp"

// Time keeping
// ------------

unsigned long last_micros = 0;

// Limit loop frequency to Hz.
#define LOOP_FREQUENCY 200
const int loop_delay_millis = 1000 / LOOP_FREQUENCY;
// How long it takes between loop runs.
unsigned long loop_interval_micros = 0;
// How long it takes to do processing during a loop.
unsigned long loop_active_micros = 0;



// Setup
// -----

void setup(){
  // Turn device on, and setup up power pins and button.
  power::setup();


  // Set 10bit analog read resolution as it's what I chose for the PID driver class. Putting
  // the ESP32 in line with the Arduino Nano boards. Should be fine for voltage and current.
  analogReadResolution(10);
  analogSetWidth(10);


  // Setup the on-board PID driver.
  onboardpid::setup();


  // PID parameters configuration
  {
    using namespace joints;

    // For the onboard driver.
    for (int i = 0; i < 2; i++) onboardpid::pids[i] = HysterisisPID8bit(p, i_time, d_time, threshold, overshoot);
    // For the external drivers.
    pid6drive_0.config.set_all_pid_params(p, i_time, d_time, threshold, overshoot);
    pid6drive_1.config.set_all_pid_params(p, i_time, d_time, threshold, overshoot);
    pid6drive_2.config.set_all_pid_params(p, i_time, d_time, threshold, overshoot);

  }


  // Comms
  Wire.begin(SDA_PIN, SCL_PIN, 400000);
  Serial.begin(115200);


  // Initialize LCD.
  ui::lcd.begin();


  // Setup inputs.
  ui::wheel_0.begin();
  ui::wheel_1.begin();
  ui::button_0.begin();
  ui::button_1.begin();


  // Setup fingertip pressure sensors.
  joints::ads_0.begin();
  joints::ads_1.begin();


  // Initialize memory and load config.
  memory::init();
  memory::load();


  // External PIDs
  joints::pid6drive_0.configure();
  joints::pid6drive_1.configure();
  joints::pid6drive_2.configure();


  // Initialize time keeping.
  last_micros = micros();
}


// Main loop
// ---------

// Instead of sleeping at the end of a loop tick, spin
// the fast loop until the next tick time.
void fast_loop();

void loop(){
  // Time keeping
  // ------------

  unsigned long loop_start_micros = micros();
  const int elapsed_micros = loop_start_micros - last_micros;
  last_micros = loop_start_micros;
  const int elapsed_millis = (elapsed_micros + 500) / 1000;
  // Exponentially average the loop time with gamma = 0.8.
  loop_interval_micros = (loop_interval_micros * 80 + elapsed_micros * 20 + 50) / 100;

  ui::esp_interval_millis = (loop_interval_micros + 500) / 1000;

  // Power management
  // ----------------

  power::shutdown_on_long_press();
  power::measure_and_update(elapsed_millis);


  // Comms
  // -----

  // Restart serial communication in case something was faulty.
  Wire.begin(SDA_PIN, SCL_PIN, 400000);


  // Joint control
  // -------------

  // Update motor drivers configuration.
  joints::pid6drive_0.check_and_configure();
  joints::pid6drive_1.check_and_configure();
  joints::pid6drive_2.check_and_configure();

  // Get positions from the motor drivers.
  joints::pid6drive_0.read_values();
  joints::pid6drive_1.read_values();
  joints::pid6drive_2.read_values();

  // Update joints state.
  joints::update(elapsed_millis);

  // Update the inputs and outputs of the on-board PID.
  onboardpid::loop_tick(elapsed_millis);
  // Update targets on the motor drivers.
  joints::pid6drive_0.send_commands(elapsed_millis);
  joints::pid6drive_1.send_commands(elapsed_millis);
  joints::pid6drive_2.send_commands(elapsed_millis);


  // Sensors
  // -------

  joints::ads_0_sample_count.update_sample_rate(elapsed_millis);
  joints::ads_1_sample_count.update_sample_rate(elapsed_millis);


  // Display and UI
  // --------------

  // Handle inputs and write the lcd text.
  ui::update();

  ui::lcd.update();


  // Fast loop
  // ---------

  // Exponentially average the loop active time with gamma = 0.8.
  loop_active_micros = (loop_active_micros * 80 + (micros()-loop_start_micros) * 20 + 50) / 100;

  // Run fast loop at least once per slow loop.
  fast_loop();

  // We want the main loop at loop frequency, but we need a faster loop
  // to handle data communication with the pressure sensors.
  while(((micros()-loop_start_micros) / 1000) < loop_delay_millis) fast_loop();
}

void fast_loop(){
  joints::ads_0_sample_count.reads += joints::ads_0.update();
  joints::ads_1_sample_count.reads += joints::ads_1.update();
}



void shutdown(){
  // Close config writer.
  memory::close();
  // Turn the power mosfet off to cut-off everything.
  power::turnoff();
}