#include "Arduino.h"
#include <WiFi.h>
#include <LittleFS.h>

#include <esper-core/service.h>
#include <esper-core/platform.h>
#include <esper-gui/hardware/futaba_gp1232.h>
#include <esper-gui/compositing.h>
#include <esper-gui/text.h>

#include <modes/cd_mode.h>
#include <modes/netradio_mode.h>
#include <modes/bluetooth_mode.h>

static char LOG_TAG[] = "APL_MAIN";

Core::ThreadSafeI2C * i2c;
Platform::Keypad * keypad;
Graphics::Hardware::FutabaGP1232ADriver * disp;
Graphics::Compositor * compositor;
Platform::AudioRouter * router;
Platform::WM8805 * spdif;

Mode * app;

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

TaskHandle_t renderTaskHandle = 0;
void renderTask(void*) {
  while(true) {
    compositor->render(app->main_view());
    vTaskDelay(33);
  }
}

TaskHandle_t keypadTaskHandle = 0;
void keypadTask(void * pvArgs) {
  Platform::Keypad * kp = reinterpret_cast<Platform::Keypad*>(pvArgs);
  while(true) {
    kp->update();
    vTaskDelay(pdMS_TO_TICKS(33));
  }
}


// cppcheck-suppress unusedFunction
void setup(void) { 
  ESP_LOGI(LOG_TAG, "CPU speed = %i", getCpuFrequencyMhz());
#ifdef BOARD_HAS_PSRAM
  heap_caps_malloc_extmem_enable(1024);
#endif
  // Open Serial 
  Serial.begin(115200);
  while(!Serial);

  disp = new Graphics::Hardware::FutabaGP1232ADriver();
  disp->initialize();
  disp->set_power(true);
  disp->set_brightness(Graphics::Hardware::Brightness::DISP_BRIGHTNESS_50);
  compositor = new Graphics::Compositor(disp);

  Wire.begin(32, 33, 400000);
  i2c = new Core::ThreadSafeI2C(&Wire);
  i2c->log_all_devices();

  LittleFS.begin(true, "/mnt");
  ESP_LOGI("FS", "Free FS size = %i", LittleFS.totalBytes() - LittleFS.usedBytes());
  load_all_fonts();
  
  Core::Services::WLAN::start();
  Core::Services::NTP::start();

  keypad = new Platform::Keypad(i2c);

  spdif = new Platform::WM8805(i2c);
  spdif->initialize();

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

  app = new BluetoothMode({
    .i2c = i2c,
    .keypad = keypad,
    .router = router
  });

  app->setup();
  app->main_view().set_needs_display();

  xTaskCreatePinnedToCore(
    renderTask,
    "RENDER",
    16000,
    nullptr,
    2,
    &renderTaskHandle,
    0
  );

  xTaskCreatePinnedToCore(
    keypadTask,
    "KEYP",
    4000,
    keypad,
    2,
    &keypadTaskHandle,
    0
  );

  
}


// cppcheck-suppress unusedFunction
void loop() {
  print_memory();
  app->loop();
}