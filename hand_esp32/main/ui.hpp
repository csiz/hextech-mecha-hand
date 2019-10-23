#pragma once

#include <stdio.h>

#include "quadrature_encoder.hpp"
#include "button.hpp"
#include "joints.hpp"
#include "utils.hpp"
#include "lcd.hpp"
#include "pins.hpp"


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

  int selected_joint = 0;

  unsigned long last_update_millis = 0;

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

  // TODO: show power use
  // TODO: show loop time
  // TODO: option to save/reset joint config.

  void update(){
    using namespace joints;
    using joints::joints; // remove ambiguity with the namespace.

    // Don't update faster than the minimum.
    if (millis() - last_update_millis < 200) return;


    // Input handling
    // --------------

    int change_0 = wheel_0.collect_change();
    int change_1 = wheel_1.collect_change();
    int presses_0 = button_0.collect_presses();
    int presses_1 = button_1.collect_presses();

    // Left button returns to overview.
    if (presses_0) {
      joint_view = JointView::OVERVIEW;
    }

    // Left wheel cycles joints or values.
    if (change_0) {
      // Currently selected joint; can be changed during overview.
      auto & joint = joints[selected_joint];

      // The mouse wheel increments twice very quickly, just count the direction.
      int increment = change_0 > 0 ? +1 : -1;

      switch(joint_view) {
        // Cycle joints during overview.
        case JointView::OVERVIEW: {
          selected_joint = mod(selected_joint + increment, NUM_JOINTS);
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

    // Right button cycles through views.
    if (presses_1) {
      joint_view = static_cast<JointView>((typed(joint_view) + 1) % typed(JointView::_MAXVALUE));
    }

    // Right wheels drives unit for a short amount of time.
    if (change_1) {
      int direction = change_1 > 0 ? +1 : -1;
      joints[selected_joint].drive_power = 128 * direction; // half-power
      joints[selected_joint].drive_time = 100; // milliseconds
    }


    // Display text
    // ------------

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


    // Store the update time.
    last_update_millis = millis();
  }
}