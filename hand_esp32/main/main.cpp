/* Blink Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>

#include "Arduino.h"
#include "LiquidCrystal_I2C.h"

// TODO: remove once vs code can find these files on its own.
#include "esp32-hal-gpio.h"

#include "esp_attr.h"
#include "driver/gpio.h"

#include "sdkconfig.h"

#include "quadrature_encoder.hpp"

// Define pin connections
#define WHEEL_1_A GPIO_NUM_16
#define WHEEL_1_B GPIO_NUM_17


// set the LCD number of columns and rows
const int lcd_columns = 16;
const int lcd_rows = 2;

// set LCD address, number of columns and rows
// if you don't know your display address, run an I2C scanner sketch
LiquidCrystal_I2C lcd(0x27, lcd_columns, lcd_rows);


Encoder wheel_1 {WHEEL_1_A, WHEEL_1_B};

void setup(){
  // initialize LCD
  lcd.begin();
  // turn on LCD backlight
  lcd.backlight();

  wheel_1.begin();
}


void loop(){

  lcd.clear();
  // set cursor to first column, first row
  lcd.setCursor(0, 0);
  // print message
  lcd.print(wheel_1.position);

  delay(50);
}

