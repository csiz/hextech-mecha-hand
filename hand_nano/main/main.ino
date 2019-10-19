#include <stdint.h>
#include <stdio.h>

#include <Wire.h>
#include <utility/twi.h>

#include "pid.hpp"
#include "pid6drive_registers.hpp"


// Analog inputs.
#define IN0 A0
#define IN1 A1
#define IN2 A2
#define IN3 A3
#define IN4 A6
#define IN5 A7

// Analog thresholds where we assume malfunction.
#define IN_LOW_THRESHOLD 8
#define IN_HI_THRESHOLD 1015 // 1023-8

// I2C lines.
#define SCL A5
#define SDA A4
// I2C address selection pins.

// Address pins are dual use. On setup they are set to input pullup (37.5KΩ) and
// the adress bit can be set to 1 by connecting to ground through a pull down
// resistor (10KΩ). This is enough to pull the input to 1V < 2.4V threshold
// which sets the pin low. After address selection the pins are set to output
// and control the regiser and led.
#define ADDRESS0 7
#define ADDRESS1 8

// Power control of outputs.
#define PWM0 3
#define PWM1 5
#define PWM2 6
#define PWM3 9
#define PWM4 10
#define PWM5 11

// Direction control of outputs; DIR0-7 are attached to the shift register.
#define DIR8 1
#define DIR9 0
#define DIR10 2
#define DIR11 4

// Shift register pins.
#define SRCLK 13
#define SER 12
#define RCLK 8 // dual use with address 1
// We want to bit bang these so they're a bit faster; PORTB controls IO pins 8-13.
#define SRCLK_ON PORTB |= 0b00100000
#define SRCLK_OFF PORTB &= ~0b00100000
#define RCLK_ON PORTB |= 0b00000001
#define RCLK_OFF PORTB &= ~0b00000001
#define SER_SET(state) PORTB = state ? (PORTB | 0b00010000) : (PORTB & ~0b00010000)



// Error LED (in case a short it detected).
#define LED_ERROR 7 // dual use with address 0.

// Timing
#define LOOP_FREQUENCY 200
const int loop_delay_micros = 1000000 / LOOP_FREQUENCY;
unsigned long last_micros;
unsigned long loop_interval_micros = 0;

// I2C address.
int i2c_address = PID6DriveRegister_ADDRESS;
byte current_register = 0xFF;

// Input positions, 10bits.
int inputs[6] = {0, 0, 0, 0, 0, 0};

// Target positions, 10bits.
int targets[6] = {0, 0, 0, 0, 0, 0};

// Enable input/output combination.
bool enable[6] = {false, false, false, false, false, false};

// Whether the controller seeks position, or only applies the timed drive.
bool seeking[6] = {false, false, false, false, false, false};

// Invert seeking outputs so that positive error leads to negative control.
bool invert[6] = {false, false, false, false, false, false};

// Amount of power to apply to each output (doesn't respects invert direction).
int drive_power[6] = {0, 0, 0, 0, 0, 0};

// Amount of time (in millis) to apply the driving power for.
int drive_time[6] = {0, 0, 0, 0, 0, 0};

// Output indexes of driver units, -1 for unset.
int8_t output_idx[6] = {0, 1, 2, 3, 4, 5};

// Input indexes  of driver units -1 for unset.
int8_t input_idx[6] = {0, 1, 2, 3, 4, 5};

// PID controller for outputs. Indexed by driver units.
HysterisisPID8bit pids[6];

// Error flag; indexed as inputs.
bool error_state = false;
bool error_pin[6] = {false, false, false, false, false, false};

// Non-error LED state; blink for 1 second on start-up.
int blink_remaining_millis = 1000;
int blink_halfperiod_millis = 100;

// Expose this flag so the master device can detect resets.
bool configured = false;

// Count any i2c errors.
uint8_t i2c_errors = 0;

// Reset the i2c bus if there's no connection for some time.
int i2c_no_comms_reset_millis = 500;
// Allow 5 seconds connection time after restart.
int i2c_time_left_to_reset_millis = 5000;


// Get input position of drive unit.
inline int get_input(int8_t drive_idx){
  if (drive_idx < 0 or 6 <= drive_idx) return -1;
  if (input_idx[drive_idx] == -1) return -1;
  return inputs[input_idx[drive_idx]];
}

// Get error state of drive unit.
inline int get_error(int8_t drive_idx){
  if (drive_idx < 0 or 6 <= drive_idx) return false;
  // Skip if not being driven by the input.
  if (not enable[drive_idx] or not seeking[drive_idx]) return false;
  if (input_idx[drive_idx] == -1) return false;
  return error_pin[input_idx[drive_idx]];
}


