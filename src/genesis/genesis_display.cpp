#include "genesis_display.h"
#include <string.h>
#include <stdio.h>
#include "esp_heap_caps.h"
#include <Arduino.h>
#include <M5Cardputer.h>
#include "share/emu_log_cpp.h"

extern "C" {
  // Gwenesis VDP
  extern unsigned int screen_height;
}

// Globals
QueueHandle_t g_scanQ = nullptr;
TaskHandle_t  g_displayTaskHandle = nullptr;
int g_dstW, g_dstH;
int g_viewX0, g_viewY0;
int g_viewW, g_viewH;
bool g_inFrame = false;
int g_prevDstY = 0;
int g_srcH_cached = -1;
int g_field_ofs = 0;  // 0 = odd, 1 = even
uint16_t *s_lineImg  = nullptr;
uint16_t *s_lineFull = nullptr;
int s_capImg  = 0;
int s_capFull = 0;
static uint16_t *s_xmap = nullptr;
static int s_xmap_srcW  = -1, s_xmap_dstW = -1;
static int s_xmap_roiX0 = -1, s_xmap_roiW = -1;
static int s_roiX0 = 0, s_roiY0 = 0, s_roiW = 0, s_roiH = 0;

int genesisZoomPercent = 110;

/* Memory set and copy helpers */
static inline void memset16(uint16_t *dst, uint16_t v, int count) {
  for (int i = 0; i < count; ++i) dst[i] = v;
}
static inline void memcpy16(uint16_t *dst, const uint16_t *src, int count) {
  memcpy(dst, src, count * sizeof(uint16_t));
}

/* helpers ROI & xmap */
static inline int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

/* Calculate centered Region of Interest */
static inline void compute_centered_roi(int srcW, int srcH) {
  int zp = (genesisZoomPercent <= 0) ? 100 : genesisZoomPercent;
  int roiW = (srcW * 100) / zp;
  int roiH = (srcH * 100) / zp;

  // Bornes
  roiW = clampi(roiW, 16, srcW);
  roiH = clampi(roiH, 16, srcH);

  int roiX0 = (srcW - roiW) / 2;
  int roiY0 = (srcH - roiH) / 2;

  s_roiX0 = roiX0;
  s_roiY0 = roiY0;
  s_roiW  = roiW;
  s_roiH  = roiH;
}

