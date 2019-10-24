#pragma once

#include <stdio.h>

#include "quadrature_encoder.hpp"
#include "button.hpp"
#include "joints.hpp"
#include "utils.hpp"
#include "lcd.hpp"
#include "pins.hpp"
#include "power.hpp"
#include "memory.hpp"


namespace ui {

  // Hardware inputs
  // ---------------

  Encoder wheel_0 {ENC0A, ENC0B, /* active_state = */ LOW};
  Encoder wheel_1 {ENC1A, ENC1B, /* active_state = */ LOW};
  Button button_0 {BTN0, /* active_state = */ LOW};
  Button button_1 {BTN1, /* active_state = */ LOW};


  // LCD
  // ---

  // Define LCD properties.
  const int LCD_COLUMNS = 16;
  const int LCD_ROWS  = 2;
  const byte LCD_ADDRESS = 0x27;

  LCD<LCD_COLUMNS, LCD_ROWS> lcd(LCD_ADDRESS);


  // User interface
  // --------------

  // Independently keep track of the UI update rate.
  unsigned long last_update_millis = 0;

  // Main views
  enum struct View : int {
    POWER,
    TIMINGS,
    JOINTS,
    SAVE,

    _MAXVALUE
  };

  View view = View::POWER;

  // The joints view cycles through all of them as a top-level view.
  int selected_joint = 0;

  // Change the view or special case to cycle through joints.
  void change_view(int increment) {
    // Cycle through joints if they are in the valid zone.
    if (view == View::JOINTS) {
      int new_selected_joint = selected_joint + increment;
      if (0 <= new_selected_joint and new_selected_joint < NUM_JOINTS) {
        selected_joint = new_selected_joint;
        // Early return after setting proper joint.
        return;
      }
    }

    view = typed_add_mod(view, increment, View::_MAXVALUE);

    // If the new view is joints, we need to set the selected joint properly.
    if (view == View::JOINTS) {
      // If we increased, then we must reach joint 0.
      if (increment > 0) selected_joint = 0;
      // If we decreased, we must've come back to the last joint.
      if (increment < 0) selected_joint = NUM_JOINTS - 1;
    }
  }


  // Power view
  // ----------

  enum struct PowerView : int {
    OVERVIEW,
    EDIT_VOLTAGE,
    EDIT_CURRENT,

    _MAXVALUE
  };

  PowerView power_view = PowerView::OVERVIEW;