// Exponentially average analog reads so we don't get such large fluctuations.
inline void exp_avg_analog_read(const uint8_t pin, const uint8_t i) {
  int value = analogRead(pin);
  if (IN_LOW_THRESHOLD < value and value < IN_HI_THRESHOLD) {
    error_pin[i] = false;
    inputs[i] = (inputs[i] * 6 + value * 4 + 5) / 10; // + 5 / 10 to round the value.
  } else {
    error_pin[i] = true;
  }
}


// Send bits to the shift register, QA bit last. Start the send with zeroes to clear the register.
template<size_t N>
void send_sr_bits(size_t zeroes, bool bits[N]) {
  // We don't have enough pins to clear the register, so clear it with zeroes.
  SER_SET(false);
  _NOP(); // wait 62.5ns; 30ns minimum
  for (size_t i = 0; i < zeroes; i++) {
    SRCLK_ON;
    _NOP(); // wait 62.5ns; 20ns minimum
    SRCLK_OFF;
  }
  // Now send data.
  for (size_t i = 0; i < N; i++) {
    // Use no-ops for timing requirements. One _NOP at 16MHz frequency is 62.5ns.
    SER_SET(bits[i]);
    _NOP(); // wait 62.5ns; 30ns minimum
    SRCLK_ON;
    _NOP(); // wait 62.5ns; 20ns minimum
    SRCLK_OFF;
  }
  RCLK_ON;
  _NOP(); // wait 62.5ns; 20ns minimum
  RCLK_OFF;
}


// I2C Comms
// ---------

// We need a custom function to reset the i2c line without pulling it low unnecesarily.

// Re-declare the `twi_state` variable from `utility/twi.c`.
static volatile uint8_t twi_state;

inline void i2c_reset() {
  // We can't use Wire.end function because it sets the SDA/SCL pins to 0.

  // Atmega328P datasheet, page 199:
  // • Bit 2 – TWEN: TWI Enable Bit
  // The TWEN bit enables TWI operation and activates the TWI interface. When TWEN is written to one, the TWI takes control
  // over the I/O pins connected to the SCL and SDA pins, enabling the slew-rate limiters and spike filters. If this bit is written to
  // zero, the TWI is switched off and all TWI transmissions are terminated, regardless of any ongoing operation.

  // Disable twi module, acks, and twi interrupt.
  TWCR &= ~(_BV(TWEN) | _BV(TWIE) | _BV(TWEA));


  // Release bus, and re-enable it.
  TWCR = _BV(TWEN) | _BV(TWIE) | _BV(TWEA) | _BV(TWINT);

  // Ipdate twi state.
  twi_state = TWI_READY;
}

inline void i2c_error() {
  blink_remaining_millis = 200;
  blink_halfperiod_millis = 50;
  i2c_errors += 1;
  i2c_reset();
}

inline int read_int16() {
  return static_cast<int>(Wire.read()) << 8 | Wire.read();
}

inline void write_int16(const int value){
  Wire.write(value >> 8);
  Wire.write(value & 0xFF);
}

// Repeat the macro for all 6 inputs/controls.
#define EACH(macro) macro(0) macro(1) macro(2) macro(3) macro(4) macro(5)

/* Request function gets called when master expects a response. */
void i2c_request(){
  // Restart no-comms count down.
  i2c_time_left_to_reset_millis = i2c_no_comms_reset_millis;

  // Grab the register to handle and reset the current one. We do
  // this so we can simply return from the switch statement, instead
  // of having extra code at the end.
  const byte handling_register = current_register;
  current_register = 0xFF;

  switch(static_cast<PID6DriveRegister>(handling_register)) {
    case PID6DriveRegister::_UNUSED:
      i2c_error();
      return;

#define GET_INPUT_MACRO(i) \
    case PID6DriveRegister::GET_INPUT_##i: { \
      write_int16(get_input(i)); \
      return; \
    }

    EACH(GET_INPUT_MACRO)

#define GET_TARGET_MACRO(i) \
    case PID6DriveRegister::GET_TARGET_##i: { \
      write_int16(targets[i]); \
      return; \
    }

    EACH(GET_TARGET_MACRO)

