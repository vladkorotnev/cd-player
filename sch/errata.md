# ESPer CDP V1.0 PCB errata

- Murata MPD7K020S DC-DC cannot work from 12V input. Temp solution: replace internal resistor of the module on pin 1 of TPS chip from 510k to around 200k (stacking a second 510k 0603 resistor seems to work). Stability of such hack is subject to debate.
- `E (133) I2S: i2s_check_set_mclk(253): ESP32 only support to set GPIO0/GPIO1/GPIO3 as mclk signal, error GPIO number:4`: well that was a royal one. Since RxD is not used outside of programming anyway let's stick with GPIO3.