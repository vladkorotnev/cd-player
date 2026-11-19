# ESPer-CDP Project Schematics Archive

## v1: original ESPer design

Folder: [./v1/]

Format: DipTrace (legacy).

Use with the `OG8M` firmware flavor.

Interfaces:

* ATAPI (slow, command-only)
* I2C expansion ("NIXIEOUT", deprecated/unused)
* 8 button inputs, active low
* RS232 to Futaba GP1232 display (160x32 pixels)
* Line out, 12V out

## v1.5: interface prototype

Folder: [./v1_5]

Partial prototype of a fast ATAPI interface using 2x MCP23S17, 74HC4040AP, 74HC32AP and 74HC123AP in DIP package, as well as AE-LLCNV-LVCH16T245 level shifter breakout board.

Use in conjunction with any ESP32 or the v1 board.

No pre-existing board config exists, create one with the following template:

```c
#define BCONF_IDE_TYPE                          IDEBUS_TYPE_CURSED_SPI
#define BCONF_IDE_SPI_NUM                       VSPI
#define BCONF_IDE_SPI_BURST_PIN                 ...
#define BCONF_IDE_SPI_WRITE_PIN                 ...
#define BCONF_IDE_SPI_CS_PIN                    ...
#define BCONF_IDE_SPI_SCK_PIN                   ...
#define BCONF_IDE_SPI_MISO_PIN                  ...
#define BCONF_IDE_SPI_MOSI_PIN                  ...
#define BCONF_IDE_SPI_RST_PIN                   ...
```

# v2

Work in progress
