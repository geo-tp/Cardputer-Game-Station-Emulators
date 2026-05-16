// ngc_display.cpp

#include "ngc_display.h"
#include <M5Cardputer.h>
#include <string.h>
#include <atomic>
#include "share/emu_log_cpp.h"

// Framebuffer NGPC 160x152
int ngpZoomPercent = 100;
bool ngpFullscreen =  true;
volatile unsigned g_frame_ready = 0;
volatile unsigned g_frame_counter = 0;
bool s_lastScreenMode = ngpFullscreen;

// LUT state
static bool s_lut_ready = false;

// Entrelacement odd/even
bool s_interlace_parity = false;

// Color conversion LUT
extern uint16_t* totalpalette;

static uint16_t* s_linebuf_panel = nullptr;
static uint16_t* s_lut_x_full    = nullptr;
static uint16_t* s_lut_y_full    = nullptr;
static uint16_t* s_lut_x_4x3     = nullptr;
static uint16_t* s_fb            = nullptr;
#ifdef NGP_ONLY_RENDER_VISIBLE_LINES
uint8_t* s_lut_y_render    = nullptr; // 152 element array of boolean render status for each line
#endif
unsigned short *drawBuffer = s_fb; 

extern "C" void ngc_display_init(void)
{
  const size_t fbBytes = (size_t)NGPC_W * (size_t)NGPC_H * sizeof(uint16_t);

  // --- Framebuffer ---
  if (!s_fb) {
    s_fb = (uint16_t*)heap_caps_malloc(fbBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_fb) s_fb = (uint16_t*)malloc(fbBytes);
    if (s_fb) memset(s_fb, 0, fbBytes);
    drawBuffer = s_fb;
  }

  // --- Buffers dynamiques ---
  if (!s_linebuf_panel)
    s_linebuf_panel = (uint16_t*)heap_caps_malloc(240 * sizeof(uint16_t),
                                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (!s_lut_x_full)
    s_lut_x_full = (uint16_t*)heap_caps_malloc(240 * sizeof(uint16_t),
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (!s_lut_y_full)
    s_lut_y_full = (uint16_t*)heap_caps_malloc(135 * sizeof(uint16_t),
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

  if (!s_lut_x_4x3)
    s_lut_x_4x3 = (uint16_t*)heap_caps_malloc(180 * sizeof(uint16_t),
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

#ifdef NGP_ONLY_RENDER_VISIBLE_LINES
  if (!s_lut_y_render)
    s_lut_y_render = (uint8_t*)heap_caps_malloc(152 * sizeof(uint8_t),
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif

  M5.Display.setSwapBytes(true);
  M5.Display.fillScreen(TFT_BLACK);

#ifdef NGP_TRACE_LOGS
  EMU_LOG("[NGP][DISP] init fb=%p line=%p lutXFull=%p lutYFull=%p lutX4x3=%p",
          s_fb, s_linebuf_panel, s_lut_x_full, s_lut_y_full, s_lut_x_4x3);
#ifdef NGP_ONLY_RENDER_VISIBLE_LINES
  EMU_LOG(" lutYRender=%p", s_lut_y_render);
#endif
  EMU_LOG(" heap=%u largest8=%u\n",
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
#endif
}

extern "C" void ngc_display_set_parity(bool odd) {
  s_interlace_parity = odd;
}

static void build_scale_luts()
{
  const int panelW = M5.Display.width();   // 240
  const int panelH = M5.Display.height();  // 135
  const int srcW   = NGPC_W;               // 160
  const int srcH   = NGPC_H;               // 152

  // Fullscreen
  for (int x = 0; x < panelW; ++x) {
    uint32_t sx = ((uint32_t)x * (uint32_t)srcW) / (uint32_t)panelW;
    s_lut_x_full[x] = (uint16_t)((sx < (uint32_t)srcW ? sx : (uint32_t)(srcW-1)) << 1);
  }
  for (int y = 0; y < panelH; ++y) {
    uint32_t sy = ((uint32_t)y * (uint32_t)srcH) / (uint32_t)panelH;
    s_lut_y_full[y] = (uint16_t)((sy < (uint32_t)srcH ? sy : (uint32_t)(srcH-1)));
  }

  // 4:3 aspect ratio
  const int outH   = panelH;              // 135
  const int outW   = (outH * 4) / 3;      // 180
  const int virtW  = (srcH * 4) / 3;      // 202
  const int virtOff= (virtW - srcW) / 2;  // 21

  for (int x = 0; x < outW; ++x) {
    int virtX = ((int32_t)x * virtW) / outW;
    int srcX  = virtX - virtOff;
    s_lut_x_4x3[x] = (unsigned)srcX < (unsigned)srcW ? (uint16_t)srcX : 0;
  }

#ifdef NGP_ONLY_RENDER_VISIBLE_LINES
  // reset all lines to 0
  for (int y = 0; y < srcH; y++)
    s_lut_y_render[y]=0;

  // toggle only visible lines
  for (int y = 0; y < outH; y++)
    s_lut_y_render[s_lut_y_full[y]]=1;
#endif

  s_lut_ready = true;

#ifdef NGP_TRACE_LOGS
#ifdef NGP_ONLY_RENDER_VISIBLE_LINES
  int visibleLines = 0;
  for (int y = 0; y < srcH; y++) {
    if (s_lut_y_render[y]) visibleLines++;
  }
#else
  const int visibleLines = srcH;
#endif
  EMU_LOG("[NGP][DISP] luts mode=%s panel=%dx%d src=%dx%d visibleLines=%d y0=%u yMid=%u yLast=%u x0=%u xMid=%u xLast=%u\n",
          ngpFullscreen ? "fullscreen" : "4:3",
          panelW, panelH, srcW, srcH, visibleLines,
          (unsigned)s_lut_y_full[0], (unsigned)s_lut_y_full[panelH / 2],
          (unsigned)s_lut_y_full[panelH - 1],
          (unsigned)(s_lut_x_full[0] >> 1), (unsigned)(s_lut_x_full[panelW / 2] >> 1),
          (unsigned)(s_lut_x_full[panelW - 1] >> 1));
#endif
}

//static inline IRAM_ATTR void paint_fullscreen_stretch()
static inline void paint_fullscreen_stretch()
{
  const int panelW = 240;
  const int panelH = 135;
  const int srcW   = NGPC_W;

  M5.Display.startWrite();
  
  #ifdef NGP_INTERLACED
  const int parity = (int)s_interlace_parity;
  for (int y = parity; y < panelH; y += 2) {
  #else
  for (int y = 0; y < panelH; y += 1) {
  #endif
    const uint16_t  srcY    = s_lut_y_full[y];                         // 0..(srcH-1)
    const uint16_t* srcLine = drawBuffer + (size_t)srcY * srcW;        // source
    const uint8_t*  base    = (const uint8_t*)srcLine;                 // base 8-bit
    const uint16_t* lutx    = s_lut_x_full;                            // offsets
    uint16_t*       dst     = s_linebuf_panel;
    // Unrolled loop for speed
    int x = 0;
    for (; x <= panelW - 8; x += 8) {
  #ifdef  NGP_USE_THREADED_COLORMAPPING
        // Use the totalpalette LUT for color conversion
        dst[x+0] = totalpalette[*(const uint16_t*)(base + lutx[x+0])];
        dst[x+1] = totalpalette[*(const uint16_t*)(base + lutx[x+1])];
        dst[x+2] = totalpalette[*(const uint16_t*)(base + lutx[x+2])];
        dst[x+3] = totalpalette[*(const uint16_t*)(base + lutx[x+3])];
        dst[x+4] = totalpalette[*(const uint16_t*)(base + lutx[x+4])];
        dst[x+5] = totalpalette[*(const uint16_t*)(base + lutx[x+5])];
        dst[x+6] = totalpalette[*(const uint16_t*)(base + lutx[x+6])];
        dst[x+7] = totalpalette[*(const uint16_t*)(base + lutx[x+7])];
  #else
      // Color conversion was already performed
      dst[x+0] = *(const uint16_t*)(base + lutx[x+0]);
      dst[x+1] = *(const uint16_t*)(base + lutx[x+1]);
      dst[x+2] = *(const uint16_t*)(base + lutx[x+2]);
      dst[x+3] = *(const uint16_t*)(base + lutx[x+3]);
      dst[x+4] = *(const uint16_t*)(base + lutx[x+4]);
      dst[x+5] = *(const uint16_t*)(base + lutx[x+5]);
      dst[x+6] = *(const uint16_t*)(base + lutx[x+6]);
      dst[x+7] = *(const uint16_t*)(base + lutx[x+7]);
#endif
    }
    for (; x < panelW; ++x) {
#ifdef NGP_USE_THREADED_COLORMAPPING
      // Use the totalpalette LUT for color conversion
      dst[x] = totalpalette[*(const uint16_t*)(base + lutx[x])];
#else
      // Color conversion was already performed
      dst[x] = *(const uint16_t*)(base + lutx[x]);
#endif
    }

    M5.Display.setAddrWindow(0, y, panelW, 1);
    M5.Display.writePixels(dst, panelW, /*swapBytesAlready=*/true);
  }

  M5.Display.endWrite();
}

static inline IRAM_ATTR void paint_fullheight_4x3()
{
  const int panelW = 240;
  const int panelH = 135;
  const int outW    = 160;
  const int x_start = (panelW - outW) / 2; // 40
  const int srcW   = NGPC_W;

  M5.Display.startWrite();
  
  #ifdef NGP_INTERLACED
  const int parity = (int)s_interlace_parity;
  for (int y = parity; y < panelH; y += 2) {
  #else
  for (int y = 0; y < panelH; y += 1) {
  #endif
    const uint16_t  srcY    = s_lut_y_full[y];                         // 0..(srcH-1)
    const uint16_t* srcLine = drawBuffer + (size_t)srcY * srcW;        // source
    const uint8_t*  base    = (const uint8_t*)srcLine;                 // base 8-bi
    uint16_t*       dst     = s_linebuf_panel;
    // Unrolled loop for speed
    int x = 0;
    for (; x <= 160 - 8; x += 8) {
  #ifdef  NGP_USE_THREADED_COLORMAPPING
        // Use the totalpalette LUT for color conversion
      dst[x+0] = totalpalette[*(const uint16_t*)(base + ((x+0) << 1))];
      dst[x+1] = totalpalette[*(const uint16_t*)(base + ((x+1) << 1))];
      dst[x+2] = totalpalette[*(const uint16_t*)(base + ((x+2) << 1))];
      dst[x+3] = totalpalette[*(const uint16_t*)(base + ((x+3) << 1))];
      dst[x+4] = totalpalette[*(const uint16_t*)(base + ((x+4) << 1))];
      dst[x+5] = totalpalette[*(const uint16_t*)(base + ((x+5) << 1))];
      dst[x+6] = totalpalette[*(const uint16_t*)(base + ((x+6) << 1))];
      dst[x+7] = totalpalette[*(const uint16_t*)(base + ((x+7) << 1))];
  #else
      // Color conversion was already performed, copy directly from source to line buffer(or use source buffer directly in next version)
      dst[x+0] = *(const uint16_t*)(base + ((x+0) << 1));
      dst[x+1] = *(const uint16_t*)(base + ((x+1) << 1));
      dst[x+2] = *(const uint16_t*)(base + ((x+2) << 1));
      dst[x+3] = *(const uint16_t*)(base + ((x+3) << 1));
      dst[x+4] = *(const uint16_t*)(base + ((x+4) << 1));
      dst[x+5] = *(const uint16_t*)(base + ((x+5) << 1));
      dst[x+6] = *(const uint16_t*)(base + ((x+6) << 1));
      dst[x+7] = *(const uint16_t*)(base + ((x+7) << 1));
#endif
    }
    for (; x < 160; ++x) {
#ifdef NGP_USE_THREADED_COLORMAPPING
      // Use the totalpalette LUT for color conversion
      dst[x] = totalpalette[*(const uint16_t*)(base + (x << 1))];
#else
      // Color conversion was already performed
      dst[x] = *(const uint16_t*)(base + (x << 1));
#endif
    }

    M5.Display.setAddrWindow(x_start, y, outW, 1);
    M5.Display.writePixels(dst, outW, /*swapBytesAlready=*/true);
  }

  M5.Display.endWrite();
}

extern "C" IRAM_ATTR void graphics_paint(unsigned char render)
{
  if (!render || !drawBuffer) return;
  if (!s_lut_ready) build_scale_luts();

#ifdef NGP_TRACE_LOGS
  const unsigned paintNo = g_frame_counter + 1;
  if (paintNo <= 5 || (paintNo % 60) == 0) {
    const uint16_t raw0 = drawBuffer[0];
    const uint16_t raw1 = drawBuffer[80];
    const uint16_t raw2 = drawBuffer[(size_t)76 * NGPC_W + 80];
    const uint16_t raw3 = drawBuffer[(size_t)(NGPC_H - 1) * NGPC_W + (NGPC_W - 1)];
    const uint16_t lcd0 = (totalpalette && raw0 < 4096) ? totalpalette[raw0] : raw0;
    const uint16_t lcd1 = (totalpalette && raw1 < 4096) ? totalpalette[raw1] : raw1;
    const uint16_t lcd2 = (totalpalette && raw2 < 4096) ? totalpalette[raw2] : raw2;
    EMU_LOG("[NGP][PAINT %u] render=%u mode=%s parity=%u fb=%p totalpal=%p raw=%04X/%04X/%04X/%04X lcd=%04X/%04X/%04X ready=%u\n",
            paintNo, (unsigned)render, ngpFullscreen ? "fullscreen" : "4:3",
            (unsigned)s_interlace_parity, drawBuffer, totalpalette,
            raw0, raw1, raw2, raw3, lcd0, lcd1, lcd2, (unsigned)g_frame_ready);
  }
#endif
  
  if (s_lastScreenMode && !ngpFullscreen) {
    M5.Display.fillScreen(TFT_BLACK);
  }

  s_lastScreenMode = ngpFullscreen;
  
  if (ngpFullscreen) {
    paint_fullscreen_stretch();
  } else {
    paint_fullheight_4x3();
  }

  
#ifndef NGP_HW_INTERLACED
  // alternate
   s_interlace_parity = !s_interlace_parity; // parity is set by tlcs_execute() if using NGP_HW_INTERLACED
#endif

  g_frame_counter++;
}
