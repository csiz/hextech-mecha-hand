#pragma once

#include "pins.hpp"
#include "timing.hpp"
#include "web.hpp"
#include "state.hpp"

#include "Wire.h"
#include "Arduino.h"
#include "esp_adc_cal.h"

#include "Adafruit_SSD1306.h"

#include <stdio.h>

namespace ui {

  const int screen_width = 128; // OLED display width, in pixels
  const int screen_height = 32; // OLED display height, in pixels

  bool screen_initialized = false;

  // Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
  // Reset pin # (or -1 if sharing Arduino reset pin)
  Adafruit_SSD1306 display(screen_width, screen_height, &Wire, -1);


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
  }

  // Throttle UI updates so we don't waste I2C time.
  auto update = timing::throttle_function([](){
    // Try and re-setup, but if it doesn't work then nothing to display.
    if (not screen_initialized) {
      setup();
      if (not screen_initialized) return;
    }

    using state::state;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    // Maximum characters per line for text size 1 plus \0 termination.
    const size_t max_chars = 21+1;
    char line[max_chars];

    // First line contains wifi network name.
    if (web::ok) {
      if (web::connected_to_router) {
        snprintf(line, max_chars, "Sta: %s", web::router_ssid);
      } else {
        snprintf(line, max_chars, "AP: %s", web::ap_ssid);
      }
    } else {
      snprintf(line, max_chars, "Error initialzing WiFi!");
    }
    display.println(line);

    // Second line is local IP and active controller last IP byte.
    snprintf(line, max_chars, "IP:%s C:%u", web::ip.toString().c_str(), web::last_command_ip[3]);
    display.println(line);

    // Third line is timing information.
    snprintf(line, max_chars, "%4lus fps:%.0f max:%.0fms", (state.update_time / 1000) % 10000, state.fps, 1000.0 * state.max_loop_duration);
    display.println(line);

    // Fourth line shows power information.
    snprintf(line, max_chars, "%.1fV %.3fA %.1fJ", state.voltage, state.current, state.energy);
    display.println(line);

    // TODO: show shutdown on low voltage message.

    display.display();

  }, /* throttle_period (millis) = */ 500);
}