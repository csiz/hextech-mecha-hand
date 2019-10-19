#include <stdio.h>

#include "Esp.h"
#include "Arduino.h"
#include "Wire.h"

#include "esp_attr.h"
#include "driver/gpio.h"


#include "sdkconfig.h"

#include "quadrature_encoder.hpp"

#include "pid6drive_interface.hpp"
#include "i2c.hpp"
#include "ads1115.hpp"
#include "button.hpp"
#include "lcd.hpp"
#include "pins.hpp"
#include "onboardpid.hpp"

// Utils
// -----

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

PID6Drive pid6drive_0(PID6DRIVE_ADDRESS + 0b00);
PID6Drive pid6drive_1(PID6DRIVE_ADDRESS + 0b01);
PID6Drive pid6drive_2(PID6DRIVE_ADDRESS + 0b02);


// Global joint controls
// ---------------------


// PID parameters
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



void update_joints(const int elapsed_millis){

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


    PID6Drive * pid6drive_chip = nullptr;

    // Invert the control if the output and position are unsynced.
    bool joint_inverted_control = (joint.inverted_output != joint.inverted_position);


    switch(joint.chip) {
      // If on the main chip, then we can immediately update values.
      case Chip::ESPMAIN:
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
        break;


      // The chip classes handle sending data, we just need to set the proper values.
      case Chip::DRIVE_0:
        pid6drive_chip = &pid6drive_0;
        // fallthrough
      case Chip::DRIVE_1:
        pid6drive_chip = &pid6drive_1;
        // fallthrough
      case Chip::DRIVE_2:
        pid6drive_chip = &pid6drive_2;
        // fallthrough

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

        break;

      // Nothing to do for NONE and _MAXVALUE
      default: break;
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


// User interface
// --------------


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



// Setup
// -----

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


  pid6drive_0.config.set_all_pid_params(p, i_time, d_time, threshold, overshoot);
  pid6drive_1.config.set_all_pid_params(p, i_time, d_time, threshold, overshoot);
  pid6drive_2.config.set_all_pid_params(p, i_time, d_time, threshold, overshoot);

  pid6drive_0.configure();
  pid6drive_1.configure();
  pid6drive_2.configure();
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


  // Joint control
  // -------------

  update_joints(elapsed_millis);

  // Motor drivers
  // -------------

  // Update the inputs and outputs of the on-board PID.
  onboardpid::loop_tick(elapsed_millis);
  pid6drive_0.update(elapsed_millis);
  pid6drive_1.update(elapsed_millis);
  pid6drive_2.update(elapsed_millis);

  // Experiments
  // -----------



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

  // TODO: remove
  // snprintf(lcd_text[0], LCD_COLUMNS+1, "V%4d I%4d E%3d", voltage_in, current_in, nr_wire_errors);


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
