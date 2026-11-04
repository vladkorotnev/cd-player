#include "Arduino.h"
#include <WiFi.h>
#include <LittleFS.h>

#include <board_cfg.h> //<- must be first so that the config reflects in ESPerCore
#include <esper-core/service.h>
#include <web/web_remote.h>
#include <esper-gui/hardware/futaba_gp1232.h>
#include <web/mirrored_display.h>
#include <esper-gui/text.h>
#include <esper-gui/views/label.h>
#include <shared_prefs.h>

#include <mode_host.h>
#include <consts.h>
#include <mount.h>
#include <otafvu.h>
#include <web/web_remote_server.h>

static char LOG_TAG[] = "APL_MAIN";

#ifdef BCONF_HAS_I2C
Core::ThreadSafeI2C * i2c;
#endif
Platform::Keypad * keypad;
Platform::Remote * remote;
Graphics::Hardware::DisplayDriver * disp;
Platform::WebRemote * web_remote;
Graphics::MirroredDisplayDriver * mirrored_display;
Platform::AudioRouter * router;
Platform::IDEBus * ide;
ATAPI::Device * cdrom;

ModeHost * host;

static TickType_t memory_last_print = 0;
static void print_memory() {
    TickType_t now = xTaskGetTickCount();
    if(now - memory_last_print > pdMS_TO_TICKS(30000)) {
        ESP_LOGW(LOG_TAG, "HEAP: %d free of %d (%d minimum, %d contiguous available)", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
#ifdef BOARD_HAS_PSRAM
        ESP_LOGW(LOG_TAG, "PSRAM: %d free of %d (%d minimum, %d contiguous available)", ESP.getFreePsram(), ESP.getPsramSize(), ESP.getMinFreePsram(), ESP.getMaxAllocPsram());
#endif
        ESP_LOGW(LOG_TAG, "Loop task stack water mark = %d", uxTaskGetStackHighWaterMark(NULL));
        memory_last_print = now;
    }
}

void load_all_fonts() {
  Fonts::Font tmp;
  FILE * f = fopen((FONT_DIR_PREFIX "/default.mofo"), "rb");
  fseek(f, 0, SEEK_SET);
  if(!Fonts::MoFo::LoadFromHandle(f, &tmp)) ESP_LOGE(LOG_TAG, "keyrus0808 err");
  else Fonts::EGFont_register(tmp);

  if(!Fonts::MoFo::LoadFromHandle(f, &tmp)) ESP_LOGE(LOG_TAG, "keyrus0816 err");
  else Fonts::EGFont_register(tmp);

  if(!Fonts::MoFo::LoadFromHandle(f, &tmp)) ESP_LOGE(LOG_TAG, "xnu err");
  else Fonts::EGFont_register(tmp);

  if(!Fonts::MoFo::Load(FONT_DIR_PREFIX "/misaki_mincho.mofo", &tmp)) ESP_LOGE(LOG_TAG, "misaki err");
  else Fonts::EGFont_register(tmp);

  if(!Fonts::MoFo::Load(FONT_DIR_PREFIX "/jiskan16.mofo", &tmp)) ESP_LOGE(LOG_TAG, "jiskan err");
  else Fonts::EGFont_register(tmp);

  if(!Fonts::MoFo::Load(FONT_DIR_PREFIX "/k8x12.mofo", &tmp)) ESP_LOGE(LOG_TAG, "k8x12 err");
  else Fonts::EGFont_register(tmp);

  fclose(f);
}

TaskHandle_t keypadTaskHandle = 0;
void keypadTask(void *) {
  while(true) {
    keypad->update();
    remote->update();
  }
}

#if (BCONF_RC_TYPE == RC_TYPE_SONY_PS2_REMOTE)
  static Core::Remote::Sony20bitDecoder sony;
  static Core::Remote::KeymapDecoder keymap(sony, Platform::PS2_REMOTE_KEYMAP);
#else
  #error "IR RC type not set or unknown, check board config"
#endif

// cppcheck-suppress unusedFunction
void setup(void) { 
  TickType_t start = xTaskGetTickCount();
  ESP_LOGI(LOG_TAG, "CPU speed = %i", getCpuFrequencyMhz());
#ifdef BOARD_HAS_PSRAM
  heap_caps_malloc_extmem_enable(64);
#endif

  Serial.begin(BCONF_SERIAL_BAUD);
  while(!Serial) {}

  Graphics::Hardware::DisplayDriver *hardware_display = nullptr;
  #if (BCONF_DISPLAY_TYPE == DISP_TYPE_FUTABA_GP1232A)
    hardware_display = new Graphics::Hardware::FutabaGP1232ADriver(
      BCONF_DISPLAY_UART_NUM,
      BCONF_DISPLAY_RX_PIN,
      BCONF_DISPLAY_TX_PIN,
      BCONF_DISPLAY_BAUD
    );
  #elif (BCONF_DISPLAY_TYPE == DISP_TYPE_NULL)
    hardware_display = new Graphics::Hardware::NullDisplayDriver();
  #else
    #error "Display type not set or unknown, check board config or add correct driver"
  #endif

    mirrored_display = new Graphics::MirroredDisplayDriver(*hardware_display);
    disp = mirrored_display;
    disp->initialize();

  #ifdef BCONF_HAS_I2C
    Wire.begin(BCONF_I2C_SDA, BCONF_I2C_SCL, BCONF_I2C_FRQ);
    i2c = new Core::ThreadSafeI2C(&Wire);
    keypad = new Platform::Keypad(i2c);
    #if (BCONF_IDE_TYPE == IDEBUS_TYPE_ATAPIDUINO_ISH_I2C)
      ide = new Platform::IDEBus(i2c);
    #endif
  #endif

  #if IDEBUS_RELIES_ON_DRIVE_TO_PLAY(BCONF_IDE_TYPE)
    cdrom = new ATAPI::AutonomousPlayerDevice(ide);
  #else
    #error "ATAPI Device impl not done for the IDE BUS type; write new driver or check board config"
  #endif

  auto * infrared_remote = new Platform::InfraredRemote(keymap, BCONF_INFRARED_PIN);
  web_remote = new Platform::WebRemote(*infrared_remote);
  remote = web_remote;

  #if (BCONF_AUDIOROUTER_TYPE == AUDIOROUTER_TYPE_WOLFSON_WM8805)
    Platform::WM8805 * spdif = new Platform::WM8805(i2c);
    router = new Platform::WM8805AudioRouter(
      spdif,
      {
        .unmute = BCONF_DAC_UNMUTE_PIN,
        .demph = BCONF_DAC_DEMPH_PIN
      },
      {
        .mck = BCONF_DAC_MCK_PIN,
        .bck = BCONF_DAC_BCK_PIN,
        .lrck = BCONF_DAC_LRCK_PIN,
        .data = BCONF_DAC_DATA_PIN
      },
      BCONF_BYPASS_RELAY_PIN
    );
  #elif (IDEBUS_RELIES_ON_DRIVE_TO_PLAY(BCONF_IDE_TYPE))
    #error "Not using software playback but no SPDIF Transceiver, check board config"
  #else
    #error "TODO: decouple WM8805 from AudioRouter and support working without one!!"
  #endif

  const PlatformSharedResources rsrc = {
    .keypad = keypad,
    .remote = remote,
    .router = router,
    .display = disp,
    .ide = ide,
    .cdrom = cdrom
  };

  host = new ModeHost(rsrc);
  mount_fs_if_needed();
  load_all_fonts();
  UI::Label::set_scroll_speed(Prefs::get(PREFS_KEY_UI_SCROLL_SPEED));
  
  Core::Services::WLAN::start();
  bool is_recovery = OTAFVU::start_if_needed(rsrc, host);
  if(is_recovery) return;
  Core::Services::NTP::start();

  static WebRemoteServer web_server(*web_remote, *mirrored_display, *host);
  web_server.begin();

  if (Prefs::get(PREFS_KEY_TELNET_LOGGER)) {
    Core::Services::Telnet::start();
  }

  #ifdef BCONF_HAS_I2C
    i2c->log_all_devices();
  #endif
  router->initialize();
  cdrom->reset();
  xTaskCreatePinnedToCore(
    keypadTask,
    "KEYP",
    4000,
    nullptr,
    2,
    &keypadTaskHandle,
    0
  );

  // classic bt only in this project
  if(esp_bt_controller_mem_release(ESP_BT_MODE_BLE) != ESP_OK) ESP_LOGE(LOG_TAG, "BLE dealloc failed");

  host->activate_last_used_mode(); // when done booting, go to last used app
  TickType_t end = xTaskGetTickCount();
  ESP_LOGI(LOG_TAG, "Booted in %i ms", pdTICKS_TO_MS(end - start));
}


// cppcheck-suppress unusedFunction
void loop() {
  print_memory();
  host->loop();
}
