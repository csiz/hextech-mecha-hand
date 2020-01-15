/* 24 Channel DC motor driver

PCB Features
------------

* Power control via the power button and high current mosfet.
* Voltage monitoring by a voltage divider 8.2k / (8.2k + 2k) ~= 0.804.
* Current monitoring via [ACS712-20A-T](https://www.sparkfun.com/datasheets/BreakoutBoards/0712.pdf) and 2k/2k == 0.5 voltage divider.
* 2 buttons, 1 joystick, and [SSD1306](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf) I2C OLED display as user interface.
* External pins for serial, secondary I2C interface and GPIO 0 (bootloader pin).
* 24 [DRV8870DDAR](https://www.ti.com/lit/ds/symlink/drv8870.pdf) motor drivers.
* Motor drivers controlled by [TLC59116IPWR](https://www.ti.com/lit/ds/symlink/tlc59116.pdf) PWM LED drivers @ 97kHz; I2C interface.
* Motor current measurement via 0.1ohm resistor and [AD7689BCPZRL7](https://www.analog.com/media/en/technical-documentation/data-sheets/AD7682_7689.pdf)
ADC; serial interface.
* Position measurement via [ADC128S102](https://www.ti.com/lit/ds/symlink/adc128s102.pdf); serial interface.
* 12 strain gauge measurements via [ADS1115](https://www.ti.com/lit/ds/symlink/ads1115.pdf) with AIN3 set to 1/2 voltage; I2C interface.
* Gyro and acceleration measurements via [MPU-6050](https://www.invensense.com/wp-content/uploads/2015/02/MPU-6000-Datasheet1.pdf); I2C interface.


Code Outline
------------

* main.cpp: Top level setup and loop. Manage all interactions between modules below.
* pins.hpp: Defines physical connections to the ESP32 chip, and I2C addresses for slave ICs.
* power.hpp: Power control and measurements.
* ui.hpp: User interaction via buttons and the OLED screen.
* drivers.hpp: Control driving power for each motor.
* positions.hpp: Read positions corresponding to each motor.
* strains.hpp: Read strain gauges.
* currents.hpp: Read per motor current use.
* pid.hpp: PID control logic.

*/


#include "i2c.hpp"
#include "ui.hpp"
#include "drivers.hpp"
#include "power.hpp"
#include "spi.hpp"
#include "currents.hpp"
#include "positions.hpp"

void setup(){
  power::setup();
  i2c::setup();
  ui::setup();
  drivers::setup();
  spi::setup();
  currents::setup();
  positions::setup();
}

void loop(){
  // TODO: remove
  drivers::power[0] = 1.0;
  drivers::power[1] = -1.0;
  drivers::power[2] = 0.5;
  drivers::power[3] = -0.5;

  drivers::update();
  currents::update();

  // TODO: remove
  ui::display.clearDisplay();
  ui::display.setTextSize(1);
  ui::display.setTextColor(SSD1306_WHITE);
  ui::display.setCursor(0, 0);
  ui::display.println(currents::current[0], 5);
  ui::display.println(currents::current[1], 5);
  ui::display.println(currents::current[2], 5);
  ui::display.println(currents::current[3], 5);
  ui::display.display();

  delay(500);

  power::shutdown_on_long_press();
}