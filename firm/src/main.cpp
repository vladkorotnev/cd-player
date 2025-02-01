#include "Arduino.h"
#include <esper-core/keypad.h>
#include <esper-cdp/atapi.h>
#include <esper-cdp/metadata.h>
#include <WiFi.h>

Core::ThreadSafeI2C * i2c;
Platform::Keypad * keypad;
Platform::IDEBus * ide;
ATAPI::Device * cdrom;

// Arduino Setup
void setup(void) {  
  // Open Serial 
  Serial.begin(115200);
  while(!Serial);
  
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

  cdrom->reset();
  cdrom->wait_ready();
}


// Arduino loop - copy sound to out
long lastMs = 0;
uint8_t kp_sts = 0;
void loop() {
  uint8_t kp_new_sts = 0;
  if(keypad->read(&kp_new_sts)) {
    if(kp_new_sts != kp_sts) {
      ESP_LOGI("Test", "Key change: 0x%02x to 0x%02x", kp_sts, kp_new_sts);
      kp_sts = kp_new_sts;
      
      if(kp_sts == 1) {
        ATAPI::MediaTypeCode mtc = cdrom->check_media();
        cdrom->eject(mtc != ATAPI::MediaTypeCode::MTC_DOOR_OPEN);
      }
      else if(kp_sts == 2) {
        if(cdrom->query_state()->is_door_open) {
          cdrom->eject(false);
        }
        cdrom->wait_ready();
        ATAPI::MediaTypeCode mtc = ATAPI::MediaTypeCode::MTC_NO_DISC;
        do {
          mtc = cdrom->check_media();
        } while(mtc == ATAPI::MediaTypeCode::MTC_DOOR_OPEN || mtc == ATAPI::MediaTypeCode::MTC_DOOR_CLOSED_UNKNOWN);

        if(ATAPI::MediaTypeCodeIsAudioCD(mtc)) {
          auto toc = cdrom->read_toc();
          if(toc.tracks.size() > 0) {
            cdrom->play( toc.tracks.front().position, toc.leadOut );
          }
        }
      }
      else if(kp_sts == 4) { 
        bool resume = cdrom->query_position()->state == ATAPI::AudioStatus::PlayState::Paused;
        cdrom->pause(!resume);
      }
      else if(kp_sts == 8) { 
        cdrom->stop();
      }
      else if(kp_sts == 16) { 
        auto mech_sts = cdrom->query_state();
        int disc = 0;
        for(int i = (mech_sts->current_disc + 1) % mech_sts->slot_count; i < mech_sts->slot_count; i++) {
          if(mech_sts->changer_slots[i].disc_in) {
            disc = i;
            break;
          }
        }
        if(disc == 0 && !mech_sts->changer_slots[disc].disc_in) {
          ESP_LOGE("TEST", "empty changer");
        }
        else {
          cdrom->load_unload(disc);
          cdrom->wait_ready();

          ATAPI::MediaTypeCode mtc = ATAPI::MediaTypeCode::MTC_NO_DISC;
          do {
            mtc = cdrom->check_media();
          } while(mtc == ATAPI::MediaTypeCode::MTC_DOOR_OPEN || mtc == ATAPI::MediaTypeCode::MTC_DOOR_CLOSED_UNKNOWN);

          if(ATAPI::MediaTypeCodeIsAudioCD(mtc)) {
            auto toc = cdrom->read_toc();
            if(toc.tracks.size() > 0) {
              cdrom->play( toc.tracks.front().position, toc.leadOut );
            }
          }
        }
      }
      else if(kp_sts == 32) {
        ATAPI::MediaTypeCode mtc = ATAPI::MediaTypeCode::MTC_NO_DISC;
        do {
          mtc = cdrom->check_media();
        } while(mtc == ATAPI::MediaTypeCode::MTC_DOOR_OPEN || mtc == ATAPI::MediaTypeCode::MTC_DOOR_CLOSED_UNKNOWN);

        if(ATAPI::MediaTypeCodeIsAudioCD(mtc)) {
          auto toc = cdrom->read_toc();
          
          auto meta = CD::CachingMetadataAggregateProvider();
          meta.providers.push_back(new CD::MusicBrainzMetadataProvider());
          meta.providers.push_back(new CD::CDDBMetadataProvider("gnudb.gnudb.org", "asdfasdf@example-esp32.com"));

          auto album = CD::Album(toc);

          meta.fetch_album(album);

          ESP_LOGI("TEST", "Album: %s by %s", album.title.c_str(), album.artist.c_str());
          for(int i = 0; i < album.tracks.size(); i++) {
            ESP_LOGI("TEST", "  [%02i] %s - %s", i, album.tracks[i].artist.c_str(), album.tracks[i].title.c_str());
          }
        }
      }
      else {
        cdrom->query_position();
      }
    }
  }
}