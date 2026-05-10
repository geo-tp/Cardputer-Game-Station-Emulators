// lynx_display.cpp

#include "lynx_display.h"

#include <Arduino.h>
#include <M5Cardputer.h>
#include "esp_heap_caps.h"
#include "share/emu_log_cpp.h"

// Public
bool lynxFullScreen   = true;
int  lynxZoomPercent  = 100; 

// Message
typedef struct {
  const uint16_t *fb;
  int width;
  int height;
} LynxFrameMsg;

// Globals
static QueueHandle_t s_frameQ  = nullptr;
static TaskHandle_t  s_task    = nullptr;
static uint16_t     *s_lineBuf = nullptr;
static int           s_lineCap = 0;
static uint16_t     *s_xmap    = nullptr;
static uint16_t     *s_ymap    = nullptr;
static int           s_xmapCap = 0;
static int           s_ymapCap = 0;

struct LynxDisplayTransform {
  int   dstW;
  int   dstH;
  int   xOffset;
  int   yOffset;
  float invScaleX;
  float invScaleY;
  float srcCX;
  float srcCY;
  float dstCX;
  float dstCY;
};

static LynxDisplayTransform s_transform;

// cache for transform computation
static int s_lastSrcW = 0;
static int s_lastSrcH = 0;
static int s_lastLcdW = 0;
static int s_lastLcdH = 0;
static int s_lastZoomPercent = -1;
static bool s_lastFullScreen  = false;

static void lynx_display_compute_transform(int srcW, int srcH)
{
  int lcdW = M5Cardputer.Display.width();
  int lcdH = M5Cardputer.Display.height();

  if (lcdW <= 0 || lcdH <= 0 || srcW <= 0 || srcH <= 0) {
    return;
  }

  bool full = lynxFullScreen;
  int  zoom = (lynxZoomPercent > 0) ? lynxZoomPercent : 100;

  // nothing changed?
  if (zoom   == s_lastZoomPercent &&
      full   == s_lastFullScreen  &&
      srcW   == s_lastSrcW        &&
      srcH   == s_lastSrcH        &&
      lcdW   == s_lastLcdW        &&
      lcdH   == s_lastLcdH        &&
      s_xmap && s_ymap) {
    return;
  }

  // update cache
  s_lastZoomPercent = zoom;
  s_lastFullScreen  = full;
  s_lastSrcW        = srcW;
  s_lastSrcH        = srcH;
  s_lastLcdW        = lcdW;
  s_lastLcdH        = lcdH;

  float zoomFactor = zoom / 100.0f;
  if (zoomFactor <= 0.0f) zoomFactor = 1.0f;

  int   dstW, dstH;
  int   xOffset, yOffset;
  float baseInvScaleX, baseInvScaleY;

  if (!full) {
    // Mode original
    float scaleX = (float)lcdW / (float)srcW;
    float scaleY = (float)lcdH / (float)srcH;
    float scale  = (scaleX < scaleY) ? scaleX : scaleY;
    if (scale <= 0.0f) scale = 1.0f;

    dstW = (int)(srcW * scale);
    dstH = (int)(srcH * scale);
    if (dstW < 1) dstW = 1;
    if (dstH < 1) dstH = 1;

    xOffset = (lcdW - dstW) / 2;
    yOffset = (lcdH - dstH) / 2;
    if (xOffset < 0) xOffset = 0;
    if (yOffset < 0) yOffset = 0;

    baseInvScaleX = (float)srcW / (float)dstW;
    baseInvScaleY = (float)srcH / (float)dstH;

    M5Cardputer.Display.fillScreen(TFT_BLACK);
  } else {
    // Mode fullscreen
    dstW = lcdW;
    dstH = lcdH;
    xOffset = 0;
    yOffset = 0;

    baseInvScaleX = (float)srcW / (float)dstW;
    baseInvScaleY = (float)srcH / (float)dstH;
  }

  // apply zoom
  float invScaleX = baseInvScaleX / zoomFactor;
  float invScaleY = baseInvScaleY / zoomFactor;

  s_transform.dstW      = dstW;
  s_transform.dstH      = dstH;
  s_transform.xOffset   = xOffset;
  s_transform.yOffset   = yOffset;
  s_transform.invScaleX = invScaleX;
  s_transform.invScaleY = invScaleY;

  s_transform.srcCX = (float)srcW * 0.5f;
  s_transform.srcCY = (float)srcH * 0.5f;
  s_transform.dstCX = (float)(dstW - 1) * 0.5f;
  s_transform.dstCY = (float)(dstH - 1) * 0.5f;

  if (dstW > s_xmapCap) {
    if (s_xmap) free(s_xmap);
    s_xmap = (uint16_t*)malloc(dstW * sizeof(uint16_t));
    s_xmapCap = s_xmap ? dstW : 0;
  }
  if (dstH > s_ymapCap) {
    if (s_ymap) free(s_ymap);
    s_ymap = (uint16_t*)malloc(dstH * sizeof(uint16_t));
    s_ymapCap = s_ymap ? dstH : 0;
  }

  if (!s_xmap || !s_ymap) {
    return;
  }

  for (int x = 0; x < dstW; ++x) {
    int srcX = (int)(s_transform.srcCX + ((float)x - s_transform.dstCX) * s_transform.invScaleX);
    s_xmap[x] = (srcX < 0) ? 0 : ((srcX >= srcW) ? srcW - 1 : srcX);
  }
  for (int y = 0; y < dstH; ++y) {
    int srcY = (int)(s_transform.srcCY + ((float)y - s_transform.dstCY) * s_transform.invScaleY);
    s_ymap[y] = (srcY < 0) ? 0 : ((srcY >= srcH) ? srcH - 1 : srcY);
  }
}


