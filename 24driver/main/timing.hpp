#pragma once

#include "Arduino.h"

#include <functional>
#include <algorithm>

namespace timing {

  struct ExponentialAverage {
    // Store the already inverted half life to avoid repeated divisions.
    const float inverse_life;
    ExponentialAverage(const float half_life) : inverse_life(1.0 / (2 * half_life)) {}

    float operator()(const float value, const float last_value, const float duration) const {
      const float gamma = duration * inverse_life;
      // Use just the current value if enough time passed.
      return (gamma > 1.0) ? value : (value * gamma + last_value * (1-gamma));
    }
  };


  struct LoopTimer {
    // Time of last update (milliseconds).
    unsigned long update_time = 0;
    // Duration between last updates (seconds).
    float loop_duration = 0.0;

    // How often the loop is called.
    float fps = 0.0;
    // Exp average fps over 2 seconds.
    ExponentialAverage exp_avg_fps = {2.0};
    // Approximate maximum loop duration (seconds).
    float max_loop_duration = 0.0;
    // Exponentially decay towards current loop time over 10 seconds.
    ExponentialAverage exp_avg_max_loop_duration = {10.0};

    void begin(){
      update_time = millis();
    }

    // Update the timing statistics and delay such that the time
    // beteween updates is at least throttle period (milliseconds).
    void update(unsigned long throttle_period_millis = 0){
      unsigned long time = millis();
      const unsigned long start_duration_millis = time - update_time;

      if (start_duration_millis < throttle_period_millis) {
        delay(throttle_period_millis - start_duration_millis);
        // We waited a bit, so get time again.
        time = millis();
      }

      // Cap minimum duration to at least 1ms.
      loop_duration = 0.001 * std::max(1ul, time - update_time);

      update_time = time;

      // Compute exponentially averaged fps. Not dividing by zero because of the above cap.
      const float loop_fps = 1.0 / loop_duration;
      fps = exp_avg_fps(loop_fps, fps, loop_duration);

      // Set the maximum duration between updates, but exponentially average down to the current loop duration.
      max_loop_duration = std::max(
        loop_duration,
        exp_avg_max_loop_duration(loop_duration, max_loop_duration, loop_duration));
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

}