/* Ensure xmap ROI */
static inline void ensure_xmap_roi(int srcW, int dstW, int roiX0, int roiW) {
  if (s_xmap &&
      s_xmap_srcW == srcW &&
      s_xmap_dstW == dstW &&
      s_xmap_roiX0 == roiX0 &&
      s_xmap_roiW  == roiW) {
    return;
  }
  free(s_xmap);
  s_xmap = (uint16_t*)heap_caps_malloc(dstW * sizeof(uint16_t),
                                       MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
  s_xmap_srcW = srcW;
  s_xmap_dstW = dstW;
  s_xmap_roiX0 = roiX0;
  s_xmap_roiW  = roiW;

  // x -> roiX0 + x * roiW / dstW
  for (int x = 0; x < dstW; ++x) {
    s_xmap[x] = (uint16_t)(roiX0 + ((x * roiW) / dstW));
  }
}

/* Allocate buffers for line rendering */
static inline void allocate_line_buffers() {
  if (g_viewW > s_capImg) {
    free(s_lineImg);
    s_lineImg  = (uint16_t*)heap_caps_malloc(g_viewW * sizeof(uint16_t),
                                             MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_capImg   = g_viewW;
  }
  if (g_dstW > s_capFull) {
    free(s_lineFull);
    s_lineFull = (uint16_t*)heap_caps_malloc(g_dstW * sizeof(uint16_t),
                                             MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_capFull  = g_dstW;
  }
}

/* Initialize display subsystem */
extern "C" void genesis_display_init(void) {
  g_dstW = M5.Lcd.width();   // 240
  g_dstH = M5.Lcd.height();  // 135

  // Screen viewport defaults cardputer
  g_viewW = g_dstW;
  g_viewH = g_dstH;
  g_viewX0 = 0;
  g_viewY0 = 0;

  // Buffers
  if (!s_lineFull) s_lineFull = (uint16_t*)heap_caps_malloc(g_dstW * sizeof(uint16_t),
                                    MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  if (!s_lineImg)  s_lineImg  = (uint16_t*)heap_caps_malloc(g_viewW * sizeof(uint16_t),
                                    MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
}

/* Display task */
void display_task(void* arg) {
  allocate_line_buffers();
  int prevDstY = 0;
  bool inFrame = false;
  int cachedSrcH = -1;
  int roiInitForSrcW = -1;

  for (;;) {
    ScanMsg m;
    if (xQueueReceive(g_scanQ, &m, portMAX_DELAY) != pdTRUE) continue;

    if (m.type == MSG_BEGIN_FRAME) {
      cachedSrcH = m.srcH;
      compute_centered_roi(/*srcW=*/FB_W, /*srcH=*/cachedSrcH); 
      roiInitForSrcW = -1;

      M5.Lcd.startWrite();
      inFrame = true;
      prevDstY = g_viewY0;
      continue;
    }

    if (m.type == MSG_END_FRAME) {
      if (inFrame) {
        M5.Lcd.endWrite();
        inFrame = false;
      }
      continue;
    }

    if (roiInitForSrcW != (int)m.w) {
      compute_centered_roi((int)m.w, (int)m.srcH);
      ensure_xmap_roi(/*srcW*/ m.w, /*dstW*/ g_viewW, /*roiX0*/ s_roiX0, /*roiW*/ s_roiW);
      roiInitForSrcW = (int)m.w;
    } else {
      ensure_xmap_roi(/*srcW*/ m.w, /*dstW*/ g_viewW, /*roiX0*/ s_roiX0, /*roiW*/ s_roiW);
    }

    if (m.w == g_viewW && s_roiX0 == 0 && s_roiW == m.w) {
      memcpy16(s_lineImg, m.data, g_viewW);
    } else {
      const uint16_t *xmap = s_xmap;
      for (int x = 0; x < g_viewW; ++x) {
        s_lineImg[x] = m.data[xmap[x]];
      }
    }

    const int leftBlack  = g_viewX0;
    const int rightBlack = g_dstW - (g_viewX0 + g_viewW);

    if (leftBlack > 0)  memset16(s_lineFull, 0, leftBlack);
    memcpy16(s_lineFull + leftBlack, s_lineImg, g_viewW);
    if (rightBlack > 0) memset16(s_lineFull + leftBlack + g_viewW, 0, rightBlack);

    int srcLineP1 = m.line + 1;
    int relP1     = srcLineP1 - s_roiY0;
    relP1 = clampi(relP1, 0, s_roiH);
    int dstY_end  = g_viewY0 + ((relP1 * g_viewH) / s_roiH);

    int linesToPush = dstY_end - prevDstY;

    while (linesToPush > 0) {
      int chunk = (linesToPush > 16) ? 16 : linesToPush;
      M5.Lcd.setAddrWindow(0, prevDstY, g_dstW, chunk);
      for (int i = 0; i < chunk; ++i) {
        M5.Lcd.writePixels(s_lineFull, g_dstW);
      }
      prevDstY    += chunk;
      linesToPush -= chunk;
      if ((prevDstY & 31) == 0) vTaskDelay(0);
    }

    // Yield
    if ((m.line & 31) == 31) vTaskDelay(0);
  }
}

/* Start the display task */
extern "C" void genesis_display_start(void) {
  if (!g_scanQ) {
    g_scanQ = xQueueCreate(SCANLINE_QUEUE_DEPTH, sizeof(ScanMsg));
    if (!g_scanQ) {
      EMU_LOG("[DISPLAY] queue create failed\n");
      return;
    }
  }
  if (!g_displayTaskHandle) {
    BaseType_t ok = xTaskCreatePinnedToCore(
      display_task, "DisplayTask",
      3192, nullptr, 6, &g_displayTaskHandle,
      0 /* core  */
    );
    if (ok != pdPASS) {
      EMU_LOG("[DISPLAY] task create failed\n");
      vQueueDelete(g_scanQ); g_scanQ = nullptr;
    }
  }
}

/* Stop the display task */
extern "C" void genesis_display_stop(void) {
  if (g_displayTaskHandle) {
    vTaskDelete(g_displayTaskHandle);
    g_displayTaskHandle = nullptr;
  }
  if (g_scanQ) {
    vQueueDelete(g_scanQ);
    g_scanQ = nullptr;
  }
}

/* Begin a new frame */
extern "C" void genesis_display_begin_frame(uint16_t srcH) {
  if (!g_scanQ) return;
  // Reset cached ROI 
  s_roiX0 = 0; s_roiY0 = 0; s_roiW = FB_W; s_roiH = srcH;
  ScanMsg b = { MSG_BEGIN_FRAME, 0, (uint16_t)FB_W, srcH, {0} };
  xQueueSend(g_scanQ, &b, portMAX_DELAY);
}

/* Gwenesis push scanline */
extern "C" void IRAM_ATTR GWENESIS_PUSH_SCANLINE(int line, const uint16_t* src16, int w) {
  if (!g_scanQ) return;

  ScanMsg m;
  m.type = MSG_SCANLINE;
  m.line = (uint16_t)line;
  m.w    = (uint16_t)w;

  int srcH = (g_srcH_cached > 0) ? g_srcH_cached
                                 : (int)(screen_height ? screen_height : 224);
  m.srcH = (uint16_t)srcH;

  // Copie ligne
  int copyW = (w < FB_W) ? w : FB_W;
  memcpy(m.data, src16, copyW * sizeof(uint16_t));
  if (copyW < FB_W) memset(m.data + copyW, 0, (FB_W - copyW) * sizeof(uint16_t));

  xQueueSend(g_scanQ, &m, portMAX_DELAY);
}

/* End the current frame */
extern "C" void genesis_display_end_frame(void) {
  if (!g_scanQ) return;
  uint16_t h = (uint16_t)(screen_height ? screen_height : 224u);
  ScanMsg e = { MSG_END_FRAME, 0, (uint16_t)FB_W, h, {0} };
  xQueueSend(g_scanQ, &e, portMAX_DELAY);
}
