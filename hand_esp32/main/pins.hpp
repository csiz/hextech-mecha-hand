#pragma once

#include "Esp.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

// Power
#define POWER_CTRL GPIO_NUM_2
#define POWER_BTN GPIO_NUM_4
#define VOLTAGE_IN GPIO_NUM_36
#define CURRENT_IN GPIO_NUM_39

// Finger-tip pressure sensors pins.
#define ADS0_ALERT GPIO_NUM_19
#define ADS0_ADDRESS (ADS_ADDRESS + 0b00)
#define ADS1_ALERT GPIO_NUM_18
#define ADS1_ADDRESS (ADS_ADDRESS + 0b01)

// Interface
#define BTN0 GPIO_NUM_0 // Note this is also the bootloader button.
#define BTN1 GPIO_NUM_15
#define ENC0A GPIO_NUM_25
#define ENC0B GPIO_NUM_14
#define ENC1A GPIO_NUM_26
#define ENC1B GPIO_NUM_5

// LED error for the motor inputs.
#define IN_ERROR GPIO_NUM_23

// LED error for the ADS fingertip inputs.
#define ADC_ERROR GPIO_NUM_27


// Onboard PID
#define IN0 GPIO_NUM_34
#define IN1 GPIO_NUM_35
#define DIR0 GPIO_NUM_12
#define DIR1 GPIO_NUM_13
#define DIR2 GPIO_NUM_32
#define DIR3 GPIO_NUM_33
#define PWM0 GPIO_NUM_17
#define PWM1 GPIO_NUM_16
// ESP32 PWM works on channels, so need to also define thouse.
#define PWM0_C LEDC_CHANNEL_0
#define PWM1_C LEDC_CHANNEL_1