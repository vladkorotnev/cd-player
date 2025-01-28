#include "Arduino.h"
#include <esper-core/keypad.h>
#include <esper-core/ide.h>

Core::ThreadSafeI2C * i2c;
Platform::Keypad * keypad;
Platform::IDE * ide;

// Arduino Setup
void setup(void) {  
  // Open Serial 
  Serial.begin(115200);
  while(!Serial);

  Wire.begin(32, 33, 100000);
  i2c = new Core::ThreadSafeI2C(&Wire);
  i2c->log_all_devices();

  keypad = new Platform::Keypad(i2c);
  ide = new Platform::IDE(i2c);

  ide->reset();
  delay(5000);
  auto rslt = ide->read(0xF4); // Cylinder Low
  if(rslt.low == 0x14) {
    rslt = ide->read(0xF5); // Cylinder Hi
    if(rslt.low == 0xEB) {
      Serial.println("Found ATAPI device!!");
    }
  }
}

// Arduino loop - copy sound to out
uint8_t kp_sts = 0;
void loop() {
  uint8_t kp_new_sts = 0;
  if(keypad->read(&kp_new_sts)) {
    if(kp_new_sts != kp_sts) {
      ESP_LOGI("Test", "Key change: 0x%02x to 0x%02x", kp_sts, kp_new_sts);
      kp_sts = kp_new_sts;
    }
  }
}