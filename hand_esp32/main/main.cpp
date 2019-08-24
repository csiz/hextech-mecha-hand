#include <stdio.h>

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

// Limit loop frequency to Hz.
#define LOOP_FREQUENCY 100
const int loop_delay_millis = 1000 / LOOP_FREQUENCY;

// Define pin connections
#define WHEEL_1_A GPIO_NUM_16
#define WHEEL_1_B GPIO_NUM_17

#define DIRECTION_BUTTON GPIO_NUM_23

// TODO: detect whether input passes these conditions and report shorts otherwise.
// ADC value where it's likely a short (at 10 bit resolution).
#define ADC_LOW 16 // ~0.06V
#define ADC_HIGH 848 // ~3.23V Note that 3.3V max input corresponds to 866.


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


Encoder wheel_1 {WHEEL_1_A, WHEEL_1_B};

const int pid_driver_0 = PID6DRIVE_ADDRESS + 0b00;


// PID parameters
// --------------
// TODO: set the arduino slave parameters.
int p = 2;
int i_time = 2000; // millis
// Capacitor adds 10ms momentum, and exp avg adds ~20ms lag.
int d_time = 30; // millis
int threshold = 4;
int overshoot = 8;


// Controls
int last_position;
int target = 512;
int target_per_tick = 8;


int inputs[3];


// Time keeping.
unsigned long last_micros = 0;

void IRAM_ATTR button_interrupt(void * arg);

struct Button {
  const gpio_num_t pin;
  volatile uint8_t presses = 0;
  unsigned long last_press = 0;
  int min_delay = 100;

  Button(gpio_num_t pin) : pin(pin) {}

  void begin(){
    pinMode(pin, INPUT_PULLDOWN);
    attachInterruptArg(pin, button_interrupt, this, RISING);
  }

  void end(){
    detachInterrupt(pin);
  }

  size_t collect_presses() {
    // Might definitely be a better way, but get presses and subtract from
    // stored presses. If interrupted mid-way we leave them for next time.
    uint8_t tmp = presses;
    presses -= tmp;
    return tmp;
  }
};

// Button debounce handling; reset every loop.
void IRAM_ATTR button_interrupt(void * arg) {
  Button* button = static_cast<Button*>(arg);

  auto now = millis();
  if (now - button->last_press > button->min_delay) {
    button->presses += 1;
    button->last_press = now;
  }
}

Button direction_button(DIRECTION_BUTTON);
bool reverse_direction = true;

// I2C helpers
// -----------

int nr_wire_errors = 0;

inline int read_int16() {
  return static_cast<int>(Wire.read()) << 8 | Wire.read();
}

inline void write_int16(const int value){
  Wire.write(value >> 8);
  Wire.write(value & 0xFF);
}

template<typename Reg>
inline void read_int16_from(const byte address, const Reg reg, int & value){
  Wire.beginTransmission(address);
  Wire.write(static_cast<byte>(reg));
  if(Wire.endTransmission(false)){
    nr_wire_errors += 1;
    return;
  }
  delayMicroseconds(20); // wait for arduino to process.
  if (Wire.requestFrom(address, 2u) != 2u) {
    nr_wire_errors += 1;
    return;
  }

  value = read_int16();
}

template<typename Reg>
inline void write_int16_to(const byte address, const Reg reg, const int value){
  Wire.beginTransmission(address);
  Wire.write(static_cast<byte>(reg));
  write_int16(value);
  if(Wire.endTransmission()) nr_wire_errors += 1;
}

template<typename Reg>
inline void read_from(const byte address, const Reg reg, byte & value) {
  Wire.beginTransmission(address);
  Wire.write(static_cast<byte>(reg));
  if(Wire.endTransmission(false)){
    nr_wire_errors += 1;
    return;
  }
  delayMicroseconds(20); // wait for arduino to process.
  if (Wire.requestFrom(address, 1u) != 1u) {
    nr_wire_errors += 1;
    return;
  }

  value = Wire.read();
}

template<typename Reg>
inline void write_to(const byte address, const Reg reg, const byte value){
  Wire.beginTransmission(address);
  Wire.write(static_cast<byte>(reg));
  Wire.write(value);
  if(Wire.endTransmission()) nr_wire_errors += 1;
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
  // Turn the power supply mosfte on.
  digitalWrite(POWER_CTRL, true);

  // Setup serial comms.
  Wire.begin();
  Serial.begin(115200);

  // initialize LCD
  lcd.begin();
  // turn on LCD backlight
  lcd.backlight();
  lcd_last_millis = millis();

  // Setup wheel encoder.
  wheel_1.begin();

  // Setup ADC finger position pins.
  analogReadResolution(10);
  analogSetWidth(10);

  // Temporary motor control.
  last_position = wheel_1.position;

  // Switch direction button.
  direction_button.begin();

  // Initialize time keeping.
  last_micros = micros();


  // Wait for the arduino to start and initialize.
  delay(500);

  // Experiments
  // -----------

  setup_piddrive(pid_driver_0);
}

void loop(){
  // Time keeping.
  unsigned long loop_start_micros = micros();
  const int elapsed_micros = loop_start_micros - last_micros;
  last_micros = loop_start_micros;
  const int elapsed_millis = (elapsed_micros + 500) / 1000;

  // Switch direction on an odd number of button presses.
  if (direction_button.collect_presses() % 2 == 1) {
    reverse_direction = !reverse_direction;
  }


  // Control.
  int tick_diff = wheel_1.position - last_position;
  last_position += tick_diff;
  target += tick_diff * target_per_tick;


  // Experiments
  // -----------

  // TODO: restart serial, does this do anything?
  Wire.begin();

  byte configured_0 = 0xFF;
  read_from(pid_driver_0, PID6Drive::GET_CONFIGURED, configured_0);

  if (configured_0 != 1) setup_piddrive(pid_driver_0);

  read_int16_from(pid_driver_0, PID6Drive::GET_INPUT_0, inputs[0]);
  read_int16_from(pid_driver_0, PID6Drive::GET_INPUT_1, inputs[1]);
  read_int16_from(pid_driver_0, PID6Drive::GET_INPUT_2, inputs[2]);

  write_int16_to(pid_driver_0, PID6Drive::SET_TARGET_1, target);

  write_to(pid_driver_0, PID6Drive::INVERT_1, reverse_direction);

  int pid6drive_loop_time = -1;
  read_int16_from(pid_driver_0, PID6Drive::GET_LOOP_INTERVAL, pid6drive_loop_time);

  // Display
  // -------

  // Text to display.
  lcd_dirty = true;

  snprintf(lcd_text[0], LCD_COLUMNS+1, "%4d %4d %4d %1d", inputs[0], inputs[1], inputs[2], reverse_direction);
  snprintf(lcd_text[1], LCD_COLUMNS+1, "T%4d E%3d L%3d", target, nr_wire_errors %1000, pid6drive_loop_time);

  // LCD
  if (lcd_dirty and (millis() - lcd_last_millis) > 250) {
    lcd.clear();
    for (uint8_t row = 0; row < LCD_ROWS; row++) {
      lcd.setCursor(0, row);
      lcd.print(lcd_text[row]);
    }
    lcd_dirty = false;
    lcd_last_millis = millis();
  }

  // Cap loop frequency.
  const int remaining_millis = loop_delay_millis - ((micros()-loop_start_micros) / 1000);
  if (remaining_millis > 0) delay(remaining_millis);
}
