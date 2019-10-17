#include <stdio.h>

#include "Esp.h"
#include "Arduino.h"
#include "Wire.h"

#include "esp_attr.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "sdkconfig.h"

#include "quadrature_encoder.hpp"

#include "pid6drive_interface.hpp"
#include "i2c.hpp"
#include "ads1115.hpp"
#include "button.hpp"
#include "lcd.hpp"
#include "pins.hpp"
#include "onboardpid.hpp"


// Time keeping
// ------------

unsigned long last_micros = 0;

// Limit loop frequency to Hz.
#define LOOP_FREQUENCY 200
const int loop_delay_millis = 1000 / LOOP_FREQUENCY;


// Power management
// ----------------

// Store the last time the power button was pressed so we can turn off
// on a long press.
unsigned long power_last_press = 0;
void IRAM_ATTR power_button_interrupt() {
  power_last_press = millis();
}

// TODO: track total power use.


// Sensors
// -------

ADS1115_3In_1Ref ads_0(ADS0_ALERT, ADS0_ADDRESS);
ADS1115_3In_1Ref ads_1(ADS1_ALERT, ADS1_ADDRESS);

int ads_0_reads = 0;
int ads_0_last_reads = 0;
float ads_0_sps = 0.0;

int ads_1_reads = 0;
int ads_1_last_reads = 0;
float ads_1_sps = 0.0;


// Interface
// ---------

Encoder wheel_0 {ENC0A, ENC0B, /* active_state = */ LOW};
Encoder wheel_1 {ENC1A, ENC1B, /* active_state = */ LOW};
Button button_0 {BTN0, /* active_state = */ LOW};
Button button_1 {BTN1, /* active_state = */ LOW};


// LCD
// ---

// Define LCD properties.
#define LCD_COLUMNS 16
#define LCD_ROWS 2
#define LCD_ADDRESS 0x27

LCD<LCD_COLUMNS, LCD_ROWS> lcd(LCD_ADDRESS);


// External PIDs
// -------------

const int pid_driver_0 = PID6DRIVE_ADDRESS + 0b00;
const int pid_driver_1 = PID6DRIVE_ADDRESS + 0b01;
const int pid_driver_2 = PID6DRIVE_ADDRESS + 0b10;


// Global joint controls
// ---------------------


// PID parameters
// TODO: set the arduino slave parameters.
int p = 2;
int i_time = 2000; // millis
// Capacitor adds 10ms momentum, and exp avg adds ~20ms lag.
int d_time = 30; // millis
int threshold = 4;
int overshoot = 8;


enum struct Chip : int {
  NONE,
  ESPMAIN,
  DRIVE_0,
  DRIVE_1,
  DRIVE_2,
  _MAXVALUE
};

const int joints_in_chip[] = {0, 2, 6, 6, 6};

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

struct Joint {
  // Note that inputs and targets are 10bit values.

  // Chip this joint is on, and on that chip which input and which output index.
  Chip chip = Chip::NONE;
  int input_index = -1;
  int output_index = -1;

  // Current position, power, and target seeking.
  int position = 512;
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

const Joint default_joint = {};

#define NUM_JOINTS 20

Joint joints[NUM_JOINTS];

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

// TODO: translate joint stuff above to actual hardware movements.

// TODO: add class to pid drive interface that can store the setting and then send them at once.

// User interface
// --------------

// Helper to use the int type of enums.
template <typename E>
constexpr typename std::underlying_type<E>::type typed(E e) noexcept {
    return static_cast<typename std::underlying_type<E>::type>(e);
}

// The % operator in C is actually the "remainder". To cycle through
// enum values we need the actual mod operator.
inline int mod(int a, int b)
{
    int r = a % b;
    return r < 0 ? r + b : r;
}

namespace ui {

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

  void update(){

    // Don't update faster than the minimum.
    if (millis() - last_update_millis < 100) return;


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
          break;
        }
        // Cycle through outpot indexes.
        case JointView::SELECT_OUT_IDX: {
          // -1 is a valid index, but we should only work with positive values. So add and subtract 1, but
          // also make sure to count 1 extra value option in addition to the joints on chip.
          joint.output_index = mod(joint.output_index + 1 + increment, joints_in_chip[typed(joint.chip)] + 1) - 1;
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
          joint.input_index = mod(joint.input_index + 1 + increment, joints_in_chip[typed(joint.chip)] + 1) - 1;
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
      joints[selected_joint].drive_time = 50; // milliseconds
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
        snprintf(lcd.text[1], LCD_COLUMNS+1, "Pos idx: %1d", joint.input_index);
        break;
      }

