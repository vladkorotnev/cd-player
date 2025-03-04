#include "Arduino.h"
#include <WiFi.h>
#include <LittleFS.h>

#include <esper-core/service.h>
#include <esper-gui/hardware/futaba_gp1232.h>
#include <esper-gui/text.h>

#include <mode_host.h>

static char LOG_TAG[] = "APL_MAIN";

Core::ThreadSafeI2C * i2c;
Platform::Keypad * keypad;
Platform::Remote * remote;
Graphics::Hardware::FutabaGP1232ADriver * disp;
Platform::AudioRouter * router;
Platform::WM8805 * spdif;
Platform::IDEBus * ide;
ATAPI::Device * cdrom;

ModeHost * host;

static TickType_t memory_last_print = 0;
static void print_memory() {
    TickType_t now = xTaskGetTickCount();
    if(now - memory_last_print > pdMS_TO_TICKS(30000)) {
        ESP_LOGV(LOG_TAG, "HEAP: %d free of %d (%d minimum, %d contiguous available)", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
#ifdef BOARD_HAS_PSRAM
        ESP_LOGV(LOG_TAG, "PSRAM: %d free of %d (%d minimum, %d contiguous available)", ESP.getFreePsram(), ESP.getPsramSize(), ESP.getMinFreePsram(), ESP.getMaxAllocPsram());
#endif
        memory_last_print = now;
    }
}

void load_all_fonts() {
  Fonts::Font tmp;
  FILE * f = fopen("/mnt/font/default.mofo", "rb");
  fseek(f, 0, SEEK_SET);
  if(!Fonts::MoFo::LoadFromHandle(f, &tmp)) ESP_LOGE(LOG_TAG, "keyrus0808 err");
  else Fonts::EGFont_register(tmp);

  if(!Fonts::MoFo::LoadFromHandle(f, &tmp)) ESP_LOGE(LOG_TAG, "keyrus0816 err");
  else Fonts::EGFont_register(tmp);

  if(!Fonts::MoFo::LoadFromHandle(f, &tmp)) ESP_LOGE(LOG_TAG, "xnu err");
  else Fonts::EGFont_register(tmp);

  if(!Fonts::MoFo::Load("/mnt/font/misaki_mincho.mofo", &tmp)) ESP_LOGE(LOG_TAG, "misaki err");
  else Fonts::EGFont_register(tmp);

  if(!Fonts::MoFo::Load("/mnt/font/jiskan16.mofo", &tmp)) ESP_LOGE(LOG_TAG, "jiskan err");
  else Fonts::EGFont_register(tmp);

  if(!Fonts::MoFo::Load("/mnt/font/k8x12.mofo", &tmp)) ESP_LOGE(LOG_TAG, "k8x12 err");
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

static Core::Remote::Sony20bitDecoder sony;
static Core::Remote::KeymapDecoder keymap(sony, Platform::PS2_REMOTE_KEYMAP);

// cppcheck-suppress unusedFunction
void setup(void) { 
  TickType_t start = xTaskGetTickCount();
  ESP_LOGI(LOG_TAG, "CPU speed = %i", getCpuFrequencyMhz());
#ifdef BOARD_HAS_PSRAM
  heap_caps_malloc_extmem_enable(64);
#endif

  Serial.begin(115200);
  while(!Serial);

  disp = new Graphics::Hardware::FutabaGP1232ADriver();
  disp->initialize();

  Wire.begin(32, 33, 400000);

  i2c = new Core::ThreadSafeI2C(&Wire);
  keypad = new Platform::Keypad(i2c);
  remote = new Platform::Remote(keymap);
  ide = new Platform::IDEBus(i2c);
  cdrom = new ATAPI::Device(ide);
  spdif = new Platform::WM8805(i2c);
  router = new Platform::AudioRouter(
    spdif,
    {
      .unmute = GPIO_NUM_18,
      .demph = GPIO_NUM_26
    },
    {
      .mck = GPIO_NUM_3,
      .bck = GPIO_NUM_5,
      .lrck = GPIO_NUM_2,
      .data = GPIO_NUM_27
    }
  );

  const PlatformSharedResources rsrc = {
    .i2c = i2c,
    .keypad = keypad,
    .remote = remote,
    .router = router,
    .display = disp,
    .ide = ide,
    .cdrom = cdrom
  };

  host = new ModeHost(rsrc);

  i2c->log_all_devices();

  LittleFS.begin(true, "/mnt");
  ESP_LOGI("FS", "Free FS size = %i", LittleFS.totalBytes() - LittleFS.usedBytes());
  load_all_fonts();
  
  Core::Services::WLAN::start();
  Core::Services::NTP::start();
  spdif->initialize();
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