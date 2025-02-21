# ESPer CDP

## Index

* [Firmware](firm)
* [Schematics](sch)
* [Test CD](test-suite)
* [Hoard of Docs](reference)

## Project goals

### Main goals 

* [x] ESP-32 based CD player platform
* [x] Nice graphical display interface (using e.g. VFD display)
* [x] CDDB connectivity for metadata display
* [x] ATAPI CD player support
* [x] ATAPI CD Changer support
* [ ] IR Remote control and physical button control
* [ ] Straight, Shuffle, Program playback
* [ ] Internet radio playback

### Stretch goals

* [ ] Web remote control API
* [ ] Bluetooth audio transmission
* [ ] HTTP audio transmission
* [ ] HTTP firmware updates

### Out of scope

* Data CD playback: not doable without a high speed bus
* SATA compatibility

For SATA compatibility it may be possible to use an IDE to SATA adapter, so if rev.2 is ever to happen, it needs to have a high speed bus for streaming PCM data. Perhaps MCP23S17 as it goes to 10 MHz?

