#pragma once


#include "Wire.h"

#include "Adafruit_SSD1306.h"



namespace ui {

  const int screen_width = 128; // OLED display width, in pixels
  const int screen_height = 32; // OLED display height, in pixels

  bool screen_initialized = false;

  // Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
  // Reset pin # (or -1 if sharing Arduino reset pin)
  Adafruit_SSD1306 display(screen_width, screen_height, &Wire, -1);


  void setup(){
    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    screen_initialized = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
    // Clear the buffer
    display.clearDisplay();
    // Show blank screen.
    display.display();
  }

  void update(){
    // Try and re-setup, but if it doesn't work then nothing to display.
    if (not screen_initialized) {
      setup();
      if (not screen_initialized) return;
    }


  }
}