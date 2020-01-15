#pragma once

#include "pins.hpp"

#include "SPI.h"

namespace spi {
  SPIClass vspi = {VSPI};

  inline void setup() {
    // Initialize vspi with the pins on the PCB, leave SS -1 since we're the master.
    vspi.begin(VSPI_CLK, VSPI_MISO, VSPI_MOSI, -1);
  }
}