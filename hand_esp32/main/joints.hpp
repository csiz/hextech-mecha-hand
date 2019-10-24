#pragma once

#include "utils.hpp"
#include "onboardpid.hpp"
#include "ads1115.hpp"
#include "pid6drive_interface.hpp"
#include "pins.hpp"


namespace joints {

  // External PID drivers
  // --------------------

  PID6Drive pid6drive_0(PID6DRIVE_ADDRESS + 0b00);
  PID6Drive pid6drive_1(PID6DRIVE_ADDRESS + 0b01);
  PID6Drive pid6drive_2(PID6DRIVE_ADDRESS + 0b10);


  // Sensors
  // -------

  ADS1115_3In_1Ref ads_0(ADS0_ALERT, ADS0_ADDRESS);
  ADS1115_3In_1Ref ads_1(ADS1_ALERT, ADS1_ADDRESS);

  struct SampleCounter {
    unsigned int reads = 0;
    unsigned int last_reads = 0;
    // Expoenntially averaged sample rate (Hz).
    float rate = 0.0;

    void update_sample_rate(const int elapsed_millis) {
      // Skip divide by 0 cases.
      if (elapsed_millis == 0) return;

      // Exponentially average the sampling rate for the ADS fingertip sensors.
      rate = 0.9 * rate + 0.1 * ((reads - last_reads) * 1000 / elapsed_millis);
      last_reads = reads;
    }
  };

  SampleCounter ads_0_sample_count;
  SampleCounter ads_1_sample_count;


  // PID parameters
  // --------------

  int p = 2;
  int i_time = 2000; // millis
  // Capacitor adds 10ms momentum, and exp avg adds ~20ms lag.
  int d_time = 30; // millis
  int threshold = 4;
  int overshoot = 8;


  // Driver chips
  // ------------

  enum struct Chip : int {
    NONE,
    ESPMAIN,
    DRIVE_0,
    DRIVE_1,
    DRIVE_2,
    _MAXVALUE
  };

  const int available_on_chip[] = {0, 2, 6, 6, 6};

  const char * chip_name(Chip chip) {
    switch(chip){
      case Chip::NONE: return "none";
      case Chip::ESPMAIN: return "espmain";
      case Chip::DRIVE_0: return "drive-0";
      case Chip::DRIVE_1: return "drive-1";
      case Chip::DRIVE_2: return "drive-2";
      case Chip::_MAXVALUE: return "invalid";
    }

    return "undefined";
  }


  // Abstract joint state
  // --------------------

  struct Joint {
    // Note that inputs and targets are 10bit values.

    // Chip this joint is on, and on that chip which input and which output index.
    Chip chip = Chip::NONE;
    int input_index = -1;
    int output_index = -1;

    // Current position, power, and target seeking.
    int position = -1;

    int drive_power = 0;
    int drive_time = 0;

    bool seeking = false;
    int target = 512;

    // Minimum and maximum positions, should be set based on the physical model.
    int min_pos = 5;
    int max_pos = 1020;

    // If the position are inverted; ie. finger is curled when at min position instead of max.
    int inverted_position = false;

    // Whether output direction is inverted; ie. the PID controllers drive the motor away from the target.
    int inverted_output = false;
  };

  #define NUM_JOINTS 20

  Joint joints[NUM_JOINTS];

  const Joint default_joint;

  const char * joint_name(size_t index){
    switch(index){
      case 0: return "index-curl";
      case 1: return "index-flex";
      case 2: return "index-side";

      case 3: return "middle-curl";
      case 4: return "middle-flex";
      case 5: return "middle-side";

      case 6: return "ring-curl";
      case 7: return "ring-flex";
      case 8: return "ring-side";

      case 9: return "pinky-curl";
      case 10: return "pinky-flex";
      case 11: return "pinky-side";

      case 12: return "thumb-curl";
      case 13: return "thumb-flex";
      case 14: return "thumb-side";

      case 15: return "thumb-abduct";
      case 16: return "pinky-abduct";

      case 17: return "wrist-roll";
      case 18: return "wrist-pitch";
      case 19: return "wrist-yaw";
    }

    return "undefined";
  }


  // Abstract to hardware update
  // ---------------------------

