#include "Arduino.h"
#include <esper-core/keypad.h>
#include <esper-cdp/atapi.h>

Core::ThreadSafeI2C * i2c;
Platform::Keypad * keypad;
Platform::IDE * ide;
ATAPI::Device * cdrom;

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
  cdrom = new ATAPI::Device(ide);

  cdrom->reset();
  cdrom->wait_ready();
}

// Arduino loop - copy sound to out
uint8_t kp_sts = 0;
void loop() {
  uint8_t kp_new_sts = 0;
  if(keypad->read(&kp_new_sts)) {
    if(kp_new_sts != kp_sts) {
      ESP_LOGI("Test", "Key change: 0x%02x to 0x%02x", kp_sts, kp_new_sts);
      kp_sts = kp_new_sts;
      cdrom->eject();
    }
  }

  delay(2000);

  cdrom->query_state();
}