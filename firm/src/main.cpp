#include "Arduino.h"
#include <esper-core/keypad.h>
#include <esper-cdp/player.h>
#include <WiFi.h>
#include <LittleFS.h>

Core::ThreadSafeI2C * i2c;
Platform::Keypad * keypad;
Platform::IDEBus * ide;
ATAPI::Device * cdrom;
CD::Player * player;
CD::CachingMetadataAggregateProvider * meta;

// Arduino Setup
void setup(void) {  
  // Open Serial 
  Serial.begin(115200);
  while(!Serial);
  
LittleFS.begin(true, "/littlefs");
  ESP_LOGI("FS", "Free FS size = %i", LittleFS.totalBytes() - LittleFS.usedBytes());

  WiFi.begin("Akasaka_SPK", "password");
  WiFi.waitForConnectResult();
  if(WiFi.isConnected()) Serial.println("WiFi ok");
  else Serial.println("!! WIFI FAIL !!");
  
  Wire.begin(32, 33, 100000);
  i2c = new Core::ThreadSafeI2C(&Wire);
  i2c->log_all_devices();

  keypad = new Platform::Keypad(i2c);
  ide = new Platform::IDEBus(i2c);
  cdrom = new ATAPI::Device(ide);

  meta = new CD::CachingMetadataAggregateProvider("/littlefs");
  meta->providers.push_back(new CD::CDTextMetadataProvider());
  meta->providers.push_back(new CD::MusicBrainzMetadataProvider());
  meta->providers.push_back(new CD::CDDBMetadataProvider("gnudb.gnudb.org", "asdfasdf@example-esp32.com"));

  player = new CD::Player(cdrom, meta);
    }



// Arduino loop - copy sound to out
long lastMs = 0;
uint8_t kp_sts = 0;

CD::Player::State last_sts;
MSF last_msf;
CD::Player::TrackNo last_trkno;

void loop() {
  CD::Player::State sts = player->get_status();
  if(sts != last_sts) {
    ESP_LOGI("Test", "State %s -> %s", CD::Player::PlayerStateString(last_sts), CD::Player::PlayerStateString(sts));
    last_sts = sts;
  }
  
  MSF msf = player->get_current_track_time();
  if(msf.M != last_msf.M || msf.S != last_msf.S) {
    ESP_LOGI("Test", "MSF: %02im%02is%02if", msf.M, msf.S, msf.F);
    last_msf = msf;
  }

  CD::Player::TrackNo trk = player->get_current_track_number();
  if(trk.track != last_trkno.track || trk.index != last_trkno.index) {
    ESP_LOGI("Test", "Trk#: %i.%i -> %i.%i", last_trkno.track, last_trkno.index, trk.track, trk.index);
    last_trkno = trk;
  }

  uint8_t kp_new_sts = 0;
  if(keypad->read(&kp_new_sts)) {
    if(kp_new_sts != kp_sts) {
      ESP_LOGI("Test", "Key change: 0x%02x to 0x%02x", kp_sts, kp_new_sts);
      kp_sts = kp_new_sts;
      
      if(kp_sts == 1) {
        player->do_command(CD::Player::Command::OPEN_CLOSE);
      }
      else if(kp_sts == 2) {
        player->do_command(CD::Player::Command::PLAY_PAUSE);
      }
      else if(kp_sts == 4) { 
        player->do_command(CD::Player::Command::PREV_TRACK);
      }
      else if(kp_sts == 8) { 
        player->do_command(CD::Player::Command::NEXT_TRACK);
      }
      else if(kp_sts == 16) { 
        player->do_command(CD::Player::Command::PREV_DISC);
      }
      else if(kp_sts == 32) {
        player->do_command(CD::Player::Command::NEXT_DISC);
      }
      else if(kp_sts == 64) {
        player->do_command(CD::Player::Command::SEEK_FF);
      }
      else if(kp_sts == 128) {
        player->do_command(CD::Player::Command::SEEK_REW);
      }
    }
  }
}