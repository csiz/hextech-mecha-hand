#pragma once

#include <stdio.h>


#include "LiquidCrystal_I2C.h"

template<size_t COLS, size_t ROWS>
class LCD{
  LiquidCrystal_I2C lcd_i2c;
  int min_update_duration;

  // Limit the update frequency of the lcd.
  unsigned long last_refresh_millis = 0;

  // Text that's already on screen. If the new text is the same, we
  // can skip sending it over the wire.
  char text_displayed[ROWS][COLS+1] = {};

  // We need to pad with spaces when sending text.
  char text_to_send[COLS+1] = {};

public:

  // Define the text buffer for the lcd, include terminating null byte.
  char text[ROWS][COLS+1] = {};

  // Address and how often to update the lcd screen (milliseconds).
  LCD(uint8_t address, int min_update_duration = 100) :
    // Set LCD address, number of columns and rows.
    lcd_i2c(address, COLS, ROWS),
    min_update_duration(min_update_duration)
  {}

  void begin() {
    // Initialize LCD and backlight.
    lcd_i2c.begin();
    lcd_i2c.backlight();
    // Store last write to the LCD so we don't write so often.
    last_refresh_millis = millis();

  }

  void update() {
    // Don't update faster than the minimum.
    if (millis() - last_refresh_millis < min_update_duration) return;

    for (uint8_t row = 0; row < ROWS; row++) {
      // Skip if the text already on screen is the same.
      if (strcmp(text[row], text_displayed[row]) == 0) continue;

      // Fix the text string to fill out any null chars after the first. Note
      // that the last null char should be left as is.
      copy_with_spaces<COLS>(text_to_send, text[row]);

      // Show the new string.
      lcd_i2c.setCursor(0, row);
      lcd_i2c.print(text_to_send);

      // Remember the displayed text.
      strcpy(text_displayed[row], text[row]);
    }

    // Update the last refresh time.
    last_refresh_millis = millis();
  }

  template<size_t L>
  void copy_with_spaces(char * dst, char * src) {
    bool reached_end = false;
    for (int i = 0; i < L; i++){
      if (src[i] == '\0') reached_end = true;
      // If we reached the end insert spaces, otherwise copy.
      dst[i] = reached_end ? ' ' : src[i];
    }
  }
};

