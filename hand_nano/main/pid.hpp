#include <stdlib.h>

template<typename T>
inline T clamp(const T v, const T lo, const T hi){
  return v < lo ? lo : v > hi ? hi : v;
}


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

  float integral_control = 0;

  HysterisisPID8bit(
    const int p /* 8bit out / 10bit in (max 5bit value) */ = 2,
    const int i_time /* millis */ = 2000,
    const int d_time /* millis */ = 30,
    const int threshold /* 10bit in */ = 4,
    const int overshoot /* 10bit in */ = 8
    ) : p(p), i_time(i_time), d_time(d_time), threshold(threshold), overshoot(overshoot)
    {}


  /* Update PID controller, given current position (10bit), current target (10bit)
  and time elapsed since last call (milliseconds).
  */
  void update(int current, int target, int elapsed){

    int error = target - current;

    // Zero out error if within the threshold. Note that diff still works to stop momentum.
    // Same if we overshot (error is opposite sign of control) and we're within the limit.
    if ((abs(error) < threshold) or ((abs(error) < overshoot) and (error * direction < 0))) {
      error = 0;
      integral_control = 0;
    }

    // Multiply by the time constant before dividing by the current elapsed time to avoid truncating.
    // Avoid jerks from target changing.
    // Check elapsed before dividing by 0 and ignore the diff term if so.
    const int diff = elapsed ? (error-last_error - (target - last_target)) * d_time / elapsed : 0;
    last_error = error;
    last_target = target;

    // Keep a running integral of the error. Use floating point to have enough precision.
    integral_control = clamp(integral_control + static_cast<float>(p) * error * elapsed / i_time, -512.f, +512.f);

    //  Compute the control due to proportional and derivative terms.
    const int pd_control = p * (error + diff);

    // Discard the integral if we are at maximum control without it.
    if (abs(pd_control) >= 255) integral_control = 0;

    // Clamp output to -255, +255. Basically 8bit value and direction as
    // the two are controlled separately. Note that we need to cast the integral back to int.
    control = clamp(pd_control + static_cast<int>(integral_control), -255, +255);

    // Update direction as the last sign of control, maintaining if control is 0.
    direction = control > 0 ? +1 : control < 0 ? -1 : direction;
  }
};
