#include <stdio.h>

#include "Esp.h"
#include "Arduino.h"
#include "LiquidCrystal_I2C.h"
#include "Wire.h"

#include "esp_attr.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "sdkconfig.h"

#include "quadrature_encoder.hpp"

#include "pid.hpp"
#include "pid6drive_interface.hpp"
#include "i2c.hpp"
#include "ads1115.hpp"
#include "button.hpp"


// Time keeping
// ------------

unsigned long last_micros = 0;

// Limit loop frequency to Hz.
#define LOOP_FREQUENCY 200
const int loop_delay_millis = 1000 / LOOP_FREQUENCY;


// Power management
// ----------------

#define POWER_CTRL GPIO_NUM_2
#define POWER_BTN GPIO_NUM_4
#define VOLTAGE_IN GPIO_NUM_36
#define CURRENT_IN GPIO_NUM_39

// Store the last time the power button was pressed so we can turn off
// on a long press.
unsigned long power_last_press = 0;
void IRAM_ATTR power_button_interrupt() {
  power_last_press = millis();
}


// Sensors
// -------

// Finger-tip pressure sensors pins.
#define ADS0_ALERT GPIO_NUM_19
#define ADS0_ADDRESS (ADS_ADDRESS + 0b00)

#define ADS1_ALERT GPIO_NUM_18
#define ADS1_ADDRESS (ADS_ADDRESS + 0b01)

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

#define BTN0 GPIO_NUM_0 // Note this is also the bootloader button.
#define BTN1 GPIO_NUM_15

#define ENC0A GPIO_NUM_25
#define ENC0B GPIO_NUM_26
#define ENC1A GPIO_NUM_26
#define ENC1B GPIO_NUM_5

Encoder wheel_0 {ENC0A, ENC0B, /* active_state = */ LOW};
Encoder wheel_1 {ENC1A, ENC1B, /* active_state = */ LOW};
Button button_0 {BTN0, /* active_state = */ LOW};
Button button_1 {BTN1, /* active_state = */ LOW};


// LCD
// ---

// Set the LCD number of columns and rows
#define LCD_COLUMNS 16
#define LCD_ROWS 2

// Define the text buffer for the lcd, include terminating null byte.
char lcd_text[LCD_ROWS][LCD_COLUMNS+1] = {};
// Only update lcd if it needs to.
bool lcd_dirty = true;
unsigned long lcd_last_millis = 0;

// Set LCD address, number of columns and rows.
LiquidCrystal_I2C lcd(0x27, LCD_COLUMNS, LCD_ROWS);


// LEDs
// ----

// LED error for the motor inputs.
#define IN_ERROR GPIO_NUM_23

// LED error for the ADS fingertip inputs.
#define ADC_ERROR GPIO_NUM_27

// On-board PID
// ------------

#define IN0 GPIO_NUM_34
#define IN1 GPIO_NUM_35
#define DIR0 GPIO_NUM_12
#define DIR1 GPIO_NUM_13
#define DIR2 GPIO_NUM_32
#define DIR3 GPIO_NUM_33
#define PWM0 GPIO_NUM_17
#define PWM1 GPIO_NUM_16

// ADC value where it's likely a short (at 10 bit resolution).
#define ADC_LOW 8 // ~0.06V
#define ADC_HIGH 856 // ~3.23V Note that 3.3V max input corresponds to 866.
// TODO: ADC is connected to +5V

// Input positions, 10bits. Note that the ESP32 max input voltage value is 866.
int inputs[2] = {0, 0};

// Target positions, 10bits.
int targets[2] = {0, 0};

// Enable input/output combination.
bool enable[2] = {false, false};

// Invert outputs so that positive error leads to negative control.
bool invert[2] = {false, false};

// Output indexes of input pin, -1 for unset.
int8_t output_idx[2] = {-1, -1};

// PID controller for outputs. Indexed by input pins.
HysterisisPID8bit pids[2];

// Error flag.
bool error_state = false;
bool error_pin[2] = {false, false};



// Exponentially average analog reads so we don't get such large fluctuations.
inline void exp_avg_analog_read(const uint8_t pin, const uint8_t i) {
  int value = analogRead(pin);
  if (ADC_LOW < value and value < ADC_HIGH) {
    error_pin[i] = false;
    inputs[i] = (inputs[i] * 6 + value * 4 + 5) / 10; // + 5 / 10 to round the value.
  } else {
    error_pin[i] = true;
  }
}

/* Setup the on-board PID drives. */
void onboard_pid_setup(){
  // Direction pins.
  pinMode(DIR0, OUTPUT);
  digitalWrite(DIR0, LOW);
  pinMode(DIR1, OUTPUT);
  digitalWrite(DIR1, LOW);
  pinMode(DIR2, OUTPUT);
  digitalWrite(DIR2, LOW);
  pinMode(DIR3, OUTPUT);
  digitalWrite(DIR3, LOW);

  // Input pins.
  pinMode(IN0, ANALOG);
  pinMode(IN1, ANALOG);

  // Read once to initialize input values.
  inputs[0] = analogRead(IN0);
  inputs[1] = analogRead(IN1);

  // Power control pins.
  pinMode(PWM0, OUTPUT);
  digitalWrite(PWM0, 0);
  pinMode(PWM1, OUTPUT);
  digitalWrite(PWM1, 0);
  // TODO: analogWrite doesn't seem to be available, probably have to
  // use the ledc.h interface of the ESP32.

  // Error led pin.
  pinMode(IN_ERROR, OUTPUT);
  digitalWrite(IN_ERROR, LOW);
}

