#pragma once

#include <cmath>

// `clamp` is not available in the current compiler, use my own.
namespace mystd {
  inline float clamp(const float v, const float lo, const float hi){
    return v < lo ? lo : v > hi ? hi : v;
  }
}

/* Hysterisis PID control.

  Because there's backlash in the motor assembly it's imprecise to move backwards.
  Minimize wiggling by allowing a bit of error if we overshot the target.

  The output is clamped to [-1, +1], going for full reverse to full forward power.

  The input is expected in the range [0, 1] from minimum to max.

  Integral, differentiation and update times are in seconds.
*/
struct PID {

  // Full power when the error is 1/4 of the full range.
  float p = 4.0;
  // Integrate minor errors over 2 seconds.
  float i_time = 2.0;
  // Compensate for 50ms of momentum.
  float d_time = 0.050;

  // Error threshold where we output 0 control.
  float threshold = 0.01;
  // Threshold in the last control direction, allowing for some overshoot with control 0.
  float overshoot = 0.02;

  // Output power [-1, +1].
  float control = 0;

  // Last control direction, or 0 if control was 0.
  int last_direction = 0;

  float last_error = 0;
  float last_target = 0;

  // Control from the integral term.
  float integral_control = 0;


  /* Update PID controller, given current position, current target and time elapsed since last call (seconds). */
  float update(float current, float target, float elapsed){

    float error = target - current;

    using std::abs;
    using mystd::clamp;

    // Zero out error if within the threshold. Note that diff still works to stop momentum.
    // Same if we overshot (error is opposite sign of control) and we're within the limit.
    if ((abs(error) < threshold) or ((abs(error) < overshoot) and (error * last_direction < 0))) {
      error = 0;
      integral_control = 0;
    }

    // Check elapsed before dividing by 0 and ignore the diff term if so. Low threshold for elapsed is
    // 0.1ms which should be shorter than any update time, but just to be safe.
    const float diff = elapsed > 0.0001 ? (error-last_error - (target-last_target)) * d_time / elapsed : 0;

    // Update error and targets. We use the last target above to avoid compensating for errors due changing targets.
    last_error = error;
    last_target = target;

    // Keep a running integral of the error. Also avoid dividing by 0.
    if (i_time > 0.0001) {
      integral_control = integral_control + p * error * elapsed / i_time;
    }

    //  Compute the control due to proportional and derivative terms.
    const float pd_control = p * (error + diff);

    // Reset the integral if we are at maximum control without it.
    if (abs(pd_control) >= 1.0) integral_control = 0;

    // Add the integral term and clamp to valid output range.
    control = clamp(pd_control + integral_control, -1.0, +1.0);

    // Update direction as the last sign of control, maintaining if control is near 0.
    last_direction = control > 0.01 ? +1 : control < 0.01 ? -1 : last_direction;

    return control;
  }
};
