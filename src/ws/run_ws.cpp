extern "C" {
  #include "oswan/WS.h"
  #include "oswan/WSRender.h"
  #include "oswan/WSFileio.h"
}

#include <M5Cardputer.h>
#include "esp_timer.h"
#ifdef BENCHMARK_LOGS
#include "esp_heap_caps.h"
#endif
#include "ws_display.h"
#include "ws_sound.h"
#include "ws_save.h"
#include "share/emu_log_cpp.h"

static void ws_update_adaptive_frameskip(uint32_t core_us, uint32_t frame_us)
{
  static uint32_t samples = 0;
  static uint64_t total_us = 0;
  static uint32_t over_budget = 0;

  samples++;
  total_us += core_us;
  if (core_us > frame_us) over_budget++;

  if (samples < 30) return;

  const uint32_t avg_us = (uint32_t)(total_us / samples);
  const int oldSkip = FrameSkip;

  if ((avg_us > (frame_us * 105u) / 100u || over_budget > samples / 3) && FrameSkip < 4) {
    FrameSkip++;
  } else if (avg_us < (frame_us * 70u) / 100u && over_budget == 0 && FrameSkip > 0) {
    FrameSkip--;
  }

#ifdef BENCHMARK_LOGS
  if (FrameSkip != oldSkip) {
    EMU_LOG("[WS][BENCH] adaptive frameskip %d -> %d (avg %.2f ms, overBudget %lu/%lu)\n",
            oldSkip, FrameSkip, (float)avg_us / 1000.0f,
            (unsigned long)over_budget, (unsigned long)samples);
  }
#endif

  samples = 0;
  total_us = 0;
  over_budget = 0;
}

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
  ws_sound_init(24000);
  ws_sound_start_task(8, 0);
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
#ifdef BENCHMARK_LOGS
  uint64_t benchCoreTotalUs = 0;
  uint32_t benchCoreMaxUs = 0;
  uint32_t benchLateFrames = 0;
  uint32_t benchMaxLateUs = 0;
  uint64_t benchIdleDelayUs = 0;
  uint64_t benchIdleSpinUs = 0;
  uint32_t benchLastLargestInternal = 0;
