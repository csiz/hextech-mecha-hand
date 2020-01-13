#pragma once

#include "pins.hpp"

#include "Wire.h"

#include <exception>

namespace i2c {

  void setup(){
    Wire.begin(SDA1, SCL1, 400000);
  }

  int nr_wire_errors = 0;

  inline int16_t read_int16() {
    return static_cast<int16_t>(Wire.read()) << 8 | Wire.read();
  }

  inline size_t write_int16(const int16_t value){
    return Wire.write(value >> 8) + Wire.write(value & 0xFF);
  }

  class I2CError : public std::exception {};

  template<typename Reg>
  inline int read_int16_from(const uint8_t  address, const Reg reg){
    Wire.beginTransmission(address);
    Wire.write(static_cast<uint8_t >(reg));
    if(Wire.endTransmission(false)){
      nr_wire_errors += 1;
      throw I2CError();
    }
    if (Wire.requestFrom(address, 2u) != 2u) {
      nr_wire_errors += 1;
      throw I2CError();
    }

    return read_int16();
  }

  template<typename Reg>
  inline void write_int16_to(const uint8_t  address, const Reg reg, const int value){
    Wire.beginTransmission(address);
    Wire.write(static_cast<uint8_t >(reg));
    write_int16(value);
    if(Wire.endTransmission()) {
      nr_wire_errors += 1;
      throw I2CError();
    }
  }

  template<typename Reg>
  inline uint8_t read_from(const uint8_t  address, const Reg reg) {
    Wire.beginTransmission(address);
    Wire.write(static_cast<uint8_t >(reg));
    if(Wire.endTransmission(false)){
      nr_wire_errors += 1;
      throw I2CError();
    }
    if (Wire.requestFrom(address, 1u) != 1u) {
      nr_wire_errors += 1;
      throw I2CError();
    }

    return Wire.read();
  }

  template<typename Reg>
  inline void write_to(const uint8_t  address, const Reg reg, const uint8_t  value){
    Wire.beginTransmission(address);
    Wire.write(static_cast<uint8_t >(reg));
    Wire.write(value);
    if(Wire.endTransmission()) {
      nr_wire_errors += 1;
      throw I2CError();
    }
  }

  // Can't return arrays...
  // template<size_t N, typename Reg>
  // inline uint8_t[N] read_bytes_from(const uint8_t  address, const Reg reg) {
  //   Wire.beginTransmission(address);
  //   Wire.write(static_cast<uint8_t >(reg));
  //   if(Wire.endTransmission(false)){
  //     nr_wire_errors += 1;
  //     throw I2CError();
  //   }
  //   if (Wire.requestFrom(address, N) != N) {
  //     nr_wire_errors += 1;
  //     throw I2CError();
  //   }

  //   uint8_t result[N];
  //   for(size_t i = 0; i < N; ++i) result[i] = Wire.read();
  //   return result;
  // }

  template<size_t N, typename Reg>
  inline void write_bytes_to(const uint8_t address, const Reg reg, const uint8_t data[N]){
    Wire.beginTransmission(address);
    Wire.write(static_cast<uint8_t >(reg));
    for (size_t i = 0; i < N; ++i) Wire.write(data[i]);
    if(Wire.endTransmission()) {
      nr_wire_errors += 1;
      throw I2CError();
    }
  }
}