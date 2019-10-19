#pragma once

#define PID6DRIVE_ADDRESS 0x60

enum class PID6DriveRegister : byte {
  /* Disable all controls. */
  DISABLE_ALL = 0x00,

  /* Define 6 macros for each prefix. */
  #define EACH(what, v) what##_0 = v, what##_1, what##_2, what##_3, what##_4, what##_5

  /* Get 10 bit input voltage for each potentiometer. */
  EACH(GET_INPUT, 0x01), // 2 byte response

  /* Set 10 bit target for each control. */
  EACH(SET_TARGET, 0x07), // 2 byte payload

  /* Get 10 bit target for each control. */
  EACH(GET_TARGET, 0x0D), // 2 byte response

  /* Enable each control. */
  EACH(ENABLE, 0x13), // 1 byte payload

  /* Invert direction of control. */
  EACH(INVERT, 0x19), // 1 byte payload

  /* Set the output index for the drive unit. */
  EACH(OUTPUT_IDX, 0x1F), // 1 byte payload.

  /* Set proportional constant per control. */
  EACH(SET_PID_P, 0x25), // 2 byte payload

  /* Set integral time per control (milliseconds). */
  EACH(SET_PID_I_TIME, 0x2B), // 2 byte payload

  /* Set the differential time per control (milliseconds). */
  EACH(SET_PID_D_TIME, 0x31), // 2 byte payload

  /* Set input-error threhold under which it's considered on-target. */
  EACH(SET_PID_THRESHOLD, 0x37), // 2 byte payload

  /* Set the overshoot threshold for each target.

  If the system has backlash, then if we overshoot the target and reverse the motor
  spins freely until the mechanism starts to reverse. Since this is imprecise, we
  allow more error in the direction we last moved, so we minimize the need correct.
  */
  EACH(SET_PID_OVERSHOOT, 0x3D), // 2 byte payload

  /* Get error of each input. */
  EACH(GET_ERROR, 0x43), // 1 byte payload

  /* Set input index for each drive unit. */
  EACH(INPUT_IDX, 0x49), // 1 byte payload

  /* Drive unit with power for a duration (milliseconds). */
  EACH(DRIVE, 0x4F), // 4 byte payload (2 byte power, 2 byte duration in ms)

  /* Set controls seeking state. */
  EACH(SEEKING, 0x55), // 1 byte payload



  #undef EACH

  /* Get whether any inputs are shorted. */
  GET_ERROR_STATE = 0x80, // 1 byte response

  /* Get all input values (2bytes * 6 10bit-inputs). */
  GET_ALL_INPUTS = 0x81, // 12 byte response

  /* Set all targets in order (2bytes * 6 10bit-targets). */
  SET_ALL_TARGETS = 0x82, // 12 byte payload

  /* Get exp-average loop interval (milliseconds). */
  GET_LOOP_INTERVAL = 0x83, // 2 byte payload

  /* Set the configured flag; can be used to detect a reset and reconfigure. */
  SET_CONFIGURED = 0x84, // 1 byte payload

  /* Get the configured flag; it is flase on reset. */
  GET_CONFIGURED = 0x85, // 1 byte payload

  /* Get and reset the number of i2c errors on the slave. */
  GET_RESET_I2C_ERRORS = 0x86, // 1 byte payload

  /* Drive all units for duration. */
  DRIVE_ALL = 0x87, // 24 ( == 4 * 6) byte payload (see DRIVE).

  /* Get all error values (1bytes * 6 errors). */
  GET_ALL_ERRORS = 0x88, // 6 byte response

  /* Register value when not in use. This is -1, the default Wire.read value. */
  _UNUSED = 0xFF,
};