#endif

  for (;;) {
    // Run one frame
    int64_t tRun0 = esp_timer_get_time();
    WsRun();
    uint32_t coreUs = (uint32_t)(esp_timer_get_time() - tRun0);
    ws_update_adaptive_frameskip(coreUs, frame_us);
#ifdef BENCHMARK_LOGS
    benchCoreTotalUs += coreUs;
    if (coreUs > benchCoreMaxUs) benchCoreMaxUs = coreUs;
#endif
    ws_save_tick();
    frameCount++;

    // Log framerate every 2 seconds
    uint32_t now = millis();
    if (now - lastLog >= 2000) {
      EMU_LOG("[WS] %lu frames rendered (%.2f FPS)\n",
             (unsigned long)frameCount,
             (float)frameCount / ((now - lastLog) / 1000.0f));
#ifdef BENCHMARK_LOGS
      WsCoreStats coreStats;
      WsGetAndResetStats(&coreStats);

      uint32_t dispFrames = 0, dispTotalUs = 0, dispMaxUs = 0, dispPending = 0;
      ws_display_get_and_reset_stats(&dispFrames, &dispTotalUs, &dispMaxUs, &dispPending);

      uint32_t audioBlocks = 0, audioUnderflows = 0, audioMaxAvailable = 0, audioMaxQueue = 0;
      ws_sound_get_and_reset_stats(&audioBlocks, &audioUnderflows, &audioMaxAvailable, &audioMaxQueue);

      const float coreAvgMs = frameCount ? (float)benchCoreTotalUs / (float)frameCount / 1000.0f : 0.0f;
      const float dispAvgMs = dispFrames ? (float)dispTotalUs / (float)dispFrames / 1000.0f : 0.0f;
      EMU_LOG("[WS][BENCH] core avg/max %.2f/%.2f ms, late %lu maxLate %.2f ms, idle delay/spin %.1f/%.1f ms\n",
              coreAvgMs, (float)benchCoreMaxUs / 1000.0f,
              (unsigned long)benchLateFrames, (float)benchMaxLateUs / 1000.0f,
              (double)benchIdleDelayUs / 1000.0, (double)benchIdleSpinUs / 1000.0);
      EMU_LOG("[WS][BENCH] core frames=%u steps=%u lines=%u paints=%u fs=%d apu=%u gdma=%u/%uB\n",
              coreStats.frames, coreStats.cpuSteps, coreStats.refreshLines,
              coreStats.paintRequests, coreStats.frameSkip, coreStats.apuTicks,
              coreStats.gdmaTransfers, coreStats.gdmaBytes);
      EMU_LOG("[WS][BENCH] irq key=%u htm=%u vtm=%u vblank=%u line=%u\n",
              coreStats.keyIrqs, coreStats.htimerIrqs, coreStats.vtimerIrqs,
              coreStats.vblankIrqs, coreStats.lineIrqs);
      if (coreStats.sramBankSwitches) {
        EMU_LOG("[WS][BENCH] sram bank switches=%u\n", coreStats.sramBankSwitches);
      }
      if (coreStats.spritePixels || coreStats.spriteLimitedLines || coreStats.spriteClipLeft || coreStats.spriteClipRight) {
        EMU_LOG("[WS][SPR] base=%04X first=%u cnt=%u cached=%u wrap=%u px=%u vis=%u/%u limit=%u clip=%u/%u skip=%u/%u/%u dsp=%02X\n",
                coreStats.spriteTableBase, coreStats.spriteFirst, coreStats.spriteCountReg,
                coreStats.spriteCached, coreStats.spriteWrapped,
                coreStats.spritePixels, coreStats.spriteVisible, coreStats.spriteCandidates,
                coreStats.spriteLimitedLines, coreStats.spriteClipLeft, coreStats.spriteClipRight,
                coreStats.spriteWindowSkips, coreStats.spritePrioritySkips,
                coreStats.spriteTransparentSkips, IO[0x00]);
      }
      EMU_LOG("[WS][BENCH] display frames=%lu avg/max %.2f/%.2f ms pending=%lu\n",
              (unsigned long)dispFrames, dispAvgMs, (float)dispMaxUs / 1000.0f,
              (unsigned long)dispPending);
      EMU_LOG("[WS][BENCH] audio blocks=%lu underflows=%lu maxAvail=%lu maxQueue=%lu\n",
              (unsigned long)audioBlocks, (unsigned long)audioUnderflows,
              (unsigned long)audioMaxAvailable, (unsigned long)audioMaxQueue);
      const uint32_t heapFree = esp_get_free_heap_size();
      const uint32_t largest8 = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
      const uint32_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      const uint32_t minFree = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
      EMU_LOG("[WS][BENCH] heap free=%u largest8=%u largestInternal=%u minFree=%u\n",
              heapFree, largest8, largestInternal, minFree);
      if (benchLastLargestInternal && largestInternal + 16384u < benchLastLargestInternal) {
        EMU_LOG("[WS][BENCH][HEAP] largestInternal drop %u -> %u bytes\n",
                benchLastLargestInternal, largestInternal);
      }
      benchLastLargestInternal = largestInternal;
      benchCoreTotalUs = 0;
      benchCoreMaxUs = 0;
      benchLateFrames = 0;
      benchMaxLateUs = 0;
      benchIdleDelayUs = 0;
      benchIdleSpinUs = 0;
#else
      EMU_LOG("[WS] HEAP: %u bytes\n", esp_get_free_heap_size());
#endif
      frameCount = 0;
      lastLog = now;
    }

    // Frame pacing (75Hz)
    next += frame_us;
    int64_t remain = (int64_t)next - (int64_t)esp_timer_get_time();
#ifdef BENCHMARK_LOGS
    if (remain < 0) {
      uint32_t lateUs = (uint32_t)(-remain);
      benchLateFrames++;
      if (lateUs > benchMaxLateUs) benchMaxLateUs = lateUs;
    } else if (remain > 2000) {
      benchIdleDelayUs += (uint32_t)remain;
    } else {
      benchIdleSpinUs += (uint32_t)remain;
    }
#endif
    if (remain > 2000) vTaskDelay(remain / 1000 / portTICK_PERIOD_MS);
    else if (remain > 0) ets_delay_us((uint32_t)remain);
    else next = esp_timer_get_time();
  }
}

