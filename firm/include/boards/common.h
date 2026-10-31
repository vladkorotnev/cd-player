#pragma once

// Display types
#define DISP_TYPE_FUTABA_GP1232A        0x00000001

// Remote types
#define RC_TYPE_SONY_PS2_REMOTE         0x00000001

// IDE Bus Controller types
#define IDEBUS_TYPE_ATAPIDUINO_ISH_I2C  0x00000001

#define IDEBUS_RELIES_ON_DRIVE_TO_PLAY(b) (b == IDEBUS_TYPE_ATAPIDUINO_ISH_I2C)