  void update_power(int change_0, int change_1, int presses_0, int presses_1){
    // ### Input handling

    // Left button returns to overview.
    if (presses_0) {
      power_view = PowerView::OVERVIEW;
    }

    // Right button cycles through views.
    if (presses_1) {
      power_view = typed_add_mod(power_view, +1, PowerView::_MAXVALUE);
    }

    // Left wheel cycles views or values.
    if (change_0) {
      // The mouse wheel increments twice very quickly, just count the direction.
      int increment = change_0 > 0 ? +1 : -1;

      switch(power_view) {
        // During overview, cycle through views.
        case PowerView::OVERVIEW: {
          change_view(increment);
          break;
        }
        // Increment voltage scale.
        case PowerView::EDIT_VOLTAGE: {
          power::voltage_scale += power::voltage_scale_inc * increment;
          break;
        }
        // Increment current scale.
        case PowerView::EDIT_CURRENT: {
          power::current_scale += power::current_scale_inc * increment;
          break;
        }
        case PowerView::_MAXVALUE: break;
      }
    }

    // Do nothing for right wheel.
    if (change_1) {}


    // ### Display text

    snprintf(lcd.text[0], LCD_COLUMNS+1, "Energy: %7.1fJ", power::energy);
    switch(power_view) {
      case PowerView::OVERVIEW: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "%3.1fW %3.1fV %3.2fA", power::power, power::voltage, power::current);
        break;
      }
      case PowerView::EDIT_VOLTAGE: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "%3.1fV S: %7.2g", power::voltage, 1e-6 * power::voltage_scale);
        break;
      }
      case PowerView::EDIT_CURRENT: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "%3.2fA S: %7.2g", power::current, 1e-6 * power::current_scale);
        break;
      }
      // Ignore _MAXVALUE option.
      case PowerView::_MAXVALUE: break;
    }
  }

  // Timings view
  // ------------

  int esp_interval_millis = -1;

  void update_timings(int change_0, int change_1, int presses_0, int presses_1){
    // ### Input handling

    // Left button does nothing.
    if (presses_0) {}

    // Right button does nothing.
    if (presses_1) {}

    // Left wheel cycles views.
    if (change_0) change_view(change_0 > 0 ? +1 : -1);

    // Right wheel does nothing.
    if (change_1) {}

    // ### Display text

    snprintf(lcd.text[0], LCD_COLUMNS+1, "Loop ms: E %2d", esp_interval_millis);
    snprintf(lcd.text[1], LCD_COLUMNS+1, "D0:%2d 1:%2d 2:%2d",
      joints::pid6drive_0.loop_interval,
      joints::pid6drive_1.loop_interval,
      joints::pid6drive_2.loop_interval);
  }


  // Joints view
  // -----------

  enum struct JointView : int {
    OVERVIEW,
    SELECT_CHIP,
    SELECT_OUT_IDX,
    SELECT_OUT_DIR,
    SELECT_IN_IDX,
    SELECT_IN_DIR,
    SELECT_MIN,
    SELECT_MAX,

    _MAXVALUE
  };

  JointView joint_view = JointView::OVERVIEW;


  void update_joints(int change_0, int change_1, int presses_0, int presses_1){
    using namespace joints;
    using joints::joints; // remove ambiguity with the namespace.

    // ### Input handling

    // Left button returns to overview.
    if (presses_0) {
      joint_view = JointView::OVERVIEW;
    }

    // Right button cycles through views.
    if (presses_1) {
      joint_view = typed_add_mod(joint_view, +1, JointView::_MAXVALUE);
    }


    // Left wheel cycles joints or values.
    if (change_0) {
      // Currently selected joint; can be changed during overview.
      auto & joint = joints[selected_joint];

      // The mouse wheel increments twice very quickly, just count the direction.
      int increment = change_0 > 0 ? +1 : -1;

      switch(joint_view) {
        // During overview, cycle through joints or views.
        case JointView::OVERVIEW: {
          change_view(increment);
          break;
        }
        // Cycle through chips.
        case JointView::SELECT_CHIP: {
          joint.chip = static_cast<Chip>(mod(typed(joint.chip) + increment, typed(Chip::_MAXVALUE)));
          joint.output_index = -1;
          joint.input_index = -1;
          break;
        }
        // Cycle through outpot indexes.
        case JointView::SELECT_OUT_IDX: {
          // -1 is a valid index, but we should only work with positive values. So add and subtract 1, but
          // also make sure to count 1 extra value option in addition to the joints on chip.
          joint.output_index = mod(joint.output_index + 1 + increment, available_on_chip[typed(joint.chip)] + 1) - 1;
          break;
        }
        // Cycle through output directions (motor should move up when rotating wheel up).
        case JointView::SELECT_OUT_DIR: {
          if (increment % 2) joint.inverted_output = not joint.inverted_output;
          break;
        }
        // Cycle through input indexes.
        case JointView::SELECT_IN_IDX: {
          // See logic for output index.
          joint.input_index = mod(joint.input_index + 1 + increment, available_on_chip[typed(joint.chip)] + 1) - 1;
          break;
        }
        // Cycle through input direction (position should increase when moving motor up).
        case JointView::SELECT_IN_DIR: {
          if (increment % 2) joint.inverted_position = not joint.inverted_position;
          break;
        }
        // Set minimum position reachable by the motor.
        case JointView::SELECT_MIN: {
          joint.min_pos = clamp(joint.min_pos + increment * 5, 5, 1020);
          break;
        }
        // Set maximum position reachable by the motor.
        case JointView::SELECT_MAX: {
          joint.max_pos = clamp(joint.max_pos + increment * 5, 5, 1020);
          break;
        }
        // Ignore _MAXVALUE option.
        case JointView::_MAXVALUE: break;
      }
    }

    // Right wheels drives unit for a short amount of time.
    if (change_1) {
      int direction = change_1 > 0 ? +1 : -1;
      joints[selected_joint].drive_power = 128 * direction; // half-power
      joints[selected_joint].drive_time = 100; // milliseconds
    }


    // ### Display text

    snprintf(lcd.text[0], LCD_COLUMNS+1, "#%2d %s", selected_joint, joint_name(selected_joint));

    auto const& joint = joints[selected_joint];

    switch(joint_view) {
      case JointView::OVERVIEW: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "P: %4d T: %4d", joint.position, joint.target);
        break;
      }

      case JointView::SELECT_CHIP: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "Chip: %s", chip_name(joint.chip));
        break;
      }

      case JointView::SELECT_OUT_IDX: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "Out idx: %1d", joint.output_index);
        break;
      }

      case JointView::SELECT_OUT_DIR: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "Out dir: %c", joint.inverted_output ? '-' : '+');
        break;
      }

      case JointView::SELECT_IN_IDX: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "P: %4d idx: %1d", joint.position, joint.input_index);
        break;
      }

      case JointView::SELECT_IN_DIR: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "P: %4d dir: %c", joint.position, joint.inverted_position ? '-' : '+');
        break;
      }

      case JointView::SELECT_MIN: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "P: %4d > %4d", joint.position, joint.min_pos);
        break;
      }

      case JointView::SELECT_MAX: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "P: %4d < %4d", joint.position, joint.max_pos);
        break;
      }
      // Ignore _MAXVALUE option.
      case JointView::_MAXVALUE: break;
    }
  }


  // Save view
  // ---------

  enum struct SaveState {
    PROMPT,
    CONFIRM,
    OUT_OF_MEMORY,
    OTHER_ERROR
  };

  SaveState save_state = SaveState::PROMPT;

  void update_save(int change_0, int change_1, int presses_0, int presses_1){
    // ### Input handling

    // Left button does nothing.
    if (presses_0) {}

    // Right button saves.
    if (presses_1) {
      if (save_state != SaveState::CONFIRM) {
        memory::save();
        if (memory::err == ESP_ERR_NVS_NOT_ENOUGH_SPACE) save_state = SaveState::OUT_OF_MEMORY;
        else if (memory::err != ESP_OK) save_state = SaveState::OTHER_ERROR;
        else save_state = SaveState::CONFIRM;
      }
    }

    // Left wheel cycles views; reset save state to prompt.
    if (change_0) {
      save_state = SaveState::PROMPT;
      change_view(change_0 > 0 ? +1 : -1);
    }

    // Right wheel does nothing.
    if (change_1) {}

    // ### Display text

    snprintf(lcd.text[0], LCD_COLUMNS+1, "Save config.");
    switch(save_state){
      case SaveState::PROMPT: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "Press to save...");
        break;
      }
      case SaveState::CONFIRM: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "Saved!");
        break;
      }
      case SaveState::OUT_OF_MEMORY: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "No memory!");
        break;
      }
      case SaveState::OTHER_ERROR: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "Error: %4d", static_cast<int>(memory::err));
        break;
      }
    }
  }


  // UI update
  // ---------

  void update(){

    // Don't update faster than the minimum.
    if (millis() - last_update_millis < 200) return;

    // Store the update time.
    last_update_millis = millis();


    // Read inputs.
    int change_0 = wheel_0.collect_change();
    int change_1 = wheel_1.collect_change();
    int presses_0 = button_0.collect_presses();
    int presses_1 = button_1.collect_presses();


    switch(view) {
      case View::POWER: return update_power(change_0, change_1, presses_0, presses_1);
      case View::TIMINGS: return update_timings(change_0, change_1, presses_0, presses_1);
      case View::JOINTS: return update_joints(change_0, change_1, presses_0, presses_1);
      case View::SAVE: return update_save(change_0, change_1, presses_0, presses_1);
      case View::_MAXVALUE: return;
    }

    // No more code, due to return from switch.
  }
}