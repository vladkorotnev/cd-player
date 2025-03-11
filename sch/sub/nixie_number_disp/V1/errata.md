# Nixierator v1 errata

- 74HC42 has inverted outputs, but SN75468 inverts them again, so all digits except the active are on: need to use CD4028 instead
- Indicator tubes are in the  wrong location physically (higher bits should be on the left)
- DATA_IN connector is in the way of the screw
- Base address is 0x38, not 0x64