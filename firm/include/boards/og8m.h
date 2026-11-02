#pragma once
#include "common.h"
#include <hal/gpio_types.h>
#include <driver/uart.h>

// Original ESPer board with 8MB PSRAM / 8MB ROM
// Uses a shared i2c bus for the keypad and IDE 

#define BCONF_HAS_I2C
#define BCONF_I2C_SDA                           GPIO_NUM_32
#define BCONF_I2C_SCL                           GPIO_NUM_33
#define BCONF_I2C_FRQ                           400000

#define BCONF_DISPLAY_TYPE                      DISP_TYPE_FUTABA_GP1232A
#define BCONF_DISPLAY_UART_NUM                  UART_NUM_1
#define BCONF_DISPLAY_RX_PIN                    GPIO_NUM_23
#define BCONF_DISPLAY_TX_PIN                    GPIO_NUM_22
#define BCONF_DISPLAY_BAUD                      115200

#define BCONF_RC_TYPE                           RC_TYPE_SONY_PS2_REMOTE

#define BCONF_IDE_TYPE                          IDEBUS_TYPE_ATAPIDUINO_ISH_I2C

#define BCONF_AUDIOROUTER_TYPE                  AUDIOROUTER_TYPE_WOLFSON_WM8805
#define BCONF_DAC_UNMUTE_PIN                    GPIO_NUM_18
#define BCONF_DAC_DEMPH_PIN                     GPIO_NUM_26
#define BCONF_DAC_MCK_PIN                       GPIO_NUM_3
#define BCONF_DAC_BCK_PIN                       GPIO_NUM_5
#define BCONF_DAC_LRCK_PIN                      GPIO_NUM_2
#define BCONF_DAC_DATA_PIN                      GPIO_NUM_27

#define BCONF_BYPASS_RELAY_PIN                  GPIO_NUM_19
#define BCONF_INFRARED_PIN                      GPIO_NUM_34
