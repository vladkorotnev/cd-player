#include "Arduino.h"
#include "AudioTools.h"
#include "AudioTools/AudioLibs/AudioSourceLittleFS.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include <Wire.h>
#include <cddb/cddb.h>

const char *startFilePath="/";
const char* ext="mp3";
AudioSourceLittleFS source(startFilePath, ext);
AudioInfo info(44100, 2, 16);
I2SStream out; 
MP3DecoderHelix decoder;
AudioPlayer player(source, out, decoder);

bool needDemph = false;
void testi2c() {
  Wire.begin(32, 33, 100000);
  uint8_t error, address;
  int nDevices;
  nDevices = 0;
  for(address = 1; address < 127; address++ )
  {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0)
    {
      Serial.print("I2C device found at address 0x");
      if (address<16)
        Serial.print("0");
      Serial.print(address,HEX);
      Serial.println("  !");
      nDevices++;
    }
    else if (error==4)
    {
      Serial.print("Unknown error at address 0x");
      if (address<16)
        Serial.print("0");
      Serial.println(address,HEX);
    }
  }
  if (nDevices == 0)
    Serial.println("No I2C devices found\n");
  else
    Serial.println("done\n");

    Wire.beginTransmission(0x20);
    Wire.write(0x00); // Input Port 0
    Wire.endTransmission();
    Wire.requestFrom(0x20, 1);
    int portSts = Wire.read();
    Serial.print("Port 0 sts = ");
    Serial.println(portSts, BIN);
    // hold any key to have demph enabled
    if(portSts == 0b11111111) needDemph = false;
    else needDemph = true;
}

void noMute() {
  pinMode(18, OUTPUT);
  digitalWrite(18, 1);
}

void noDemph() {
  pinMode(26, OUTPUT);
  digitalWrite(26, needDemph); // De-emphasis control for 44.1kHz sampling rate: Off (Low) / On (High)
}

// Arduino Setup
void setup(void) {  
  libcddb_init();
  // Open Serial 
  Serial.begin(115200);
  while(!Serial);

  testi2c();
  delay(2000);

  AudioToolsLogger.begin(Serial, AudioToolsLogLevel::Error);

  // start I2S
  Serial.println("starting I2S...");
  auto config = out.defaultConfig(TX_MODE);
  config.copyFrom(info); 
  config.pin_bck = 5;
  config.pin_mck = 3;
  config.pin_ws = 2;
  config.pin_data = 27;

  out.begin(config);
  noDemph();
  noMute();

  player.begin();

  Serial.println("started...");
}

// Arduino loop - copy sound to out 
void loop() {
  player.copy();
}