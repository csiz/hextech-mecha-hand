#pragma once
// TODO: do i need constant pressure to counter wrist gravity?

class Pressure {
  int last_pid_control = 0;
  int last_position = 0;
  int pressure_offset = 0;
  int control = 0;

  void update(int pid_control, int position, int elapsed) {

    int diff_position = position - last_position;
    // If we moved in the opposite direction of control (negative of
    // the multiple), then we need to increase our pressure.
    if (last_pid_control * diff_position < 0) {
      pressure_offset = (last_pid_control * 8 + pressure_offset * 2) / 10;
    }
    last_pid_control = pid_control;

    int control = pid_control + pressure_offset;

  }
}
