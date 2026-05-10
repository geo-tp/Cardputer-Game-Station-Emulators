extern "C" {
  #include "oswan/WS.h"
  #include "oswan/WSRender.h"
  #include "oswan/WSFileio.h"
}

#include <M5Cardputer.h>
#include "esp_timer.h"
#include "ws_display.h"
#include "ws_sound.h"
#include "ws_save.h"
#include "share/emu_log_cpp.h"

extern "C" void run_ws(const uint8_t* rom, size_t len, const char* rom_name, bool is_color)
{
  EMU_LOG("[WS] ===== WonderSwan Start =====\n");
  EMU_LOG("[WS] ROM size: %u bytes, color mode: %s\n",
         (unsigned)len, is_color ? "COLOR" : "MONO");

  // LCD
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setSwapBytes(true);
  M5Cardputer.Display.fillScreen(TFT_BLACK);

  // Core/Display/Sound init
  ws_display_init();
  ws_display_start();
  ws_sound_init(48000);
  ws_sound_start_task(16, 0);
  WsInit(); // splash screen
  EMU_LOG("[WS] Init done\n");
  
  // Load ROM
  if (WsCreateFromMemory(rom, len) != 0) {
    EMU_LOG("[WS][ERR] Cart load failed! len=%u\n, SRAM could be too big", (unsigned)len);
    for(;;) delay(1000);
  }
  EMU_LOG("[WS] Cart loaded successfully\n");

  // SRAM save/load
  ws_save_init(rom_name);
  ws_save_load();

  // Timing
  const uint32_t frame_us = 1000000u / 75u; // 13.3 ms
  uint64_t next = esp_timer_get_time();
  EMU_LOG("[WS] Frame pacing: %uus/frame\n", frame_us);
  uint32_t frameCount = 0;
  uint32_t lastLog = millis();

  for (;;) {
    // Run one frame
    WsRun();
    ws_save_tick();
    frameCount++;

    // Log framerate every 2 seconds
    uint32_t now = millis();
    if (now - lastLog >= 2000) {
      EMU_LOG("[WS] %lu frames rendered (%.2f FPS)\n",
             (unsigned long)frameCount,
             (float)frameCount / ((now - lastLog) / 1000.0f));
      frameCount = 0;
      EMU_LOG("[WS] HEAP: %u bytes\n", esp_get_free_heap_size());
      lastLog = now;
    }

    // Frame pacing (75Hz)
    next += frame_us;
    int64_t remain = (int64_t)next - (int64_t)esp_timer_get_time();
    if (remain > 2000) vTaskDelay(remain / 1000 / portTICK_PERIOD_MS);
    else if (remain > 0) ets_delay_us((uint32_t)remain);
    else next = esp_timer_get_time();
  }
}