#define GET_ERROR_MACRO(i) \
    case PID6DriveRegister::GET_ERROR_##i: { \
      Wire.write(get_error(i)); \
      return; \
    }

    EACH(GET_ERROR_MACRO)


    case PID6DriveRegister::GET_ERROR_STATE:
      Wire.write(error_state);
      return;

    case PID6DriveRegister::GET_ALL_INPUTS:
      for (int i = 0; i < 6; i++) write_int16(get_input(i));
      return;

    case PID6DriveRegister::GET_LOOP_INTERVAL:
      write_int16((loop_interval_micros + 500) / 1000);
      return;

    case PID6DriveRegister::GET_CONFIGURED:
      Wire.write(configured);
      return;

    case PID6DriveRegister::GET_RESET_I2C_ERRORS:
      Wire.write(i2c_errors);
      i2c_errors = 0;
      return;

    case PID6DriveRegister::GET_ALL_ERRORS:
      for (int i = 0; i < 6; i++) Wire.write(get_error(i));
      return;

    default:
      i2c_error();
      return;
  }
}

/* Receive function gets called when master sends data. */
void i2c_receive(int nr_of_bytes){
  // Restart no-comms count down, even if no data is sent.
  i2c_time_left_to_reset_millis = i2c_no_comms_reset_millis;

  // Return if no data.
  if (nr_of_bytes == 0) return;

  // Assume we have some data.
  byte func = Wire.read();
  nr_of_bytes -= 1;

  // Insta-return if no data is available (read returns -1).
  if (func == 0xFF) return;

  // Set register in case we need to respond.
  current_register = func;

  // Handle pure receive cases.
  switch(static_cast<PID6DriveRegister>(func)) {

    case PID6DriveRegister::DISABLE_ALL:
      for (int i = 0; i < 6; i++) enable[i] = false;
      if (nr_of_bytes != 0) i2c_error();
      return;

#define SET_TARGET_MACRO(i) \
    case PID6DriveRegister::SET_TARGET_##i: { \
      if (nr_of_bytes != 2) { i2c_error(); return; } \
      targets[i] = read_int16(); \
      return; \
    }

    EACH(SET_TARGET_MACRO)


#define ENABLE_MACRO(i) \
    case PID6DriveRegister::ENABLE_##i: { \
      if (nr_of_bytes != 1) { i2c_error(); return; } \
      enable[i] = static_cast<bool>(Wire.read()); \
      return; \
    }

    EACH(ENABLE_MACRO)


#define INVERT_MACRO(i) \
    case PID6DriveRegister::INVERT_##i: { \
      if (nr_of_bytes != 1) { i2c_error(); return; } \
      invert[i] = static_cast<bool>(Wire.read()); \
      return; \
    }

    EACH(INVERT_MACRO)


#define OUTPUT_IDX_MACRO(i) \
    case PID6DriveRegister::OUTPUT_IDX_##i: { \
      if (nr_of_bytes != 1) { i2c_error(); return; } \
      int8_t new_idx = static_cast<int8_t>(Wire.read()); \
      if (new_idx >= -1 and new_idx < 6) output_idx[i] = new_idx; \
      return; \
    }

    EACH(OUTPUT_IDX_MACRO)


#define SET_PID_PARAM_MACRO(i, param, attr) \
    case PID6DriveRegister::SET_PID_##param##_##i: { \
      if (nr_of_bytes != 2) { i2c_error(); return; } \
      pids[i].attr = read_int16(); \
      return; \
    }

#define SET_PID_P_MACRO(i) SET_PID_PARAM_MACRO(i, P, p)
#define SET_PID_I_TIME_MACRO(i) SET_PID_PARAM_MACRO(i, I_TIME, i_time)
#define SET_PID_D_TIME_MACRO(i) SET_PID_PARAM_MACRO(i, D_TIME, d_time)
#define SET_PID_THRESHOLD_MACRO(i) SET_PID_PARAM_MACRO(i, THRESHOLD, threshold)
#define SET_PID_OVERSHOOT_MACRO(i) SET_PID_PARAM_MACRO(i, OVERSHOOT, overshoot)

    EACH(SET_PID_P_MACRO)
    EACH(SET_PID_I_TIME_MACRO)
    EACH(SET_PID_D_TIME_MACRO)
    EACH(SET_PID_THRESHOLD_MACRO)
    EACH(SET_PID_OVERSHOOT_MACRO)


#define INPUT_IDX_MACRO(i) \
    case PID6DriveRegister::INPUT_IDX_##i: { \
      if (nr_of_bytes != 1) { i2c_error(); return; } \
      int8_t new_idx = static_cast<int8_t>(Wire.read()); \
      if (new_idx >= -1 and new_idx < 6) input_idx[i] = new_idx; \
      return; \
    }

    EACH(INPUT_IDX_MACRO)


