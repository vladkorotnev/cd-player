# ESPer CDP V1.0 PCB errata

- Murata MPD7K020S DC-DC cannot work from 12V input. Temp solution: replace internal resistor of the module on pin 1 of TPS chip from 510k to around 200k (stacking a second 510k 0603 resistor seems to work). Stability of such hack is subject to debate.
- `E (133) I2S: i2s_check_set_mclk(253): ESP32 only support to set GPIO0/GPIO1/GPIO3 as mclk signal, error GPIO number:4`: well that was a royal one. Since RxD is not used outside of programming anyway let's stick with GPIO3.
- Address of IO expanders is 0x20 and 0x22, not 0x02 and 0x43
- JTAG names are wrong on sch and silk, refer the table in [article](https://neocode.jp/2020/12/09/esp32debugger/) for correct wiring
- Use something like [DIX9211](https://www.ti.com/lit/ds/symlink/dix9211.pdf) instead of the EOL WM8805
- Also some SPDIF/TOSLINK IO would have been nice, since we have the inputs on board anyway, and toslink is basically SPDIF but through an LED
- Using a straight through JST 7pin cable explodes a transistor on the VFD display. The cables arrived from taobao are reversed!! Same issue with the keypad — solving by mounting keypad side connector upside down.
- 75R across SPDIF In from CD is not needed, because it's TTL level?