#pragma once

#include "Arduino.h"
#include "Wire.h"

#include "pid6drive_registers.hpp"
#include "i2c.hpp"
#include "pid.hpp"
#include "utils.hpp"


struct PID6DriveConfig {
  bool enable[6];
  bool invert[6];
  bool seeking[6];
  int8_t output_index[6];
  int8_t input_index[6];

  int16_t pid_p[6];
  int16_t pid_i_time[6];
  int16_t pid_d_time[6];
  int16_t pid_threshold[6];
  int16_t pid_overshoot[6];

  PID6DriveConfig(){
    const HysterisisPID8bit pid_defaults = {};

    for (int i = 0; i < 6; i++){
      enable[i] = false;
      invert[i] = false;
      seeking[i] = false;
      output_index[i] = i;
      input_index[i] = i;

      pid_p[i] = pid_defaults.p;
      pid_i_time[i] = pid_defaults.i_time;
      pid_d_time[i] = pid_defaults.d_time;
      pid_threshold[i] = pid_defaults.threshold;
      pid_overshoot[i] = pid_defaults.overshoot;
    }
  }

  void set_all_pid_params(
    const int16_t p /* 8bit out / 10bit in (max 5bit value) */,
    const int16_t i_time /* millis */,
    const int16_t d_time /* millis */,
    const int16_t threshold /* 10bit in */,
    const int16_t overshoot /* 10bit in */
  ){
    for (int i = 0; i < 6; i++){
      pid_p[i] = p;
      pid_i_time[i] = i_time;
      pid_d_time[i] = d_time;
      pid_threshold[i] = threshold;
      pid_overshoot[i] = overshoot;
    }
  }
};

struct PID6Drive {
private:
  // Configuration currently on the chip.
  PID6DriveConfig chip_config;
public:
  // Configuration to be sent to the chip.
  PID6DriveConfig config;

  int targets[6] = {};
  // TODO: Start positions at -1
  int positions[6] = {};
  int errors[6] = {};
  int drive_power[6] = {};
  int drive_time[6] = {};
  int loop_interval = -1;
  int drive_i2c_errors = 0;

  int communication_errors = 0;

  // Chip I2C address.
  const byte address;

  PID6Drive(const byte address) : address(address) {}


  // Configuration
  // -------------

  void configure() {
    // For each drive unit, if the parameter on the chip is different, then
    // send the new value.
    for (int i = 0; i < 6; i++){

#define CHECK_AND_SEND(field, register, write_func) \
      if(chip_config.field[i] != config.field[i]) { \
        if(write_func(address, typed_add(PID6DriveRegister::register##_0, i), config.field[i])) communication_errors += 1; \
        else chip_config.field[i] = config.field[i]; \
      }

      CHECK_AND_SEND(enable, ENABLE, write_to);
      CHECK_AND_SEND(invert, INVERT, write_to);
      CHECK_AND_SEND(seeking, SEEKING, write_to);
      CHECK_AND_SEND(output_index, OUTPUT_IDX, write_to);
      CHECK_AND_SEND(input_index, INPUT_IDX, write_to);

      CHECK_AND_SEND(pid_p, SET_PID_P, write_int16_to);
      CHECK_AND_SEND(pid_i_time, SET_PID_I_TIME, write_int16_to);
      CHECK_AND_SEND(pid_d_time, SET_PID_D_TIME, write_int16_to);
      CHECK_AND_SEND(pid_threshold, SET_PID_THRESHOLD, write_int16_to);
      CHECK_AND_SEND(pid_overshoot, SET_PID_OVERSHOOT, write_int16_to);

#undef CHECK_AND_SEND

    }

    if(write_to(address, PID6DriveRegister::SET_CONFIGURED, 1)) communication_errors += 1;
  }

  // The chip is unconfigured at startup after a potential restart. Check every
  // loop iteration if it restarted, and reconfigure.
  void check_and_configure(){
    // Read the configuration flag.
    byte configured = 0xFF;
    if(read_from(address, PID6DriveRegister::GET_CONFIGURED, configured)) communication_errors += 1;
    // Reset chip config values to default after an assumed chip restart; ignore comm errors though.
    else if (configured != 1) chip_config = PID6DriveConfig();

    // Configure chip using the current config values, if any are different than chip values.
    configure();
  }


  void read_values() {
    // Read how long a loop takes on the driver chip.
    if(read_int16_from(address, PID6DriveRegister::GET_LOOP_INTERVAL, loop_interval)) communication_errors += 1;

    // Get any new i2c errors.
    byte new_i2c_errors = 0xFF;
    if(read_from(address, PID6DriveRegister::GET_RESET_I2C_ERRORS, new_i2c_errors)) communication_errors += 1;
    else if (new_i2c_errors != 0xFF) drive_i2c_errors += new_i2c_errors;

    if(read_errors()) communication_errors += 1;
    if(read_positions()) communication_errors += 1;
  }


  void send_commands(const int elapsed_millis) {

    if(send_targets()) communication_errors += 1;
    if(send_drive_commands()) communication_errors += 1;

    // Subtract elasped time from reamining drive times.
    for (int i = 0; i < 6; i++){
      if (drive_time[i] > elapsed_millis) drive_time[i] -= elapsed_millis;
      else drive_time[i] = 0;
    }
  }


  bool read_positions(){
    Wire.beginTransmission(address);
    Wire.write(static_cast<byte>(PID6DriveRegister::GET_ALL_INPUTS));
    if(Wire.endTransmission(false)) {
      nr_wire_errors += 1;
      return true;
    }
    delayMicroseconds(20); // wait for arduino to process.
    if (Wire.requestFrom(address, 12u) != 12u) {
      nr_wire_errors += 1;
      return true;
    }
    for (int i = 0; i < 6; i++) {
      positions[i] = read_int16();
    }
    return false;
  }

  bool read_errors(){
    Wire.beginTransmission(address);
    Wire.write(static_cast<byte>(PID6DriveRegister::GET_ALL_ERRORS));
    if(Wire.endTransmission(false)) {
      nr_wire_errors += 1;
      return true;
    }
    delayMicroseconds(10); // wait for arduino to process.
    if (Wire.requestFrom(address, 6u) != 6u) {
      nr_wire_errors += 1;
      return true;
    }
    for (int i = 0; i < 6; i++) {
      errors[i] = Wire.read();
    }
    return false;
  }

  bool send_targets(){
    Wire.beginTransmission(address);
    Wire.write(static_cast<byte>(PID6DriveRegister::SET_ALL_TARGETS));
    for (int i = 0; i < 6; i++) {
      write_int16(targets[i]);
    }
    if(Wire.endTransmission()) {
      nr_wire_errors += 1;
      return true;
    }
    return false;
  }

  bool send_drive_commands(){
    Wire.beginTransmission(address);
    Wire.write(static_cast<byte>(PID6DriveRegister::DRIVE_ALL));
    for (int i = 0; i < 6; i++) {
      write_int16(drive_power[i]);
      write_int16(drive_time[i]);
    }
    if(Wire.endTransmission()) {
      nr_wire_errors += 1;
      return true;
    }
    return false;
  }
};