#define DRIVE_MACRO(i) \
    case PID6DriveRegister::DRIVE_##i: { \
      if (nr_of_bytes != 4) { i2c_error(); return; } \
      drive_power[i] = read_int16(); \
      drive_time[i] = read_int16(); \
      return; \
    }

    EACH(DRIVE_MACRO)

#define SEEKING_MACRO(i) \
    case PID6DriveRegister::SEEKING_##i: { \
      if (nr_of_bytes != 1) { i2c_error(); return; } \
      seeking[i] = static_cast<bool>(Wire.read()); \
      return; \
    }

    EACH(SEEKING_MACRO)



    case PID6DriveRegister::SET_ALL_TARGETS:
      if (nr_of_bytes != 12) { i2c_error(); return; }
      for (int i = 0; i < 6; i++) targets[i] = read_int16();
      return;

    case PID6DriveRegister::SET_CONFIGURED:
      if (nr_of_bytes != 1) { i2c_error(); return; }
      // Faulty reads have the value 0xFF, ignore those for the configured flag.
      configured = (Wire.read() == 1);
      return;

    case PID6DriveRegister::DRIVE_ALL:
      if (nr_of_bytes != 24) { i2c_error(); return; }
      for (int i = 0; i < 6; i++) {
        drive_power[i] = read_int16();
        drive_time[i] = read_int16();
      }
      return;

    default:
      if (nr_of_bytes != 0) i2c_error();
      // Let all request cases fall through.
      return;
  }
}

#undef EACH
#undef GET_INPUT_MACRO
#undef SET_TARGET_MACRO
#undef GET_TARGET_MACRO
#undef ENABLE_MACRO
#undef INVERT_MACRO
#undef OUTPUT_IDX_MACRO
#undef SET_PID_PARAM_MACRO
#undef SET_PID_P_MACRO
#undef SET_PID_I_TIME_MACRO
#undef SET_PID_D_TIME_MACRO
#undef SET_PID_THRESHOLD_MACRO
#undef SET_PID_OVERSHOOT_MACRO
#undef GET_ERROR_MACRO
#undef INPUT_IDX_MACRO
#undef DRIVE_MACRO
#undef SEEKING_MACRO

// Startup
// -------

void setup() {
  Serial.begin(115200);

  // Setup pin modes and initial values
  // ----------------------------------


  // Read the address (do this before setting the RCLK and LED_ERROR pins).
  pinMode(ADDRESS0, INPUT_PULLUP);
  pinMode(ADDRESS1, INPUT_PULLUP);
  bitWrite(i2c_address, 0, !digitalRead(ADDRESS0));
  bitWrite(i2c_address, 1, !digitalRead(ADDRESS1));


  // Direction pins.
  pinMode(DIR8, OUTPUT);
  digitalWrite(DIR8, LOW);
  pinMode(DIR9, OUTPUT);
  digitalWrite(DIR9, LOW);
  pinMode(DIR10, OUTPUT);
  digitalWrite(DIR10, LOW);
  pinMode(DIR11, OUTPUT);
  digitalWrite(DIR11, LOW);

  // Input pins.
  pinMode(IN0, INPUT);
  pinMode(IN1, INPUT);
  pinMode(IN2, INPUT);
  pinMode(IN3, INPUT);
  pinMode(IN4, INPUT);
  pinMode(IN5, INPUT);

  // Read once to initialize input values.
  inputs[0] = analogRead(IN0);
  inputs[1] = analogRead(IN1);
  inputs[2] = analogRead(IN2);
  inputs[3] = analogRead(IN3);
  inputs[4] = analogRead(IN4);
  inputs[5] = analogRead(IN5);


  // Power control pins.
  pinMode(PWM0, OUTPUT);
  analogWrite(PWM0, 0);
  pinMode(PWM1, OUTPUT);
  analogWrite(PWM1, 0);
  pinMode(PWM2, OUTPUT);
  analogWrite(PWM2, 0);
  pinMode(PWM3, OUTPUT);
  analogWrite(PWM3, 0);
  pinMode(PWM4, OUTPUT);
  analogWrite(PWM4, 0);
  pinMode(PWM5, OUTPUT);
  analogWrite(PWM5, 0);


  // Shift register pins.
  pinMode(SRCLK, OUTPUT);
  digitalWrite(SRCLK, LOW);
  pinMode(SER, OUTPUT);
  digitalWrite(SER, LOW);
  pinMode(RCLK, OUTPUT);
  digitalWrite(RCLK, LOW);

  // Error led pin.
  pinMode(LED_ERROR, OUTPUT);
  digitalWrite(LED_ERROR, LOW);


  // Join the i2c bus as slave.
  Wire.begin(i2c_address);
  Wire.onReceive(i2c_receive);
  Wire.onRequest(i2c_request);

  // Initialize time keeping.
  last_micros = micros();
}