/* Read inputs and output controls for the on-board PID drives. */
void onboard_pid_loop_tick(const int elapsed_millis) {

  // Inputs and control
  // ------------------

  // Read inputs.
  exp_avg_analog_read(IN0, 0);
  exp_avg_analog_read(IN1, 1);


  // Update pid controllers.
  for (int i = 0; i < 2; i++) {
    // Skip errored pins.
    if(error_pin[i]) continue;

    pids[i].update(inputs[i], targets[i], elapsed_millis);
  }

  // Compute required values for the direction pins.
  bool direction[4] = {};
  int power[2] = {};

  for (int i = 0; i < 2; i++) {
    const int idx = output_idx[i];
    if (idx == -1) continue;

    const int control = pids[i].control;

    if (control == 0 or (not enable[i]) or error_pin[i]) {
      // Free run till stop.
      power[idx] = 0;
      direction[idx*2 + 0] = false;
      direction[idx*2 + 1] = false;
    } else {
      // Run in control direction at control strength.
      power[idx] = abs(control);
      // Run in inverted direction by xor (!=) with the invert flag.
      direction[idx*2 + 0] = (control > 0) != invert[i];
      direction[idx*2 + 1] = (control < 0) != invert[i];
    }
  }

  // Set pin outputs to computed values.
  digitalWrite(DIR0, direction[0]);
  digitalWrite(DIR1, direction[1]);
  digitalWrite(DIR2, direction[2]);
  digitalWrite(DIR3, direction[3]);

  // Write pulse width modulation levels.
  // TODO: use the ledc.h interface.
  // analogWrite(PWM0, power[0]);
  // analogWrite(PWM1, power[1]);


  // Error handling
  // --------------

  // Use a temporary flag so we update the global state in a single clock step.
  bool new_error_state = false;
  for (int i = 0; i < 2; i++) new_error_state |= error_pin[i];
  error_state = new_error_state;

  digitalWrite(IN_ERROR, error_state);
}


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
  ONBOARD = 1,
  DRIVE0 = 2,
  DRIVE1 = 3,
  DRIVE2 = 4
};

struct Joint {
  // Note that inputs and targets are 10bit values.

  // Chip this joint is on, and on that chip which input and which output index.
  Chip chip = Chip::NONE;
  int input_index = -1;
  int output_index = -1;

  // Current position and target.
  int position = 512;
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

char * joint_name(size_t index){
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
  onboard_pid_setup();


  // Comms
  // -----

  Wire.begin();
  Wire.setClock(400000);
  Serial.begin(115200);

  // Other
  // -----

  // Initialize LCD and backlight.
  lcd.begin();
  lcd.backlight();
  // Store last write to the LCD so we don't write so often.
  lcd_last_millis = millis();


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
  onboard_pid_loop_tick(elapsed_millis);

  // TODO: set the global inputs from the on-board PID inputs.


  // Experiments
  // -----------

  byte configured_0 = 0xFF;
  read_from(pid_driver_0, PID6Drive::GET_CONFIGURED, configured_0);

  if (configured_0 != 1) setup_piddrive(pid_driver_0);

  read_int16_from(pid_driver_0, PID6Drive::GET_INPUT_0, inputs[0]);
  read_int16_from(pid_driver_0, PID6Drive::GET_INPUT_1, inputs[1]);
  read_int16_from(pid_driver_0, PID6Drive::GET_INPUT_2, inputs[2]);

  write_int16_to(pid_driver_0, PID6Drive::SET_TARGET_1, 512);

  write_to(pid_driver_0, PID6Drive::INVERT_1, false);

  int pid6drive_loop_time = -1;
  read_int16_from(pid_driver_0, PID6Drive::GET_LOOP_INTERVAL, pid6drive_loop_time);



  // Power experiments.
  int voltage_in = analogRead(VOLTAGE_IN);
  int current_in = analogRead(CURRENT_IN);



  // Sensors
  // -------

  // Exponentially average the sampling rate for the ADS fingertip sensors.
  ads_0_sps = 0.9 * ads_0_sps + 0.1 * ((ads_0_reads - ads_0_last_reads) * 1000 / elapsed_millis);
  ads_0_last_reads = ads_0_reads;

  ads_1_sps = 0.9 * ads_1_sps + 0.1 * ((ads_1_reads - ads_1_last_reads) * 1000 / elapsed_millis);
  ads_1_last_reads = ads_1_reads;

  // Display
  // -------

  // Text to display.
  lcd_dirty = true;

  // snprintf(lcd_text[0], LCD_COLUMNS+1, "V%4d I%4d E%3d", voltage_in, current_in, nr_wire_errors);
  // snprintf(lcd_text[0], LCD_COLUMNS+1, "%4d %4d %4d %1d", inputs[0], inputs[1], inputs[2], reverse_direction);

  snprintf(lcd_text[0], LCD_COLUMNS+1, "%5.2f % 7.4f", ads_0_sps, ads_0.in0);

  // snprintf(lcd_text[1], LCD_COLUMNS+1, "T%4d X%4d L%2d", target, inputs[1], pid6drive_loop_time);
  snprintf(lcd_text[1], LCD_COLUMNS+1, "% 7.4f % 7.4f", ads_0.in1, ads_0.in2);


  // LCD
  // ---

  if (lcd_dirty and (millis() - lcd_last_millis) > 250) {
    lcd.clear();
    for (uint8_t row = 0; row < LCD_ROWS; row++) {
      lcd.setCursor(0, row);
      lcd.print(lcd_text[row]);
    }
    lcd_dirty = false;
    lcd_last_millis = millis();
  }


  // Fast loop
  // ---------

  // We want the main loop at loop frequency, but we need a faster loop
  // to handle data communication with the pressure sensors.
  while(((micros()-loop_start_micros) / 1000) < loop_delay_millis) {
    ads_0_reads += ads_0.update();
    ads_1_reads += ads_1.update();
  }
}
