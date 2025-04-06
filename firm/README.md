# ESPer-CDP Firmware

This is the ESP32 firmware for the ESPer-CDP CD player board.

## System Requirements

* ESPer-CDP platform dev board with IDE bus
* ESP32-WROVER **with ESP32 REV 3 or newer**, with 8MB QIO Flash and 4MB QIO PSRAM (HIMEM is not used currently)
* ESPer Keypad
* Futaba GP1232A02 VFD display (take it out of a broken arcade cabinet... or write a driver for something less obscure, but preferably also VFD :-)
* IR remote receiver + PS2 DVD remote
* optional: Nixierator module for track # display (not yet implemented)

### Tested CD drives

#### The Good

* **Recommended: NEC ND-3500A**: just works in every aspect, and even has CD TEXT! Wish every one of the others was like this.
* *Philips/Lite-On DH-20A4P* aka *Buffalo DVSM-XE1219FB*: Load a bit on the slow side, noisy mech! Janky ffwd/rewind (using software timer because the drive doesn't do SCAN commands).
* *TEAC DV-W516GDM*: Smooth operation, has CD TEXT, janky ffwd/rewind
* *LITE-ON LTN-486*: Noisy, cannot set speed to 600, janky ffwd/rewind, otherwise works, has CD TEXT
* *LITE-ON LTR-48246S*: Kinda noisy, works mostly, janky ffwd/rewind, has CD TEXT.
* *JLMS XJ-HD166S* (JVC/Liteon): Kinda noisy, works mostly, janky ffwd/rewind. Properly reports audio status when using the (hidden) play button! Loses sync quite often on some CDs, test unit defective?
* *MATSHITADVD-RAM SW-9583S*: Works mostly. No SPDIF output even though the jack is there?? Do we need to alter the Mode Select page?
* *TEAC DV-W58G*: works and reads CD TEXT, janky ffwd/rewind (same as DH-20A4P). Most of the initial development was done with this drive. SPDIF output noisy during initial spin-up and pause release, or my unit just too old/faulty?
* *Matsushita SR-8171 (slimline)*: mostly works. TOC read after disc change is unstable (goes through on 3rd retry, timing/busyflag issue?) Needs a standard JAE 50pin to IDE 40pin adapter. Does not require 12V supply. Analog output only.

#### The Bad

* *Matsushita/Panasonic SW-9590C*: never initializes, never sets DRQ, faulty unit?

#### The Ugly

* *NEC CDR-1400C*: kinda works, very unstable state management, sometimes freezes the whole system, doesn't report media type codes properly.
* *TEAC CD-C68E*: works including changer, sometimes TOC reads are broken, workaround is in place but still. *No SPDIF output, so you have to resort to analog.* As of V1.0a it's become significantly more glitchy and I have no idea why. TBD in a future release...
* *ALi DVD Ver0.96A_0104*: found inside a MUSTEK DVD player. Plays it seems but no SPDIF. Cannot stop without being paused first. Overall weird behavior.

## Capabilities

* Read CD audio TOC from an IDE drive
* Ask the drive to play/pause and skip tracks (sequential playback only for now)
* Read CD TEXT if the drive supports it (only English/UTF8 block is supported)
* Display CD metadata found online through [Musicbrainz](https://musicbrainz.org/), or [GnuDB](https://gnudb.org/) — using real [libcddb](lib/cddb) ported to ESP32, lol
* Cache CD metadata in Flash (no cleanup when storage runs out yet... but with compression you can fit an average CD collection in there with ease)
* Display timed lyrics for the songs using the awesome [LRCLib](https://lrclib.net/)
* Receive internet radio stations (as long as no crazy bitrate) in MP3 and AAC formats with up to 6 station presets
* Receive sound over Bluetooth (SBC only) and control the remote device
* Settings UI on screen with multiple language support: English, Japanese, Russian, feel free to add your own?
* IR Remote (PS2-DVD remote)
* Scrobbling to Last.FM
* ... more to come! I need time :P

## Further goals

### Software only

* HTTP updates with CI/CD
* Hopefully increase size of cache partition
* Stretch: Bluetooth transmitter from radio/CD
* Maybe some day: Web interface
* Dependency, so also maybe some day: Connect to WiFi by QR (**upd:** DPP QR doesn't fit in 29x29 or 31x31, must use captive portal or OSD altogether)
* More lyric sources (similar to those supported by one popular foobar plugin)
* Figure out how to disable drive's sleep mode? For now I almost exploded the disc twice from hanging the CPU in the drive, but never got the power condition mode select call to work

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
* All fonts are loaded into PSRAM from LittleFS where they are stored using the [MoFo Format](https://github.com/vladkorotnev/plasma-clock/blob/develop/src/graphics/font.cpp#L10) from [PIS-OS](https://github.com/vladkorotnev/plasma-clock/)
* ESP-IDF is used to customize the building of Arduino libraries because otherwise I run out of IRAM area when using A2DP + WiFi in the same program

## Credits

* Thanks to OBIWAN for help with the GP1232A02 protocol
* Thanks to [Phil Schatzmann](https://www.pschatzmann.ch/) for [Arduino Audio Tools](https://github.com/pschatzmann/arduino-audio-tools) and helping troubleshoot it's usage in the project
* Fonts `keyrus08x08`, `keyrus08x16` yanked from the MS-DOS Russian Locale package "keyrus" by Dmitriy Gurtyak &copy; 1989-1994
* Font `xnu` yanked from the [XNU/Darwin kernel](https://github.com/apple/darwin-xnu/blob/main/osfmk/console/iso_font.c), &copy; 2000 Apple Computer, Inc., &copy; 2000 Ka-Ping Yee
* [Misaki Mincho](https://littlelimit.net/misaki.htm) 8x8 kanji font, [k8x12](https://littlelimit.net/k12x8.htm) kanji font by [門真 なむ (LittleLimit)](https://littlelimit.net/index.html)
* Inspired by [ATAPIDUINO](http://singlevalve.web.fc2.com/Atapiduino/atapiduino.htm) and [daniel1111's ArduinoCdPlayer](https://github.com/daniel1111/ArduinoCdPlayer)

## Tech Notes

* On design of CDDB metadata in-flash cache system: https://github.com/littlefs-project/littlefs/issues/1067
* Various issues around Arduino-Audio-Tools: https://github.com/pschatzmann/arduino-audio-tools/discussions/1930

----

&copy; Genjitsu Gadget Lab, 2025 (yes, a CD player project in 2025)