void loop() {
  // Time keeping
  // ------------

  unsigned long loop_start_micros = micros();
  const long elapsed_micros = loop_start_micros - last_micros;
  last_micros = loop_start_micros;
  const int elapsed_millis = (elapsed_micros + 500) / 1000;
  // Exponentially average the loop time with gamma = 0.5.
  loop_interval_micros = (loop_interval_micros * 80 + elapsed_micros * 20 + 50) / 100;

  // Inputs and control
  // ------------------

  // Read inputs, prevent interrupts from sending over half of an input.
  noInterrupts();
  exp_avg_analog_read(IN0, 0);
  exp_avg_analog_read(IN1, 1);
  exp_avg_analog_read(IN2, 2);
  exp_avg_analog_read(IN3, 3);
  exp_avg_analog_read(IN4, 4);
  exp_avg_analog_read(IN5, 5);
  interrupts();


  // Compute required values for the direction pins; initialize to 0 power.
  bool direction[12] = {};
  int power[6] = {};

  for (int i = 0; i < 6; i++) {
    // Skip if unit not enabled.
    if (not enable[i]) continue;

    int control = 0;

    // Apply timed pressure regardless of seeking position.
    if (drive_time[i] > 0) {
      drive_time[i] -= elapsed_millis;

      control += drive_power[i];
    }

    // Update PID control if seeking to a position.
    const int in_idx = input_idx[i];
    if (seeking[i] and in_idx != -1 and not error_pin[in_idx]) {
      // Guard against updates to the parameters or target.
      noInterrupts();
      const int position = inputs[in_idx];
      pids[i].update(position, targets[i], elapsed_millis);
      control += (invert[i] ? -1 : +1) * pids[i].control;
      interrupts();
    }

    // Get the output index.
    const int out_idx = output_idx[i];
    if (out_idx == -1) continue;

    // Run in control direction at control strength.
    power[out_idx] = min(abs(control), 255);
    direction[out_idx*2 + 0] = control > 0;
    direction[out_idx*2 + 1] = control < 0;
    // Note that for 0 control the motor will run freely untill stop.
  }

  // Set pin outputs to computed values.

  // Last bit into shift register is the QA output (direction 0).
  bool sr_bits[8] = {
    direction[7], direction[6], direction[5], direction[4],
    direction[3], direction[2], direction[1], direction[0]};
  // Also send 10 zero bits to reset the shift register in case
  // a spike or brown-out put it in a dodgy state.
  send_sr_bits<8>(10, sr_bits);
  digitalWrite(DIR8, direction[8]);
  digitalWrite(DIR9, direction[9]);
  digitalWrite(DIR10, direction[10]);
  digitalWrite(DIR11, direction[11]);

  // Write pulse width modulation levels.
  analogWrite(PWM0, power[0]);
  analogWrite(PWM1, power[1]);
  analogWrite(PWM2, power[2]);
  analogWrite(PWM3, power[3]);
  analogWrite(PWM4, power[4]);
  analogWrite(PWM5, power[5]);


  // Error handling
  // --------------

  // Use a temporary flag so we update the global state in a single step.
  bool new_error_state = false;
  for (int i = 0; i < 6; i++) new_error_state |= get_error(i);
  error_state = new_error_state;


  // Reset the i2c bus if there wasn't any transmission for the reset time.
  if (i2c_time_left_to_reset_millis < 0) {
    i2c_reset();
    blink_remaining_millis = 20;
    blink_halfperiod_millis = 5;
    i2c_time_left_to_reset_millis = i2c_no_comms_reset_millis;
  } else {
    i2c_time_left_to_reset_millis -= elapsed_millis;
  }


  // Set the error state unless we have to blink the LED.
  if (blink_remaining_millis > 0) {
    const bool blink_state = (blink_remaining_millis / blink_halfperiod_millis) % 2 == 0;
    digitalWrite(LED_ERROR, blink_state);

    // Lower remaining blink time, or 0 if it would turn negative.
    if (elapsed_millis > blink_remaining_millis) blink_remaining_millis = 0;
    else blink_remaining_millis -= elapsed_millis;

  // No blinking required, set the LED based on input error.
  } else {
    digitalWrite(LED_ERROR, error_state);
  }


  // Cap loop frequency.
  const int remaining_micros = loop_delay_micros - ((micros()-loop_start_micros));
  if (remaining_micros > 0) delayMicroseconds(remaining_micros);
}
