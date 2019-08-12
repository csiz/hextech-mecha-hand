#include <stdint.h>

#include <Wire.h>

#include "pid.hpp"
#include "pid6drive_interface.hpp"


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

// Error LED (in case a short it detected).
#define LED_ERROR 7 // dual use with address 0.

// Timing
#define LOOP_FREQUENCY 100
const int loop_delay_micros = 1000000 / LOOP_FREQUENCY;
unsigned long last_micros;
unsigned long loop_interval_millis = 0;

// I2C address.
int i2c_address = 0x60;

// Input positions, 10bits.
int inputs[6] = {0, 0, 0, 0, 0, 0};

// Target positions, 10bits.
int targets[6] = {0, 0, 0, 0, 0, 0};

// Enable input/output combination.
bool enable[6] = {false, false, false, false, false, false};

// Invert outputs so that positive error leads to negative control.
bool invert[6] = {false, false, false, false, false, false};

// Output indexes of input pin, -1 for unset.
int8_t output_idx[6] = {-1, -1, -1, -1, -1, -1};

// PID controller for outputs. Indexed by output pins.
HysterisisPID8bit pids[6];

// Error flag.
bool error_state = false;
bool error_pin[6] = {false, false, false, false, false, false};

// Exponentially average analog reads so we don't get such large fluctuations.
inline void exp_avg_analog_read(const uint8_t pin, int & value) {
  int new_value = analogRead(pin);
  if (new_value > IN_LOW_THRESHOLD and new_value < IN_HI_THRESHOLD) {
    error_pin[pin] = false;
    value = (value * 60 + new_value * 40) / 100;
  } else {
    error_pin[pin] = true;
  }
}

// Read raw inputs.
inline void read_inputs(){
  inputs[0] = analogRead(IN0);
  inputs[1] = analogRead(IN1);
  inputs[2] = analogRead(IN2);
  inputs[3] = analogRead(IN3);
  inputs[4] = analogRead(IN4);
  inputs[5] = analogRead(IN5);
}

// Read exp average inputs.
inline void read_exp_avg_inputs(){
  exp_avg_analog_read(IN0, inputs[0]);
  exp_avg_analog_read(IN1, inputs[1]);
  exp_avg_analog_read(IN2, inputs[2]);
  exp_avg_analog_read(IN3, inputs[3]);
  exp_avg_analog_read(IN4, inputs[4]);
  exp_avg_analog_read(IN5, inputs[5]);
}

// Send bits to the shift register, QA bit last.
template<size_t N>
void send_sr_bits(bool bits[N]) {
  for (size_t i = 0; i < N; i++) {
    // Use no-ops for timing requirements. One _NOP at 16MHz frequency is 62.5ns.
    digitalWrite(SER, bits[i]);
    _NOP(); // 30ns minimum
    digitalWrite(SRCLK, HIGH);
    _NOP(); // 20ns minimum
    digitalWrite(SRCLK, LOW);
  }
  digitalWrite(RCLK, HIGH);
  _NOP(); // 20ns minimum
  digitalWrite(RCLK, LOW);
}

