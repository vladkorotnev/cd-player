#pragma once
#include "common.h"
#include <hal/gpio_types.h>
#include <driver/uart.h>

// Newer single-board implementation
// Goal is to release at M3 in 2027 and maybe somewhere else

#warning "!!! This board config is not yet stable !!!"

#define BCONF_DISPLAY_TYPE                      DISP_TYPE_NULL

#define BCONF_RC_TYPE                           RC_TYPE_SONY_PS2_REMOTE
#define BCONF_INFRARED_PIN                      GPIO_NUM_34

#define BCONF_IDE_TYPE                          IDEBUS_TYPE_CURSED_SPI
#define BCONF_IDE_SPI_NUM                       VSPI
#define BCONF_IDE_SPI_BURST_PIN                 GPIO_NUM_23
#define BCONF_IDE_SPI_WRITE_PIN                 GPIO_NUM_19
#define BCONF_IDE_SPI_CS_PIN                    GPIO_NUM_22
#define BCONF_IDE_SPI_SCK_PIN                   GPIO_NUM_14
#define BCONF_IDE_SPI_MISO_PIN                  GPIO_NUM_13
#define BCONF_IDE_SPI_MOSI_PIN                  GPIO_NUM_15
#define BCONF_IDE_SPI_RST_PIN                   GPIO_NUM_NC

#define BCONF_AUDIOROUTER_TYPE                  AUDIOROUTER_TYPE_NONE
#define BCONF_DAC_UNMUTE_PIN                    GPIO_NUM_18
#define BCONF_DAC_DEMPH_PIN                     GPIO_NUM_26
#define BCONF_DAC_MCK_PIN                       GPIO_NUM_3
#define BCONF_DAC_BCK_PIN                       GPIO_NUM_5
#define BCONF_DAC_LRCK_PIN                      GPIO_NUM_2
#define BCONF_DAC_DATA_PIN                      GPIO_NUM_27
