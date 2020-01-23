#pragma once

#include "pins.hpp"

#include "Wire.h"
#include "Arduino.h"
#include "esp_adc_cal.h"

#include "Adafruit_SSD1306.h"



namespace ui {

  const int screen_width = 128; // OLED display width, in pixels
  const int screen_height = 32; // OLED display height, in pixels

  bool screen_initialized = false;

  // Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
  // Reset pin # (or -1 if sharing Arduino reset pin)
  Adafruit_SSD1306 display(screen_width, screen_height, &Wire, -1);

  unsigned long last_screen_update;


  void setup(){
    // Initialize the buttons and joystick pins. They have hardware pull-up
    // resistors and 1k & 100nF ~= 1.6kHz low pass filters.
    pinMode(BTN0, INPUT);
    pinMode(BTN1, INPUT);
    pinMode(JBTN, INPUT);

    pinMode(J0, ANALOG);
    adcAttachPin(J0);
    analogSetPinAttenuation(J0, ADC_11db); // Full scale range of 3.9V

    pinMode(J1, ANALOG);
    adcAttachPin(J1);
    analogSetPinAttenuation(J1, ADC_11db); // Full scale range of 3.9V

    // Turn the status LED off.
    pinMode(LED0, OUTPUT);
    digitalWrite(LED0, LOW);


    // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
    screen_initialized = display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
    // Clear the buffer
    display.clearDisplay();
    // Show blank screen.
    display.display();

    // Initialize last screen update timing.
    last_screen_update = millis();
  }

  void update(){
    // Try and re-setup, but if it doesn't work then nothing to display.
    if (not screen_initialized) {
      setup();
      if (not screen_initialized) return;
    }


  }
}