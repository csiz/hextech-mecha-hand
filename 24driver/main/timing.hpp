#pragma once

#include "Arduino.h"

#include <functional>

struct LoopTimer {
  unsigned long last_loop_time = 0;
  unsigned long last_loop_duration = 0;

  float fps = 0.0;
  float fps_gamma = 0.99;

  void begin(){
    last_loop_time = millis();
  }

  void update(unsigned int throttle_period = 0){
    unsigned long time = millis();
    unsigned long duration = time - last_loop_time;

    if (duration < throttle_period) {
      delay(throttle_period - duration);
      // We waited a bit, so get time again.
      time = millis();
    }

    last_loop_duration = time - last_loop_time;
    last_loop_time = time;

    // Compute exponentially averaged fps.
    // Make sure not to divide by 0. Assume 1000 fps if duration between frames is less than 1 ms.
    fps = fps_gamma * fps + (1.0 - fps_gamma) * (1000.0 / (last_loop_duration ? last_loop_duration : 1));
  }
};

inline std::function<void()> throttle_function(std::function<void()> func, unsigned int throttle_period){
  unsigned long last_call_time = 0;

  return [func, throttle_period, last_call_time]() mutable {
    unsigned long time = millis();
    if (time - last_call_time >= throttle_period) {
      func();
      last_call_time = time;
    }
    // Otherwise do nothing this loop run and wait for next update.
  };
}