void i2c_request(){
  // Use at most 3 bytes per function, if we neeed a 2byte value, then
  // read/send hi first lo second. Ignore incomplete instructions.
  byte func = 0;
  byte hi = 0;
  byte lo = 0;

#define READ(what) if (Wire.available()) { what = Wire.read(); } else { return; }
#define EACH(macro) macro(0) macro(1) macro(2) macro(3) macro(4) macro(5)

i2c_func:
  READ(func);

  switch(static_cast<PID6Drive>(func)) {
  case PID6Drive::DISABLE_ALL: for (int i = 0; i < 6; i++) { enable[i] = false; } goto i2c_func;

#define GET_INPUT_MACRO(i) case PID6Drive::GET_INPUT_##i: { hi = inputs[i] >> 8; lo = inputs[i] & 0xFF; Wire.write(hi); Wire.write(lo); } goto i2c_func;

  EACH(GET_INPUT_MACRO)

#define SET_TARGET_MACRO(i) case PID6Drive::SET_TARGET_##i: { READ(hi); READ(lo); targets[i] = static_cast<int>(hi) << 8 | lo; } goto i2c_func;

  EACH(SET_TARGET_MACRO)

#define GET_TARGET_MACRO(i) case PID6Drive::GET_TARGET_##i: { hi = targets[i] >> 8; lo = targets[i] & 0xFF; Wire.write(hi); Wire.write(lo); } goto i2c_func;

  EACH(GET_TARGET_MACRO)

#define ENABLE_MACRO(i) case PID6Drive::ENABLE_##i: { READ(lo); enable[i] = static_cast<bool>(lo); } goto i2c_func;

  EACH(ENABLE_MACRO)

#define INVERT_MACRO(i) case PID6Drive::INVERT_##i: { READ(lo); invert[i] = static_cast<bool>(lo); } goto i2c_func;

  EACH(INVERT_MACRO)

#define OUTPUT_IDX_MACRO(i) \
  case PID6Drive::OUTPUT_IDX_##i: { \
  READ(lo); int8_t new_idx = static_cast<int8_t>(lo); \
  if (new_idx >= -1 and new_idx < 6) { output_idx[i] = new_idx; } } \
  goto i2c_func;

  EACH(OUTPUT_IDX_MACRO)

#define SET_PID_PARAM_MACRO(i, param, attr) case PID6Drive::SET_PID_##param##_##i: { READ(hi); READ(lo); pids[i].attr = static_cast<int>(hi) << 8 | lo; } goto i2c_func;

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


#define GET_ERROR_MACRO(i) case PID6Drive::GET_ERROR_##i: { lo = error_pin[i] & 0xFF; Wire.write(lo); } goto i2c_func;

  EACH(GET_ERROR_MACRO)


  case PID6Drive::GET_ERROR_STATE: Wire.write(error_state); goto i2c_func;

  case PID6Drive::GET_ALL_INPUTS:
    for (int i = 0; i < 6; i++) {
      hi = inputs[i] >> 8;
      lo = inputs[i] & 0xFF;
      Wire.write(hi);
      Wire.write(lo);
    }
    goto i2c_func;

  case PID6Drive::SET_ALL_TARGETS:
    for (int i = 0; i < 6; i++) {
      READ(hi);
      READ(lo);
      targets[i] = static_cast<int>(hi) << 8 | lo;
    }
    goto i2c_func;

  case PID6Drive::GET_LOOP_INTERVAL: { hi = loop_interval_millis >> 8; lo = loop_interval_millis & 0xFF; Wire.write(hi); Wire.write(lo); } goto i2c_func;

  }

#undef READ
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
}

void setup() {
  // Setup pin modes and initial values.

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
  read_inputs();

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



  // Zero the shift regisert; we don't have enough pins for SRCLR so we need
  // to push zeroes in the values.
  bool zeroes[8] = {0};
  send_sr_bits<8>(zeroes);


  // Join the i2c bus as a slave.
  Wire.begin(i2c_address);
  Wire.onRequest(i2c_request);

  // Initialize time keeping.
  last_micros = micros();
}

void loop() {
  // Time keeping.
  unsigned long loop_start_micros = micros();
  const int elapsed_micros = loop_start_micros - last_micros;
  last_micros = loop_start_micros;
  const int elapsed_millis = (elapsed_micros + 500) / 1000;
  // Exponentially average the loop time with gamma = 0.99.
  loop_interval_millis = (loop_interval_millis * 99 + elapsed_millis) / 100;

  // Read inputs.
  read_exp_avg_inputs();

  // Update pid controllers.
  for (int i = 0; i < 6; i++) {
    // Skip errored pins.
    if(error_pin[i]) continue;

    // Guard against updates to the parameters or target.
    noInterrupts();
    pids[i].update(inputs[i], targets[i], elapsed_millis);
    interrupts();
  }

  // Compute required values for the direction pins.
  bool direction[12] = {};
  int power[6] = {};

  for (int i = 0; i < 6; i++) {
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

  // Last bit into shift register is the QA output (direction 0).
  bool sr_bits[8] = {
    direction[7], direction[6], direction[5], direction[4],
    direction[3], direction[2], direction[1], direction[0]};
  send_sr_bits<8>(sr_bits);
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


  // Set the error state and led.

  // Use a temporary flag so we update the global state in a single step.
  bool new_error_state = false;
  for (int i = 0; i < 6; i++) new_error_state |= error_pin[i];
  error_state = new_error_state;
  digitalWrite(LED_ERROR, error_state);

  // Cap loop frequency.
  const int remaining_micros = loop_delay_micros - ((micros()-loop_start_micros));
  if (remaining_micros > 0) delayMicroseconds(remaining_micros);
}
