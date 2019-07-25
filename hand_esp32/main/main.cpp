/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>

#include "Arduino.h"
#include "LiquidCrystal_I2C.h"

#include "esp_attr.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "sdkconfig.h"

#include "quadrature_encoder.hpp"
#include "pid.hpp"

// Limit loop frequency to Hz.
#define LOOP_FREQUENCY 100
const int loop_delay_millis = 1000 / LOOP_FREQUENCY;

// Define pin connections
#define WHEEL_1_A GPIO_NUM_16
#define WHEEL_1_B GPIO_NUM_17

#define MOTOR_1_EN GPIO_NUM_5
#define MOTOR_1_CHANNEL LEDC_CHANNEL_0
#define MOTOR_1_L GPIO_NUM_18
#define MOTOR_1_R GPIO_NUM_19

#define DIRECTION_BUTTON GPIO_NUM_23

#define FINGER_1_J1 GPIO_NUM_36
#define FINGER_1_J2 GPIO_NUM_39
#define FINGER_1_J3 GPIO_NUM_34

// TODO: detect whether input passes these conditions and report shorts otherwise.
// ADC value where it's likely a short (at 10 bit resolution).
#define ADC_LOW 16 // ~0.06V
#define ADC_HIGH 848 // ~3.23V Note that 3.3V max input corresponds to 866.

// Don't actuate motors under this value.
#define CONTROL_THRESHOLD 8

// Exponentially average analog reads so we don't get such large fluctuations.
inline void exp_avg_analog_read(const uint8_t pin, int & value) {
  value = (value * 60 + analogRead(pin) * 40) / 100;
}

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

HysterisisPID8bit pid_1 {2, 2000/* millis */, 30/* millis */, 4/* threshold */, 8/* overshoot */};
// Capacitor adds 10ms momentum, and exp avg adds ~20ms lag.


// Controls
int last_position;
int target = 512;
int target_per_tick = 8;

// Inputs
int finger_1_j1 = 0;
int finger_1_j2 = 0;
int finger_1_j3 = 0;


// Time keeping.
unsigned long last_micros = 0;

bool switch_direction = false;
int direction = +1;

// TODO: debouncing needs improvement.
// Button debounce handling; reset every loop.
void IRAM_ATTR button_interrupt(void * arg) {
  *static_cast<bool*>(arg) = true;
}


void setup(){
  // initialize LCD
  lcd.begin();
  // turn on LCD backlight
  lcd.backlight();
  lcd_last_millis = millis();

  // Setup wheel encoder.
  wheel_1.begin();

  // Set motor 1 pins.
  pinMode(MOTOR_1_EN, OUTPUT);
  ledcSetup(MOTOR_1_CHANNEL, 500, 8);
  ledcAttachPin(MOTOR_1_EN, MOTOR_1_CHANNEL);
  pinMode(MOTOR_1_L, OUTPUT);
  digitalWrite(MOTOR_1_L, false);
  pinMode(MOTOR_1_R, OUTPUT);
  digitalWrite(MOTOR_1_R, false);

  // Setup ADC finger position pins.
  analogReadResolution(10);
  analogSetWidth(10);

  pinMode(FINGER_1_J1, ANALOG);
  pinMode(FINGER_1_J2, ANALOG);
  pinMode(FINGER_1_J3, ANALOG);


  // Temporary motor control.
  last_position = wheel_1.position;

  // Switch direction button.
  pinMode(DIRECTION_BUTTON, INPUT_PULLDOWN);
  attachInterruptArg(DIRECTION_BUTTON, button_interrupt, &switch_direction, RISING);

  // Initialize time keeping.
  last_micros = micros();
}

void loop(){
  // Time keeping.
  unsigned long loop_start_micros = micros();
  const int elapsed_micros = loop_start_micros - last_micros;
  last_micros = loop_start_micros;
  const int elapsed_millis = (elapsed_micros + 500) / 1000;

  // Check direction button and reset.
  if (switch_direction) {
    direction *= -1;
    switch_direction = false;
  }

  // Finger positions.
  exp_avg_analog_read(FINGER_1_J1, finger_1_j1);
  exp_avg_analog_read(FINGER_1_J2, finger_1_j2);
  exp_avg_analog_read(FINGER_1_J3, finger_1_j3);


  // Motor control.
  int tick_diff = wheel_1.position - last_position;
  last_position += tick_diff;

  target += tick_diff * target_per_tick;


  pid_1.update(finger_1_j2, target, elapsed_millis);


  if (abs(pid_1.control) < CONTROL_THRESHOLD) {
    // Free run.
    ledcWrite(MOTOR_1_CHANNEL, 0);
  } else {
    // Run in control direction at control strength.
    digitalWrite(MOTOR_1_L, pid_1.control * direction > 0);
    digitalWrite(MOTOR_1_R, pid_1.control * direction < 0);
    ledcWrite(MOTOR_1_CHANNEL, abs(pid_1.control));
  }



  // Text to display.
  lcd_dirty = true;

  // Text2, test out pid
  // snprintf(lcd_text[0], LCD_COLUMNS+1, "");
  snprintf(lcd_text[1], LCD_COLUMNS+1, "% 5d %5d %4d", pid_1.control, target, finger_1_j2);

  // LCD
  if (lcd_dirty and (millis() - lcd_last_millis) > 200) {
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

