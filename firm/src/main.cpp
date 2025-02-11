#include "Arduino.h"
#include "AudioTools.h"
#include "AudioTools/AudioLibs/AudioSourceLittleFS.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"

#include <UTF8toSJIS.h>
#include <esper-core/service.h>
#include <esper-core/keypad.h>
#include <esper-core/spdif.h>
#include <esper-cdp/player.h>
#include <esper-cdp/lyrics.h>
#include <WiFi.h>
#include <LittleFS.h>

static char LOG_TAG[] = "APL_MAIN";

Core::ThreadSafeI2C * i2c;
Platform::Keypad * keypad;
Platform::IDEBus * ide;
Platform::WM8805 * spdif;
ATAPI::Device * cdrom;
CD::Player * player;
CD::CachingMetadataAggregateProvider * meta;

HardwareSerial disp(1);

inline void clrscr() { disp.write("\x1B\x0C"); disp.write("\x1B\x30"); disp.write(0);disp.write(0);disp.write(0); }
void disp_write(const char * utf) {
  static UTF8toSJIS u8ts;
  uint8_t buf[128] = {0};
  uint16_t out_size = 0;
  u8ts.UTF8_to_SJIS_str_cnv("/Utf8Sjis.tbl", utf, buf, &out_size); //<- we won't use this once ESPerGUI is a thing
  if(out_size >= 128) return;
  disp.write(buf, out_size);
}
// cppcheck-suppress unusedFunction
void setup(void) { 
#ifdef BOARD_HAS_PSRAM
  heap_caps_malloc_extmem_enable(128);
#endif
  // Open Serial 
  Serial.begin(115200);
  while(!Serial);

  disp.begin(115200, SERIAL_8N1, 23, 22);
  disp.write("\x1B\x0B"); // reset
  disp.write("\x1B\x21\x01"); // power on
  disp.write("\x1b\x20\x02"); // 50% brighness
  disp.write("\x1b\x22"); disp.write(0); disp.write(0); // DSA = 0
  disp.write("\x1B\x32\x02"); // lang = JA
  disp.write("\x1B\x33\x01"); // font = 6x8

  LittleFS.begin(true, "/littlefs");
  ESP_LOGI("FS", "Free FS size = %i", LittleFS.totalBytes() - LittleFS.usedBytes());

  WiFi.begin(WIFI_NAME, WIFI_PASS);
  WiFi.waitForConnectResult();
  if(WiFi.isConnected()) Serial.println("WiFi ok");
  else Serial.println("!! WIFI FAIL !!");

  Core::Services::NTP::start();
  
  Wire.begin(32, 33, 100000);
  i2c = new Core::ThreadSafeI2C(&Wire);
  i2c->log_all_devices();

  spdif = new Platform::WM8805(i2c);
  spdif->initialize();
  spdif->set_enabled(true);

  pinMode(18 /* #mute */, OUTPUT);
  pinMode(26 /* deemph */, OUTPUT);
  digitalWrite(18, false); // set mute
  digitalWrite(26, false); // no deemph

  // release i2s bus by setting hi-Z
  pinMode(3 /* mck */, INPUT);
  digitalWrite(3, LOW);
  pinMode(27 /* data */, INPUT);
  digitalWrite(27, LOW);

  keypad = new Platform::Keypad(i2c);
  ide = new Platform::IDEBus(i2c);
  cdrom = new ATAPI::Device(ide);

  meta = new CD::CachingMetadataAggregateProvider("/littlefs");
  meta->providers.push_back(new CD::CDTextMetadataProvider());
  meta->providers.push_back(new CD::MusicBrainzMetadataProvider());
  meta->providers.push_back(new CD::CDDBMetadataProvider("gnudb.gnudb.org", "asdfasdf@example-esp32.com"));
  meta->providers.push_back(new CD::LrcLibLyricProvider());
  
  player = new CD::Player(cdrom, meta);
  disp.write("Hello!");
}

static TickType_t memory_last_print = 0;
static void print_memory() {
    TickType_t now = xTaskGetTickCount();
    if(now - memory_last_print > pdMS_TO_TICKS(30000)) {
        ESP_LOGI(LOG_TAG, "HEAP: %d free of %d (%d minimum)", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap());
#ifdef BOARD_HAS_PSRAM
        ESP_LOGI(LOG_TAG, "PSRAM: %d free of %d (%d minimum)", ESP.getFreePsram(), ESP.getPsramSize(), ESP.getMinFreePsram());
#endif
        memory_last_print = now;
    }
}

uint8_t kp_sts = 0;

CD::Player::State last_sts;
MSF last_msf;
CD::Player::TrackNo last_trkno;
int last_lyric_idx = -1;

// cppcheck-suppress unusedFunction
void loop() {
  // TBD: Maybe use motor control peripheral for this instead of software?
  digitalWrite(18, spdif->locked_on()); // only unmute when SPDIF locked

  print_memory();

  CD::Player::State sts = player->get_status();
  if(sts != last_sts) {
    ESP_LOGI("Test", "State %s -> %s", CD::Player::PlayerStateString(last_sts), CD::Player::PlayerStateString(sts));
    last_sts = sts;
    if(sts != CD::Player::State::PLAY && sts != CD::Player::State::PAUSE) {
      clrscr();
      disp.write(CD::Player::PlayerStateString(sts));
    }
  }
  
  MSF msf = player->get_current_track_time();

  CD::Player::TrackNo trk = player->get_current_track_number();
  if(trk.track != last_trkno.track || trk.index != last_trkno.index) {
    static char buf[128];
    buf[0] = 0;

    ESP_LOGI("Test", "Trk#: %i.%i -> %i.%i", last_trkno.track, last_trkno.index, trk.track, trk.index);
    last_lyric_idx = -1;
    auto slot = player->get_active_slot();

    if(trk.track <= slot.disc->tracks.size() && trk.index == 1) {
      auto meta = slot.disc->tracks[trk.track - 1];
      if(meta.title != "") {
        ESP_LOGI("Test", "... -> %s - %s", meta.artist.c_str(), meta.title.c_str());
        snprintf(buf, 128, "#%d: %s", trk.track, meta.title.c_str());
        clrscr();
        disp_write(buf);
      }
    }

    ESP_LOGI("Test", "SPDIF is %s", spdif->locked_on() ? "lock" : "UN LOCK !!");
    last_trkno = trk;
  }

  if((msf.M != last_msf.M || msf.S != last_msf.S || msf.F != last_msf.F) && trk.index == 1) {
    auto slot = player->get_active_slot();
    if(trk.track <= slot.disc->tracks.size()) {
      auto meta = slot.disc->tracks[trk.track - 1];
      if(!meta.lyrics.empty()) {
        int cur_ms = MSF_TO_MILLIS(msf);
        int idx = last_lyric_idx;
        for(int i = 0; i < meta.lyrics.size(); i++) {
          if(meta.lyrics[i].millisecond <= cur_ms && meta.lyrics[i + 1].millisecond > cur_ms) {
            idx = i;
            break;
          }
        }
        if(idx != last_lyric_idx && idx != -1) {
          last_lyric_idx = idx;
          ESP_LOGI("Lyric", " %i | %i | e=%i\t -= %s =- ", cur_ms, meta.lyrics[idx].millisecond, (cur_ms - meta.lyrics[idx].millisecond), meta.lyrics[idx].line.c_str());
          clrscr();
          disp_write(meta.lyrics[idx].line.c_str());
        }
      }
    }
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
        player->do_command(CD::Player::Command::END_SEEK);
      }
    }
  }
}