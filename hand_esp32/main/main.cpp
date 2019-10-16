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


enum struct Chip {
  NONE,
  ESPMAIN = 1,
  DRIVE_0 = 2,
  DRIVE_1 = 3,
  DRIVE_2 = 4
};

const char * chip_name(Chip chip) {
  switch(chip){
    case Chip::NONE: return "none";
    case Chip::ESPMAIN: return "espmain";
    case Chip::DRIVE_0: return "drive-0";
    case Chip::DRIVE_1: return "drive-1";
    case Chip::DRIVE_2: return "drive-2";
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
  int min_pos = 0;
  int max_pos = 1023;

  // If the position are inverted; ie. finger is curled when at min position instead of max.
  int inverted_position = false;

  // Whether output direction is inverted; ie. the PID controllers drive the motor away from the target.
  int inverted_output = false;
};

const Joint default_joint = {};

Joint joints[20];

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

  // If still pressed we can turn on the board. Otherwise, maybe we woke from a reset,
  // then turn off and wait for another power button press.
  if (digitalRead(POWER_BTN)) {
    // Turn the power supply mosfet on.
    digitalWrite(POWER_CTRL, HIGH);
  } else {
    ESP.restart();
  }

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

  // Display
  // -------

  // snprintf(lcd_text[0], LCD_COLUMNS+1, "V%4d I%4d E%3d", voltage_in, current_in, nr_wire_errors);
  // snprintf(lcd_text[0], LCD_COLUMNS+1, "%4d %4d %4d %1d", inputs[0], inputs[1], inputs[2], reverse_direction);

  snprintf(lcd.text[0], LCD_COLUMNS+1, "%5.2f % 7.4f", ads_0_sps, ads_0.in0);

  // snprintf(lcd_text[1], LCD_COLUMNS+1, "T%4d X%4d L%2d", target, inputs[1], pid6drive_loop_time);
  snprintf(lcd.text[1], LCD_COLUMNS+1, "% 7.4f % 7.4f", ads_0.in1, ads_0.in2);


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
