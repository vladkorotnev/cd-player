# ESPer CDP

## Project goals

### Main goals 

* ESP-32 based CD player platform
* Nice graphical display interface (using e.g. VFD display)
* CDDB connectivity for metadata display
* ATAPI CD player support
* ATAPI CD Changer support
* IR Remote control and physical button control
* Play on power on (for audio timer)
* Straight, Shuffle, Program playback
* Internet radio playback

### Stretch goals

* Web remote control API
* Bluetooth audio transmission
* HTTP audio transmission
* HTTP firmware updates

### Out of scope

* Data CD playback: not doable without a high speed bus
* SATA compatibility

## HW memo

* I2C Level shifter: LSF0102
* SPDIF to I2S: WM8805
* DAC: PCM5102
* IO Expander PCA9555PW