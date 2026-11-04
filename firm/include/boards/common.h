#pragma once

// Display types
#define DISP_TYPE_NULL                  0x00000000
#define DISP_TYPE_FUTABA_GP1232A        0x00000001
#define DISP_TYPE_ST7920_SPI            0x00000002

// Remote types
#define RC_TYPE_SONY_PS2_REMOTE         0x00000001

// IDE Bus Controller types
#define IDEBUS_TYPE_ATAPIDUINO_ISH_I2C  0x00000001
#define IDEBUS_TYPE_CURSED_SPI          0x00000002

// Audio Matrix Controller Types
#define AUDIOROUTER_TYPE_NONE           0x00000000
#define AUDIOROUTER_TYPE_WOLFSON_WM8805 0x00000001

#define IDEBUS_RELIES_ON_DRIVE_TO_PLAY(b) (b == IDEBUS_TYPE_ATAPIDUINO_ISH_I2C)
