#pragma once

#include "ESP.h"

typedef float  float32_t;
typedef double float64_t;

inline void set_uint32(uint8_t * data, uint32_t value) {
  data[0] = (value >> 24) & 0xFF;
  data[1] = (value >> 16) & 0xFF;
  data[2] = (value >> 8) & 0xFF;
  data[3] = value & 0xFF;
}

inline void set_float32(uint8_t * data, float32_t value) {
  set_uint32(data, reinterpret_cast<uint32_t &>(value));
}

inline void set_int32(uint8_t * data, int32_t value) {
  set_uint32(data, reinterpret_cast<uint32_t &>(value));
}
