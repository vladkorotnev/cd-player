#include "Arduino.h"
#include <esper-core/keypad.h>
#include <esper-cdp/atapi.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <cddb/cddb.h>
#include <cddb/cddb_ni.h>
#include <mbedtls/sha1.h>

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

unsigned char *rfc822_binary (void *src,unsigned long srcl,size_t *len)
{
  unsigned char *ret,*d;
  unsigned char *s = (unsigned char *) src;
  const char *v = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._";
  unsigned long i = ((srcl + 2) / 3) * 4;
  *len = i += 2 * ((i / 60) + 1);
  d = ret = (unsigned char *) malloc ((size_t) ++i);
  for (i = 0; srcl; s += 3) {	/* process tuplets */
    *d++ = v[s[0] >> 2];	/* byte 1: high 6 bits (1) */
				/* byte 2: low 2 bits (1), high 4 bits (2) */
    *d++ = v[((s[0] << 4) + (--srcl ? (s[1] >> 4) : 0)) & 0x3f];
				/* byte 3: low 4 bits (2), high 2 bits (3) */
    *d++ = srcl ? v[((s[1] << 2) + (--srcl ? (s[2] >> 6) : 0)) & 0x3f] : '-';
				/* byte 4: low 6 bits (3) */
    *d++ = srcl ? v[s[2] & 0x3f] : '-';
    if (srcl) srcl--;		/* count third character if processed */
    if ((++i) == 15) {		/* output 60 characters? */
      i = 0;			/* restart line break count, insert CRLF */
      *d++ = '\015'; *d++ = '\012';
    }
  }
  *d = '\0';			/* tie off string */

  return ret;			/* return the resulting string */
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
          
          auto cddb = cddb_new();
          cddb_cache_disable(cddb);
          // cddb_set_server_name(cddb, "freedb.dbpoweramp.com");
          // cddb_set_server_name(cddb, "gnudb.gnudb.org");
          cddb_set_email_address(cddb, "sadhajsdka@esp32.example.com");
          cddb_log_set_level(cddb_log_level_t::CDDB_LOG_INFO);
          
          auto disc = cddb_disc_new();
          MSF len = toc.leadOut;
          ESP_LOGI("CDDB", "Lead out at %02im%02is%02if", len.M, len.S, len.F);
          cddb_disc_set_length(disc, FRAMES_TO_SECONDS(MSF_TO_FRAMES(len)));

          // MUSICBRAINZ ==========
          mbedtls_sha1_context ctx;
          mbedtls_sha1_init(&ctx);
          mbedtls_sha1_starts_ret(&ctx);

          char temp[16] = { 0 };
          unsigned char sha[20] = { 0 };
          sprintf(temp, "%02X", toc.tracks[0].number);
          mbedtls_sha1_update_ret(&ctx, (unsigned char*) temp, strlen(temp));

          sprintf(temp, "%02X", toc.tracks.back().number);
          mbedtls_sha1_update_ret(&ctx, (unsigned char*) temp, strlen(temp));

          sprintf(temp, "%08X", MSF_TO_FRAMES(toc.leadOut));
          mbedtls_sha1_update_ret(&ctx, (unsigned char*) temp, strlen(temp));
          for (int i = 0; i < 99; i++) {
              if(i < toc.tracks.size()) {
                sprintf(temp, "%08X", MSF_TO_FRAMES(toc.tracks[i].position));
                mbedtls_sha1_update_ret(&ctx, (unsigned char*) temp, strlen(temp));
              }
              else {
                mbedtls_sha1_update_ret(&ctx, (unsigned char*) "00000000", 8);
              }
          }
          mbedtls_sha1_finish_ret(&ctx, sha);
          mbedtls_sha1_free(&ctx);
          Serial.print("MBrainz SHA: ");
          for(int i = 0; i < 20; i++) {
            Serial.print(sha[i], HEX); Serial.print(" ");
          }
          Serial.println();
          size_t dummy;
          unsigned char * mbid = rfc822_binary(sha, 20, &dummy);
          Serial.print("MBrainz discId = ");
          Serial.println((const char*)mbid);
          // use it like https://musicbrainz.org/ws/2/discid/F9XV7VTnpufEOtGcaHcitqk0k2s-?inc=recordings&fmt=json
          free(mbid);

          cddb_track_t *trk;
          for(int i = 0; i < toc.tracks.size(); i++) {
            trk = cddb_track_new();
            cddb_disc_add_track(disc, trk);
            len = toc.tracks[i].position;
            ESP_LOGV("CDDB", "Track at %02im%02is%02if", len.M, len.S, len.F);
            cddb_track_set_frame_offset(trk, MSF_TO_FRAMES(len));
          }

          cddb_disc_calc_discid(disc);
          ESP_LOGI("CDDB", "DISC ID %08x", cddb_disc_get_discid(disc));
          int matches = cddb_query(cddb, disc);
          if(matches == -1) {
              cddb_error_print(cddb_errno(cddb));
          } else {
            Serial.print(matches); Serial.println(" found");
            int i = 0;
            while(i < matches) {
              i++;
              /* Print out the information for the current match. */
              printf("Match %d\n", i);
              /* Retrieve and print the category and disc ID. */
              printf("  category: %s (%d)\t%08x\n", cddb_disc_get_category_str(disc),
                    cddb_disc_get_category(disc), cddb_disc_get_discid(disc));
              /* Retrieve and print the disc title and artist name. */

              if(matches == 1) {
                bool success = cddb_read(cddb, disc);
                if(!success) cddb_error_print(cddb_errno(cddb));
                const char *s;
                int length;
                printf("\n\nTITLE:  '%s'\n ARTIST: %s\n\n", cddb_disc_get_title(disc),
                cddb_disc_get_artist(disc));
                for (trk = cddb_disc_get_track_first(disc); 
                    trk != NULL; 
                    trk = cddb_disc_get_track_next(disc)) {
                    printf("  [%02d]", cddb_track_get_number(trk));
                    printf(" '%s'", STR_OR_NULL(cddb_track_get_title(trk)));
                    s = cddb_track_get_artist(trk);
                    if (s) {
                        printf(" by %s", s); 
                    }
                    length = cddb_track_get_length(trk);
                    if (length != -1) {
                        printf(" (%d:%02d)", (length / 60), (length % 60));
                    } else {
                        printf(" (unknown)");
                    }
                    s = cddb_track_get_ext_data(trk);
                    if (s) {
                        printf(" [%s]\n", s);
                    } else {
                        printf("\n");
                    }
                }

                printf("\n\n\n\n");
              }
                    /* Get the next match, if there is one left. */
              if (i < matches) {
                  /* If there are multiple matches, then you can use the
                    following function to retrieve the matches beyond the
                    first.  This function will overwrite any data that
                    might be present in the disc structure passed to it.
                    So if you still need that disc for anything else, then
                    first create a new disc and use that to call this
                    function.  If there are no more matches left, false
                    (i.e. 0) will be returned. */
                  if (!cddb_query_next(cddb, disc)) {
                      Serial.print("query index out of bounds");
                  }
              }
            }
          }
          cddb_disc_destroy(disc);
          cddb_destroy(cddb);
        }
      }
      else {
        cdrom->query_position();
      }
    }
  }
}