// ================== TASK ==================

static void lynx_display_task(void *arg)
{
  (void)arg;

  M5Cardputer.Display.startWrite();

  for (;;) {
    LynxFrameMsg msg;
    if (xQueueReceive(s_frameQ, &msg, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    if (!msg.fb || msg.width <= 0 || msg.height <= 0) {
      continue;
    }

    int srcW = msg.width;
    int srcH = msg.height;

    lynx_display_compute_transform(srcW, srcH);

    int   dstW      = s_transform.dstW;
    int   dstH      = s_transform.dstH;
    int   xOffset   = s_transform.xOffset;
    int   yOffset   = s_transform.yOffset;

    if (dstW <= 0 || dstH <= 0 || !s_xmap || !s_ymap) {
      continue;
    }

    if (dstW > s_lineCap) {
      if (s_lineBuf) {
        free(s_lineBuf);
        s_lineBuf = nullptr;
      }
      s_lineBuf = (uint16_t*)heap_caps_malloc(
          dstW * sizeof(uint16_t),
          MALLOC_CAP_DMA | MALLOC_CAP_8BIT
      );
      s_lineCap = dstW;
    }
    if (!s_lineBuf) {
      continue;
    }

    for (int y = 0; y < dstH; ++y) {
      int srcY = s_ymap[y];
      const uint16_t* srcLine = msg.fb + srcY * srcW;

      for (int x = 0; x < dstW; ++x) {
        s_lineBuf[x] = srcLine[s_xmap[x]];
      }

      int dstY = yOffset + y;
      M5Cardputer.Display.setAddrWindow(xOffset, dstY, dstW, 1);
      M5Cardputer.Display.writePixels(s_lineBuf, dstW);
    }

    vTaskDelay(1);
  }

  M5Cardputer.Display.endWrite();
}

// ================== PUBLIC API ==================

extern "C" void lynx_display_init(void)
{
  M5Cardputer.Display.setSwapBytes(false);
  M5Cardputer.Display.fillScreen(TFT_BLACK);

  if (!s_frameQ) {
    s_frameQ = xQueueCreate(2, sizeof(LynxFrameMsg));
    if (!s_frameQ) {
      EMU_LOG("[LYNX-DISP] queue create failed\n");
    }
  }
}

extern "C" void lynx_display_start(void)
{
  if (!s_frameQ) {
    lynx_display_init();
  }

  if (!s_task && s_frameQ) {
    BaseType_t ok = xTaskCreatePinnedToCore(
      lynx_display_task,
      "LynxDisp",
      2048,
      nullptr,
      6,
      &s_task,
      0  // core 0
    );
    if (ok != pdPASS) {
      EMU_LOG("[LYNX-DISP] task create failed\n");
      if (s_task) {
        vTaskDelete(s_task);
      }
      s_task = nullptr;
    }
  }
}

extern "C" void lynx_display_stop(void)
{
  if (s_task) {
    vTaskDelete(s_task);
    s_task = nullptr;
  }
  if (s_frameQ) {
    vQueueDelete(s_frameQ);
    s_frameQ = nullptr;
  }
  if (s_lineBuf) {
    free(s_lineBuf);
    s_lineBuf = nullptr;
    s_lineCap = 0;
  }
  if (s_xmap) {
    free(s_xmap);
    s_xmap = nullptr;
    s_xmapCap = 0;
  }
  if (s_ymap) {
    free(s_ymap);
    s_ymap = nullptr;
    s_ymapCap = 0;
  }

  s_lastSrcW = 0;
  s_lastSrcH = 0;
  s_lastLcdW = 0;
  s_lastLcdH = 0;
  s_lastZoomPercent = -1;
  s_lastFullScreen = false;
}

extern "C" void lynx_display_submit_frame(const uint16_t *fb,
                                          int width,
                                          int height)
{
  if (!s_frameQ || !fb) return;

  LynxFrameMsg msg;
  msg.fb     = fb;
  msg.width  = width;
  msg.height = height;

  // non blocking
  BaseType_t ok = xQueueSend(s_frameQ, &msg, 0);
  (void)ok;
}
