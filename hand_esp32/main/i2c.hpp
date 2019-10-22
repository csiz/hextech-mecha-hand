#pragma once

#include "Wire.h"

int nr_wire_errors = 0;

inline int16_t read_int16() {
  return static_cast<int16_t>(Wire.read()) << 8 | Wire.read();
}

inline size_t write_int16(const int16_t value){
  return Wire.write(value >> 8) + Wire.write(value & 0xFF);
}

template<typename Reg>
inline bool read_int16_from(const byte address, const Reg reg, int & value){
  Wire.beginTransmission(address);
  Wire.write(static_cast<byte>(reg));
  if(Wire.endTransmission(false)){
    nr_wire_errors += 1;
    return true;
  }
  delayMicroseconds(10); // wait for arduino to process.
  if (Wire.requestFrom(address, 2u) != 2u) {
    nr_wire_errors += 1;
    return true;
  }

  value = read_int16();
  return false;
}

template<typename Reg>
inline bool write_int16_to(const byte address, const Reg reg, const int value){
  Wire.beginTransmission(address);
  Wire.write(static_cast<byte>(reg));
  write_int16(value);
  if(Wire.endTransmission()) {
    nr_wire_errors += 1;
    return true;
  }
  return false;
}

template<typename Reg>
inline bool read_from(const byte address, const Reg reg, byte & value) {
  Wire.beginTransmission(address);
  Wire.write(static_cast<byte>(reg));
  if(Wire.endTransmission(false)){
    nr_wire_errors += 1;
    return true;
  }
  delayMicroseconds(10); // wait for arduino to process.
  if (Wire.requestFrom(address, 1u) != 1u) {
    nr_wire_errors += 1;
    return true;
  }

  value = Wire.read();
  return false;
}

template<typename Reg>
inline bool write_to(const byte address, const Reg reg, const byte value){
  Wire.beginTransmission(address);
  Wire.write(static_cast<byte>(reg));
  Wire.write(value);
  if(Wire.endTransmission()) {
    nr_wire_errors += 1;
    return true;
  }
  return false;
}