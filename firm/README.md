# ESPer-CDP Firmware

This is the ESP32 firmware for the ESPer-CDP CD player board.

## System Requirements

* ESPer-CDP platform dev board with IDE bus
* ESP32-WROVER newer than rev1, with 8MB Flash and 4MB PSRAM (HIMEM is not used currently)
* ESPer Keypad
* Futaba GP1232A02 VFD display (take it out of a broken arcade cabinet... or write a driver for something less obscure, but preferably also VFD :-)
* optional: IR remote receiver (not yet implemented)
* optional: Nixierator module for track # display (not yet implemented)

### Tested CD drives

* **Recommended: NEC ND-3500A**: just works! Wish every one of the others was like this.
* *Matsushita SR-8171 (slimline)*: mostly works. TOC read after disc change is unstable (goes through on 3rd retry, timing/busyflag issue?) Needs a standard JAE 50pin to IDE 40pin adapter. Does not require 12V supply. Analog output only.
* *NEC CDR-1400C*: kinda works, very unstable state management, sometimes freezes the whole system, doesn't report media type codes properly
* *TEAC DV-W58G*: works, janky ffwd/rewind (using software timer because the drive doesn't do SCAN commands). Most of the initial development was done with this drive.
* *TEAC CD-C68E*: works including changer, sometimes TOC reads are broken, workaround is in place but still. *No SPDIF output, so you have to resort to analog.*

## Capabilities

* Read CD audio TOC from an IDE drive
* Ask the drive to play/pause and skip tracks (sequential playback only for now)
* Read CD TEXT if the drive supports it (only English/UTF8 block is supported)
* Display CD metadata found online through [Musicbrainz](https://musicbrainz.org/), or [GnuDB](https://gnudb.org/) — using real [libcddb](lib/cddb) ported to ESP32, lol
* Cache CD metadata in Flash (not compressed for now and no cleanup when storage runs out yet)
* Display timed lyrics for the songs using the awesome [LRCLib](https://lrclib.net/)
* ... more to come! I need time :P

## Further goals

### Software only

* Internet radio
* Bluetooth receiver
* Stretch: Bluetooth transmitter from radio/CD
* Settings UI on screen
* Connect to WiFi by QR (**upd:** DPP QR doesn't fit in 29x29 or 31x31, must use captive portal or OSD altogether)
* LastFM scrobble with Oauth by QR
* More lyric sources (similar to those supported by one popular foobar plugin)
* IR Remote support
* Figure out how to disable drive's sleep mode? For now I almost exploded the disc twice but never got the power condition mode select call to work

### Rev 2 of hardware if it ever comes

* Direct read of CDA from the drive instead of using SPDIF
* Reading of UDF/ISO9660 drives with AAC/MP3/MOD/XM/IT/other files on them
* FM radio with RDS
* AM radio (SW, MW, LW)
* maybe: other display types

## Architecture

* GUI is rendered using a custom [ESPer GUI (えぐい)](lib/espergui/) library
* All platform related driver and stuff is kept in the [ESPer Core](lib/espercore/) library
* ATAPI related drivers and CD player command/state machine model is stored in the [ESPer CDP](lib/espercdp/) library
* The actual arduino sketch only handles the UI and HID, most of the actual logic is kept in the libraries
* All fonts are loaded into PSRAM from LittleFS where they are stored using the [MoFo Format](https://github.com/vladkorotnev/plasma-clock/blob/develop/src/graphics/font.cpp#L10) from [PIS-OS](https://github.com/vladkorotnev/plasma-clock/)

## Credits

* OBIWAN for help with the GP1232A02 protocol
* Fonts `keyrus08x08`, `keyrus08x16` yanked from the MS-DOS Russian Locale package "keyrus" by Dmitriy Gurtyak &copy; 1989-1994
* Font `xnu` yanked from the [XNU/Darwin kernel](https://github.com/apple/darwin-xnu/blob/main/osfmk/console/iso_font.c), &copy; 2000 Apple Computer, Inc., &copy; 2000 Ka-Ping Yee
* [Misaki Mincho](https://littlelimit.net/misaki.htm) 8x8 kanji font, [k8x12](https://littlelimit.net/k12x8.htm) kanji font by [門真 なむ (LittleLimit)](https://littlelimit.net/index.html)

## Tech Notes

* On design of CDDB metadata in-flash cache system: https://github.com/littlefs-project/littlefs/issues/1067
* Various issues around Arduino-Audio-Tools: https://github.com/pschatzmann/arduino-audio-tools/discussions/1930

----

&copy; Genjitsu Gadget Lab, 2025 (yes, a CD player project in 2025)