      case JointView::SELECT_IN_DIR: {
        snprintf(lcd.text[1], LCD_COLUMNS+1, "Pos dir: %c", joint.inverted_position ? '-' : '+');
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



// Setup
// -----

void setup_piddrive(const byte address) {
  write_int16_to(address, PID6Drive::SET_PID_P_1, 1);
  write_to(address, PID6Drive::OUTPUT_IDX_1, 0);
  write_to(address, PID6Drive::ENABLE_1, true);
  write_to(address, PID6Drive::SET_CONFIGURED, true);
}

void setup(){
  // Power
  // -----

  // Initialize the power pins.
  pinMode(POWER_BTN, INPUT_PULLDOWN);
  pinMode(POWER_CTRL, OUTPUT);
  pinMode(VOLTAGE_IN, INPUT);
  pinMode(CURRENT_IN, INPUT);

  // Turn the power supply mosfet on.
  digitalWrite(POWER_CTRL, HIGH);

  // Use the power button to shut off if held down.
  attachInterrupt(POWER_BTN, power_button_interrupt, RISING);


  // Inputs and on-board PID
  // -----------------------

  // We need 10bit resolution for the on board PID controller, but this
  // should also be enough for voltage and current inputs.
  analogReadResolution(10);
  analogSetWidth(10);

  // Setup the on-board PID driver.
  onboardpid::setup();


  // Comms
  // -----

  Wire.begin();
  Wire.setClock(400000);
  Serial.begin(115200);

  // Other
  // -----

  // Initialize LCD.
  lcd.begin();


  // Setup inputs.
  wheel_0.begin();
  wheel_1.begin();
  button_0.begin();
  button_1.begin();

  // Setup fingertip pressure sensors.
  ads_0.begin();
  ads_1.begin();


  // Initialize time keeping.
  last_micros = micros();


  // External PIDs

  // Wait for the arduino to start and initialize.
  delay(500);

  setup_piddrive(pid_driver_0);
  setup_piddrive(pid_driver_1);
  setup_piddrive(pid_driver_2);
}




// Main loop
// ---------

void loop(){
  // Time keeping.
  unsigned long loop_start_micros = micros();
  const int elapsed_micros = loop_start_micros - last_micros;
  last_micros = loop_start_micros;
  const int elapsed_millis = (elapsed_micros + 500) / 1000;

  // Power management; if power button has been held for 1 second, turn off.
  if (digitalRead(POWER_BTN) and millis() - power_last_press > 1000) {
    digitalWrite(POWER_CTRL, LOW);
  }

  // Restart serial communication in case something was faulty.
  Wire.begin();


  // On-board PID
  // ------------

  // TODO: set the on-board PID target from the gloabl targets.

  // Update the inputs and outputs of the on-board PID.
  onboardpid::loop_tick(elapsed_millis);

  // TODO: set the global inputs from the on-board PID inputs.


  // Experiments
  // -----------

  // byte configured_0 = 0xFF;
  // read_from(pid_driver_0, PID6Drive::GET_CONFIGURED, configured_0);

  // if (configured_0 != 1) setup_piddrive(pid_driver_0);

  // read_int16_from(pid_driver_0, PID6Drive::GET_INPUT_0, inputs[0]);
  // read_int16_from(pid_driver_0, PID6Drive::GET_INPUT_1, inputs[1]);
  // read_int16_from(pid_driver_0, PID6Drive::GET_INPUT_2, inputs[2]);

  // write_int16_to(pid_driver_0, PID6Drive::SET_TARGET_1, 512);

  // write_to(pid_driver_0, PID6Drive::INVERT_1, false);

  // int pid6drive_loop_time = -1;
  // read_int16_from(pid_driver_0, PID6Drive::GET_LOOP_INTERVAL, pid6drive_loop_time);




  // Power management
  // ----------------

  int voltage_in = analogRead(VOLTAGE_IN);
  int current_in = analogRead(CURRENT_IN);
  // TODO: calibrate and scale voltage and current.
  // TODO: use these to compute power
  (void)voltage_in;
  (void)current_in;


  // Sensors
  // -------

  // Exponentially average the sampling rate for the ADS fingertip sensors.
  ads_0_sps = 0.9 * ads_0_sps + 0.1 * ((ads_0_reads - ads_0_last_reads) * 1000 / elapsed_millis);
  ads_0_last_reads = ads_0_reads;

  ads_1_sps = 0.9 * ads_1_sps + 0.1 * ((ads_1_reads - ads_1_last_reads) * 1000 / elapsed_millis);
  ads_1_last_reads = ads_1_reads;

  // Display and UI
  // --------------

  // Handle inputs and write the lcd text.
  ui::update();

  // snprintf(lcd_text[0], LCD_COLUMNS+1, "V%4d I%4d E%3d", voltage_in, current_in, nr_wire_errors);
  // snprintf(lcd_text[0], LCD_COLUMNS+1, "%4d %4d %4d %1d", inputs[0], inputs[1], inputs[2], reverse_direction);

  // snprintf(lcd.text[0], LCD_COLUMNS+1, "%5.2f % 7.4f", ads_0_sps, ads_0.in0);

  // snprintf(lcd_text[1], LCD_COLUMNS+1, "T%4d X%4d L%2d", target, inputs[1], pid6drive_loop_time);
  // snprintf(lcd.text[1], LCD_COLUMNS+1, "% 7.4f % 7.4f", ads_0.in1, ads_0.in2);

  // snprintf(lcd.text[0], LCD_COLUMNS+1, "%5d %1d", wheel_0.position, wheel_0.a_value);

  lcd.update();

  // Fast loop
  // ---------

  // We want the main loop at loop frequency, but we need a faster loop
  // to handle data communication with the pressure sensors.
  while(((micros()-loop_start_micros) / 1000) < loop_delay_millis) {
    ads_0_reads += ads_0.update();
    ads_1_reads += ads_1.update();
  }
}
