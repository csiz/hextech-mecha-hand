#pragma once

#include "driver/gpio.h"


// Power
#define POWER_CTRL GPIO_NUM_2
#define POWER_BTN GPIO_NUM_4
#define VOLTAGE_IN GPIO_NUM_36
#define CURRENT_IN GPIO_NUM_39

// UI Inputs
#define BTN0 GPIO_NUM_13
#define BTN1 GPIO_NUM_15
#define JBTN GPIO_NUM_32
#define J0 GPIO_NUM_34
#define J1 GPIO_NUM_32

#define LED0 GPIO_NUM_14

// External
#define P0 GPIO_NUM_0
#define SDA2 GPIO_NUM_17
#define SCL2 GPIO_NUM_16
// Defined in arduino header.
// #define RX GPIO_NUM_3
// #define TX GPIO_NUM_1

// IC interfaces
#define SDA1 GPIO_NUM_21
#define SCL1 GPIO_NUM_22
#define VSPI_MISO GPIO_NUM_19
#define VSPI_MOSI GPIO_NUM_23
#define VSPI_CLK GPIO_NUM_18
#define CURR0_CNV GPIO_NUM_5
#define CURR1_CNV GPIO_NUM_33
#define CURR2_CNV GPIO_NUM_12
#define POSITION0_CS GPIO_NUM_25
#define POSITION1_CS GPIO_NUM_26
#define POSITION2_CS GPIO_NUM_27

// I2C Addresses
#define OLED_ADDRESS 0b00111100
#define PWM_BASE_ADDRESS 0b01100000
#define STRAIN_BASE_ADDRESS 0b01001000
#define IMU_ADDRESS 0b01101000