  void update(const int elapsed_millis){

    // Need to keep count of how many joints are assigned to each chip so
    // we know the index on the chip.
    int assigned_on_chip[] = {0, 0, 0, 0, 0};


    for (int i = 0; i < NUM_JOINTS; i++){
      auto & joint = joints[i];


      // Count how many are assigned to the current chip.
      int & assigned = assigned_on_chip[typed(joint.chip)];
      assigned += 1;
      const int available = available_on_chip[typed(joint.chip)];

      // Skip this joint if not enough space on the chip.
      if (assigned > available) {
        // Reset position to invalid.
        joint.position = -1;
        continue;
      }

      // Chip indexing starts at 0.
      int index_on_chip = assigned - 1;

      // Invert the control if the output and position are unsynced.
      bool joint_inverted_control = (joint.inverted_output != joint.inverted_position);

      // Don't need to do nothing for no chip.
      if (joint.chip == Chip::NONE or joint.chip == Chip::_MAXVALUE) /* skip */;

      // If on the main chip, then we can immediately update values.
      else if(joint.chip == Chip::ESPMAIN) {
        // Enable if the joint is assigned to the chip.
        onboardpid::enable[index_on_chip] = true;
        // Set indexes.
        onboardpid::input_idx[index_on_chip] = joint.input_index;
        onboardpid::output_idx[index_on_chip] = joint.output_index;
        // Invert power only if output is inverted.
        onboardpid::drive_power[index_on_chip] = joint.drive_power * (joint.inverted_output ? -1 : +1);
        onboardpid::drive_time[index_on_chip] = joint.drive_time;
        // Set seeking target.
        onboardpid::seeking[index_on_chip] = joint.seeking;
        onboardpid::targets[index_on_chip] = joint.target;

        onboardpid::invert[index_on_chip] = joint_inverted_control;

        // Grab the position (after we've applied the settings).
        joint.position = onboardpid::get_input(index_on_chip);
      }
      // The reamining options are the external drivers.
      else {
        PID6Drive * pid6drive_chip =
          joint.chip == Chip::DRIVE_0 ? &pid6drive_0 :
          joint.chip == Chip::DRIVE_1 ? &pid6drive_1 :
          joint.chip == Chip::DRIVE_2 ? &pid6drive_2 :
          nullptr; // should'nt happen.

        // But guard against it happening.
        if (pid6drive_chip) {

          // The chip classes handle sending data, we just need to set the proper values.

          // Config values.
          pid6drive_chip->config.enable[index_on_chip] = true;
          pid6drive_chip->config.input_index[index_on_chip] = joint.input_index;
          pid6drive_chip->config.output_index[index_on_chip] = joint.output_index;
          pid6drive_chip->config.seeking[index_on_chip] = joint.seeking;
          pid6drive_chip->config.invert[index_on_chip] = joint_inverted_control;

          // Control values.
          pid6drive_chip->targets[index_on_chip] = joint.target;
          pid6drive_chip->drive_power[index_on_chip] = joint.drive_power * (joint.inverted_output ? -1 : +1);
          pid6drive_chip->drive_time[index_on_chip] = joint.drive_time;

          // Position feedback.
          joint.position = pid6drive_chip->positions[index_on_chip];
        }
      }
    }


    // Disable motors that are not assigned.
    for (int i = assigned_on_chip[typed(Chip::ESPMAIN)]; i < available_on_chip[typed(Chip::ESPMAIN)]; i++){
      onboardpid::enable[i] = false;
    }

    // Same for the external pid drivers.
    for (int i = assigned_on_chip[typed(Chip::DRIVE_0)]; i < available_on_chip[typed(Chip::DRIVE_0)]; i++){
      pid6drive_0.config.enable[i] = false;
    }
    for (int i = assigned_on_chip[typed(Chip::DRIVE_1)]; i < available_on_chip[typed(Chip::DRIVE_1)]; i++){
      pid6drive_1.config.enable[i] = false;
    }
    for (int i = assigned_on_chip[typed(Chip::DRIVE_2)]; i < available_on_chip[typed(Chip::DRIVE_2)]; i++){
      pid6drive_2.config.enable[i] = false;
    }

    // Update remaining drive times.
    for (int i = 0; i < NUM_JOINTS; i++){
      auto & drive_time = joints[i].drive_time;
      if (drive_time > elapsed_millis) drive_time -= elapsed_millis;
      else drive_time = 0;
    }
  }
}