#pragma GCC optimize ("Os")

#include "run_sms.h"

#include <M5Cardputer.h>
#include <stdio.h>
#include <string.h>

#include "cardputer/CardputerView.h"
#include "cardputer/CardputerInput.h"

#include "sms/display.h"
#include "sms/sound.h"
#include "sms/input.h"
#include "sms/save.h"
#include "share/emu_log_cpp.h"

// WARNING: The real bios is needed for Coleco emulation
// It is not included in the repo for legal reasons
// #include "sms/smsplus/coleco_bios.h"
const unsigned char ColecoVision_BIOS[8192] = {0}; // dummy data as fallback

static uint8_t map_console_type(SmsConsoleMode mode)
{
  switch (mode) {
    case SMS_MODE_GG: return TYPE_GG;
    case SMS_MODE_SG1000: return TYPE_SG1000;
    case SMS_MODE_COLECO: return TYPE_COLECO;
    case SMS_MODE_SMS:
    default: return TYPE_SMS;
  }
}

void run_sms(const uint8_t* romPtr, size_t romLen, SmsConsoleMode mode, const char* romName)
{
  CardputerView display;
  CardputerInput input;
  display.initialize();

  const bool isGG = (mode == SMS_MODE_GG);
  /* Persistent SRAM is only relevant for SMS/GG mappers. */
  const bool needsSram = (mode == SMS_MODE_SMS || mode == SMS_MODE_GG);
  const uint8_t consoleType = map_console_type(mode);

  // Runtime buffers only: no core allocation at boot.
  uint8_t* videoBuf = (uint8_t*)heap_caps_aligned_alloc(
      32, 256 * 240, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
  uint8_t* dummyBuf = (uint8_t*)heap_caps_aligned_alloc(
      32, 0x2000, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  uint8_t* sram = nullptr;
  if (needsSram) {
    sram = (uint8_t*)heap_caps_aligned_alloc(
        32, 0x8000, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (!videoBuf || !dummyBuf || (needsSram && !sram)) {
    EMU_LOG("SMS core alloc failed: video=%p dummy=%p sram=%p\n", videoBuf, dummyBuf, sram);
    free(videoBuf);
    free(dummyBuf);
    free(sram);
    return;
  }

  memset(dummyBuf, 0, 0x2000);
  if (sram) memset(sram, 0xFF, 0x8000);

  sms.coleco_bios = (mode == SMS_MODE_COLECO)
      ? (uint8_t*)ColecoVision_BIOS
      : nullptr;

  // Mapping structures core
  sms.dummy = dummyBuf;
  sms.sram  = sram;

  bitmap.width  = 256;
  bitmap.height = 192;
  bitmap.pitch  = 256;
  bitmap.depth  = 8;
  bitmap.data   = videoBuf + 24 * 256;

  cart.rom   = (uint8_t*)romPtr;
  /* SMS/GG use 16KB banking, Coleco path uses 8KB banking. */
  if (mode == SMS_MODE_COLECO) {
    cart.pages = (romLen + 0x1FFF) / 0x2000;
  } else {
    cart.pages = (romLen + 0x3FFF) / 0x4000;
  }
  cart.type  = consoleType;
  smsZoomPercent = isGG ? 100 : 110;
  render_set_console_type(cart.type);
  
  if (!z80_allocate_flag_tables()) {
    EMU_LOG("SMS Z80 alloc failed\n");
    free(videoBuf);
    free(dummyBuf);
    free(sram);
    sms.coleco_bios = nullptr;
    return;
  }
  if (!sms_init_ram()) {
    EMU_LOG("SMS WRAM alloc failed\n");
    free(videoBuf);
    free(dummyBuf);
    free(sram);
    sms.coleco_bios = nullptr;
    return;
  }
  emu_system_init(22050);
  system_reset();

  // Save
  if (sram) {
    sms_save_init(romName, sram, 0x8000);
    sms_save_load();
  }

  // Display
  sms_display_init(); 
  sms_palette_init_fixed();   
  video_compute_scaler_full();

  // Audio
  sms_audio_init();
  sms_audio_start_task(); 

  // Main loop
  bool lastToggle = fullscreen;
  uint32_t frameCount = 0;
  uint32_t lastFpsTime = millis();
  float avgFrameTime = 0;
  float avgFrameTimeRT = 0;
  float accFrameUs = 0.f;
  const uint32_t TARGET_US = 16667; // 60 Hz

  for (;;) {
    uint32_t t0 = micros();

    sms_save_tick();
    sms_frame(0);
    if (lastToggle != fullscreen) {
      lastToggle = fullscreen;
      if (fullscreen) video_compute_scaler_full();
      else            video_compute_scaler_square();
    }
    cardputer_read_input(mode);
    sms_display_write_frame();

    // Pacing 60 Hz
    uint32_t emuUs = micros() - t0;
    int32_t remaining = TARGET_US - emuUs;
    if (remaining > 0) {
      delayMicroseconds(remaining);
    }
    
    // // Realtime
    // uint32_t frameUs = micros() - t0;

    // // Stats
    // avgFrameTime += emuUs;      // perf brute
    // avgFrameTimeRT += frameUs;  // FPS effectif
    // frameCount++;

    // if (millis() - lastFpsTime >= 1000) {
    //   float avgRaw = avgFrameTime / frameCount;
    //   float fpsRaw = 1000000.0f / avgRaw;
    //   float speedPct = (fpsRaw / 60.0f) * 100.0f;

    //   float avgRT = avgFrameTimeRT / frameCount;
    //   float fpsRT = 1000000.0f / avgRT;

    //   EMU_LOG("[Perf] raw=%.1f us (%.1f fps, %.1f%%) | realtime=%.1f us (%.2f fps)\n",
    //         avgRaw, fpsRaw, speedPct, avgRT, fpsRT);

    //   avgFrameTime = 0;
    //   avgFrameTimeRT = 0;
    //   frameCount = 0;
    //   lastFpsTime = millis();
    // }
  }
}
