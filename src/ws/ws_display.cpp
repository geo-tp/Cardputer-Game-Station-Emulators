extern "C" {
  #include "oswan/WS.h"
  #include "oswan/WSRender.h"
}

#include <M5Cardputer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <algorithm>
#include <esp_attr.h>
#include "share/emu_log_cpp.h"

// -----------------------------------------------------------------------------
// Dimensions WonderSwan (src)
// -----------------------------------------------------------------------------
static const int kSrcW   = LCD_MAIN_W;     // 224
static const int kSrcH   = LCD_MAIN_H;     // 144
static const int kStride = SCREEN_WIDTH;

// -----------------------------------------------------------------------------
// Dimensions Cardputer (dst)
// -----------------------------------------------------------------------------
static const int kDstW = 240;
static const int kDstH = 135;

// -----------------------------------------------------------------------------
// Threading
// -----------------------------------------------------------------------------
static TaskHandle_t s_wsDispTask = nullptr;

// -----------------------------------------------------------------------------
// State & buffers
// -----------------------------------------------------------------------------
bool      ws_fullscreen     = true;
int       ws_zoomPercent    = 100;    // ROI zoom 
static int lastZoomPercent = -1;

static uint16_t* s_line16 = nullptr;  // ligne dest
static uint8_t*  s_xmap   = nullptr;  // map X (dstW -> srcX)
static uint8_t*  s_ymap   = nullptr;  // map Y (dstH -> srcY)
static int s_dstW = kDstW, s_dstH = kDstH;
static int s_offX = 0,     s_offY = 0;

// -----------------------------------------------------------------------------
// Utils
// -----------------------------------------------------------------------------
static void ws_display_free_buffers() {
  if (s_line16) { free(s_line16); s_line16 = nullptr; }
  if (s_xmap)   { free(s_xmap);   s_xmap   = nullptr; }
  if (s_ymap)   { free(s_ymap);   s_ymap   = nullptr; }
}

static void ws_display_alloc_buffers() {
  if (!s_line16) {
    s_line16 = (uint16_t*) heap_caps_malloc(kDstW * sizeof(uint16_t),
                                            MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  }
  if (!s_xmap) {
    s_xmap = (uint8_t*) heap_caps_malloc(kDstW * sizeof(uint8_t),
                                         MALLOC_CAP_8BIT);
  }
  if (!s_ymap) {
    s_ymap = (uint8_t*) heap_caps_malloc(kDstH * sizeof(uint8_t),
                                         MALLOC_CAP_8BIT);
  }
}

static void ws_display_compute_scaler() {
  M5Cardputer.Display.setSwapBytes(true);
  M5Cardputer.Display.fillScreen(TFT_BLACK);

  // destination
  if (ws_fullscreen) {
    s_dstW = kDstW; s_dstH = kDstH;
    s_offX = 0;     s_offY = 0;
  } else {
    const float sx = (float)kDstW / (float)kSrcW;
    const float sy = (float)kDstH / (float)kSrcH;
    const float s  = (sx < sy) ? sx : sy;
    s_dstW = std::max(1, (int)(kSrcW * s));
    s_dstH = std::max(1, (int)(kSrcH * s));
    s_offX = (kDstW - s_dstW) / 2;
    s_offY = (kDstH - s_dstH) / 2;
  }

  // ROI
  int zp   = (ws_zoomPercent <= 0) ? 100 : ws_zoomPercent;
  int roiW = kSrcW * 100 / zp;
  int roiH = kSrcH * 100 / zp;
  roiW = std::min(kSrcW, std::max(16, roiW));
  roiH = std::min(kSrcH, std::max(16, roiH));
  const int roiX0 = (kSrcW - roiW) / 2;
  const int roiY0 = (kSrcH - roiH) / 2;

  // LUT X/Y
  for (int dx = 0; dx < s_dstW; ++dx) {
    s_xmap[dx] = (uint8_t)(roiX0 + ((int64_t)dx * roiW / s_dstW));
  }
  for (int dy = 0; dy < s_dstH; ++dy) {
    s_ymap[dy] = (uint8_t)(roiY0 + ((int64_t)dy * roiH / s_dstH));
  }
}

// -----------------------------------------------------------------------------
// Render
// -----------------------------------------------------------------------------
static inline void ws_render_one_frame()
{
  const uint16_t* fb = (const uint16_t*) FrameBuffer;
  if (!fb || !s_line16 || !s_xmap || !s_ymap) return;

  // if zoom has changed, recompute scaler
  if (ws_zoomPercent != lastZoomPercent) {
    ws_display_compute_scaler();
    lastZoomPercent = ws_zoomPercent;
  }

  M5Cardputer.Display.startWrite();
  M5Cardputer.Display.setAddrWindow(s_offX, s_offY, s_dstW, s_dstH);

  int last_sy = -1;

  for (int dy = 0; dy < s_dstH; ++dy) {
    const int sy = s_ymap[dy];
    const uint16_t* srcLine = fb + sy * kStride;

    // construct line
    if (sy != last_sy) {
      int dx = 0;
      // unroll x8
      for (; dx + 8 <= s_dstW; dx += 8) {
        s_line16[dx + 0] = srcLine[s_xmap[dx + 0]];
        s_line16[dx + 1] = srcLine[s_xmap[dx + 1]];
        s_line16[dx + 2] = srcLine[s_xmap[dx + 2]];
        s_line16[dx + 3] = srcLine[s_xmap[dx + 3]];
        s_line16[dx + 4] = srcLine[s_xmap[dx + 4]];
        s_line16[dx + 5] = srcLine[s_xmap[dx + 5]];
        s_line16[dx + 6] = srcLine[s_xmap[dx + 6]];
        s_line16[dx + 7] = srcLine[s_xmap[dx + 7]];
      }
      for (; dx < s_dstW; ++dx) {
        s_line16[dx] = srcLine[s_xmap[dx]];
      }
      last_sy = sy;
    }

    // push line
    M5Cardputer.Display.writePixels(s_line16, s_dstW, true);
  }

  M5Cardputer.Display.endWrite();
}

// -----------------------------------------------------------------------------
// Task
// -----------------------------------------------------------------------------
static void ws_display_task(void* arg)
{
  (void)arg;

  ws_display_alloc_buffers();
  ws_display_compute_scaler();

  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.setSwapBytes(true);
  M5Cardputer.Display.fillScreen(TFT_BLACK);

  for (;;) {
    // Wait for notification
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    ws_render_one_frame();

    // Small pause
    if ((xTaskGetTickCount() & 7) == 0) taskYIELD();
  }
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------
extern "C" void ws_display_init(void)
{
  AllocateBuffers();   // buffers video core oswan 
}

extern "C" void ws_display_start()
{
  if (s_wsDispTask) return;

  BaseType_t ok = xTaskCreatePinnedToCore(
      ws_display_task, "WSDisp",
      2048, nullptr, 5, &s_wsDispTask,
      0 /* core */);

  if (ok != pdPASS) {
    s_wsDispTask = nullptr;
    EMU_LOG("[WS][ERR] display task create failed\n");
  }
}

extern "C" void ws_display_stop()
{
  if (s_wsDispTask) {
    vTaskDelete(s_wsDispTask);
    s_wsDispTask = nullptr;
  }
  ws_display_free_buffers();
}

// Hook oswan
extern "C" void ws_graphics_paint(void)
{
  if (!s_wsDispTask) return;
  xTaskNotifyGive(s_wsDispTask);   // non blocking
}
