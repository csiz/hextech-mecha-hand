#include <stdlib.h>

inline int clamp(const int v, const int lo, const int hi){
  return v < lo ? lo : v > hi ? hi : v;
}

/* PID control using 8bit control values (including negative). */
struct PID8bit {
  int p;
  int i_time;
  int d_time;
  int threshold;

  int control;

  int last_error = 0;
  int last_target = 0;

  int integral = 0;

  PID8bit(const int p, const int i_time, const int d_time, const int threshold)
    : p(p), i_time(i_time), d_time(d_time), threshold(threshold) {}


  void update(int current, int target, int elapsed){

    int error = target - current;

    // Zero out error if within the threshold. Note that diff still works to stop momentum.
    if (abs(error) < threshold) {
      error = 0;
      integral = 0;
    }

    // Multiply by the time constant before dividing by the current elapsed time to avoid truncating.
    // Avoid jerks from target changing.
    // Check elapsed before dividing by 0 and ignore the diff term if so.
    const int diff = elapsed ? (error-last_error - (target - last_target)) * d_time / elapsed : 0;
    last_error = error;
    last_target = target;

    // Keep a running integral of the error, don't divide by the time so we accumulate small errors.
    integral = clamp(integral + error * elapsed, -255 * i_time / p, +255 * i_time / p);

    const int pd_control = p * (error + diff);

    // Discard the integral if we are at maximum control without it.
    if (abs(pd_control) >= 255) integral = 0;

    // Clamp output to -255, +255. Basically 8bit value and direction as
    // the two are controlled separately.
    control = clamp(pd_control + p * integral / i_time, -255, +255);
  }
};

/* Hysterisis PID control using 8bit values (including negative).

  Because there's backlash in the motor assembly it's imprecise to move backwards.
  Minimize wiggling by allowing a bit of error if we overshot the target.
*/
struct HysterisisPID8bit {
  int p;
  int i_time;
  int d_time;
  int threshold;
  int overshoot;

  int control = 0;
  int direction = 0;

  int last_error = 0;
  int last_target = 0;

  int integral = 0;

  HysterisisPID8bit(
    const int p /* 8bit out / 10bit in */ = 2,
    const int i_time /* millis */ = 2000,
    const int d_time /* millis */ = 30,
    const int threshold /* 10bit in */ = 4,
    const int overshoot /* 10bit in */ = 8
    ) : p(p), i_time(i_time), d_time(d_time), threshold(threshold), overshoot(overshoot)
    {}


  void update(int current, int target, int elapsed){

    int error = target - current;

    // Zero out error if within the threshold. Note that diff still works to stop momentum.
    // Same if we overshot (error is opposite sign of control) and we're within the limit.
    if ((abs(error) < threshold) or ((abs(error) < overshoot) and (error * direction < 0))) {
      error = 0;
      integral = 0;
    }

    // Multiply by the time constant before dividing by the current elapsed time to avoid truncating.
    // Avoid jerks from target changing.
    // Check elapsed before dividing by 0 and ignore the diff term if so.
    const int diff = elapsed ? (error-last_error - (target - last_target)) * d_time / elapsed : 0;
    last_error = error;
    last_target = target;

    // Keep a running integral of the error, don't divide by the time so we accumulate small errors.
    integral = clamp(integral + error * elapsed, -255 * i_time / p, +255 * i_time / p);

    const int pd_control = p * (error + diff);

    // Discard the integral if we are at maximum control without it.
    if (abs(pd_control) >= 255) integral = 0;

    // Clamp output to -255, +255. Basically 8bit value and direction as
    // the two are controlled separately.
    control = clamp(pd_control + p * integral / i_time, -255, +255);

    // Update direction as the last sign of control, maintaining if control is 0.
    direction = control > 0 ? +1 : control < 0 ? -1 : direction;
  }
};
