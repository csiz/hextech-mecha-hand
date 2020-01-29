#pragma once

#include "pid.hpp"
#include "memory.hpp"

#include <stdio.h>


namespace state {

  struct Channel {
    // Position of the potentiometer.
    float position = 0.0;
    // Minimum position reached on reverse power.
    float min_position = 0.0;
    // Maximum position reached on forward power.
    float max_position = 1.0;
    // Power to send to the motor, -1.0 meaning full reverse to +1.0 meaning full forward.
    float power = 0.0;
    // Current used by motor in amps.
    float current = 0.0;
    // Position to seek, or -1 to disable.
    float seek = -1.0;
    // Power offset in addition to the seek position.
    float power_offset = 0.0;
    // To adjust for how cables are connected to the robot, we can redefine
    // the direction of the outputs and inputs. Pick a forward direction
    // in the model and then adjust the parameters below such that power +1
    // moves the joint forward and power -1 moves the joint backward.
    bool reverse_output = false;
    // Adjust the input direction such that moving forward increases position
    // towards 1.0, and reverse decreases position to 0.0.
    bool reverse_input = false;
    // PID state for controlling power.
    PID pid;
  };

  struct Gauge {
    // Strain in chosen units.
    float strain = 0.0;
    // Voltage when there's no strain applied.
    float zero_offset = 0.0;
    // Scaling between strain units and H bridge voltage.
    float coefficient = 1.0;
  };

  struct State {
    // Chip power info (filtered).
    float current = 0.0;
    float voltage = 0.0;
    float power = 0.0;
    float energy = 0.0;

    // Timing.
    float fps = 0.0;
    float max_loop_duration = 0.0;
    unsigned long update_time = 0.0;

    // 24 PID driver channels.
    Channel channels[24];

    // 12 strain gauge sensors.
    Gauge gauges[24];
  };

  // The one state to rule them all.
  State state;

  // Reset drive power to 0 and disable seeking.
  void halt_drivers(){
    for (size_t i = 0; i < 24; i++){
      state.channels[i].power_offset = 0.0;
      state.channels[i].seek = -1.0;
      state.channels[i].power = 0.0;
    }
  }


  void save_state_params () {
    using namespace memory;

    // Use this array to format keys with the index for each channel.
    char key[max_key];

    for (size_t i = 0; i < 24; i++) {
      auto const& channel = state.channels[i];

      snprintf(key, max_key, "c%2d-min-pos", i);
      set_float(key, channel.min_position);

      snprintf(key, max_key, "c%2d-max-pos", i);
      set_float(key, channel.max_position);

      snprintf(key, max_key, "c%2d-rev-out", i);
      set_bool(key, channel.reverse_output);

      snprintf(key, max_key, "c%2d-rev-inp", i);
      set_bool(key, channel.reverse_input);

    }

    // TODO: save gauges config.

    commit();
  }

  void load_state_params() {
    using namespace memory;

    // Use this array to format keys with the index for each channel.
    char key[max_key];

    for (size_t i = 0; i < 24; i++) {
      auto & channel = state.channels[i];

      snprintf(key, max_key, "c%2d-min-pos", i);
      get_float(key, channel.min_position);

      snprintf(key, max_key, "c%2d-max-pos", i);
      get_float(key, channel.max_position);

      snprintf(key, max_key, "c%2d-rev-out", i);
      get_bool(key, channel.reverse_output);

      snprintf(key, max_key, "c%2d-rev-inp", i);
      get_bool(key, channel.reverse_input);

    }

    // TODO: load gauges configs.
  }

  void setup(){
    load_state_params();
  }
}