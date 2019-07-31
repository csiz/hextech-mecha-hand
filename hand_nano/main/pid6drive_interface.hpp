#pragma once

enum class PID6Drive : int {
  DISABLE_ALL = 0x00,

#define EACH(what, v) what##_0 = v, what##_1, what##_2, what##_3, what##_4, what##_5

  EACH(GET_INPUT, 0x01), // 2 byte response
  EACH(SET_TARGET, 0x07), // 2 byte payload
  EACH(GET_TARGET, 0x0D), // 2 byte response
  EACH(ENABLE, 0x13), // 1 byte payload
  EACH(INVERT, 0x19), // 1 byte payload
  EACH(OUTPUT_IDX, 0x1F), // 1 byte payload
  EACH(SET_PID_P, 0x25), // 2 byte payload
  EACH(SET_PID_I_TIME, 0x2B), // 2 byte payload
  EACH(SET_PID_D_TIME, 0x31), // 2 byte payload
  EACH(SET_PID_THRESHOLD, 0x37), // 2 byte payload
  EACH(SET_PID_OVERSHOOT, 0x3D), // 2 byte payload

#undef EACH

  GET_ERROR_STATE = 0x80, // 1 byte response
  GET_ALL_INPUTS = 0x81, // 12 byte response
  SET_ALL_TARGETS = 0x82, // 12 byte payload


};