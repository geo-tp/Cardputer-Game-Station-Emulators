#pragma GCC optimize ("Os")

#include "display.h"
#include <M5Cardputer.h>
#include <M5Unified.h>
#include <algorithm>
#include <string.h>

extern "C" {
  #include "sms/smsplus/shared.h"
  #include "sms/smsplus/vdp.h"
#include "share/emu_log_cpp.h"
}

bool fullscreen = true;
bool scanline   = true;
int smsZoomPercent = 110;
static constexpr int LCD_W = 240;
static constexpr int LCD_H = 135;
int srcW, srcH, srcX0, srcY0;
static int dstW, dstH, offX, offY;

// Buffers
uint16_t* lineBuf = nullptr;
uint16_t* xmap    = nullptr;
uint16_t* ymap    = nullptr;
uint16_t* sms_palette_565 = nullptr;

static inline uint16_t rgb888_to_565(uint32_t c){
  uint8_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

// Palette fixe SMS
static const uint32_t sms_palette_rgb[256] __attribute__((aligned(4))) = {
  0x00000000,0x00000048,0x000000B4,0x000000FF,0x00002400,0x00002448,0x000024B4,0x000024FF,
  0x00004800,0x00004848,0x000048B4,0x000048FF,0x00006C00,0x00006C48,0x00006CB4,0x00006CFF,
  0x00009000,0x00009048,0x000090B4,0x000090FF,0x0000B400,0x0000B448,0x0000B4B4,0x0000B4FF,
  0x0000D800,0x0000D848,0x0000D8B4,0x0000D8FF,0x0000FF00,0x0000FF48,0x0000FFB4,0x0000FFFF,
  0x00240000,0x00240048,0x002400B4,0x002400FF,0x00242400,0x00242448,0x002424B4,0x002424FF,
  0x00244800,0x00244848,0x002448B4,0x002448FF,0x00246C00,0x00246C48,0x00246CB4,0x00246CFF,
  0x00249000,0x00249048,0x002490B4,0x002490FF,0x0024B400,0x0024B448,0x0024B4B4,0x0024B4FF,
  0x0024D800,0x0024D848,0x0024D8B4,0x0024D8FF,0x0024FF00,0x0024FF48,0x0024FFB4,0x0024FFFF,
  0x00480000,0x00480048,0x004800B4,0x004800FF,0x00482400,0x00482448,0x004824B4,0x004824FF,
  0x00484800,0x00484848,0x004848B4,0x004848FF,0x00486C00,0x00486C48,0x00486CB4,0x00486CFF,
  0x00489000,0x00489048,0x004890B4,0x004890FF,0x0048B400,0x0048B448,0x0048B4B4,0x0048B4FF,
  0x0048D800,0x0048D848,0x0048D8B4,0x0048D8FF,0x0048FF00,0x0048FF48,0x0048FFB4,0x0048FFFF,
  0x006C0000,0x006C0048,0x006C00B4,0x006C00FF,0x006C2400,0x006C2448,0x006C24B4,0x006C24FF,
  0x006C4800,0x006C4848,0x006C48B4,0x006C48FF,0x006C6C00,0x006C6C48,0x006C6CB4,0x006C6CFF,
  0x006C9000,0x006C9048,0x006C90B4,0x006C90FF,0x006CB400,0x006CB448,0x006CB4B4,0x006CB4FF,
  0x006CD800,0x006CD848,0x006CD8B4,0x006CD8FF,0x006CFF00,0x006CFF48,0x006CFFB4,0x006CFFFF,
  0x00900000,0x00900048,0x009000B4,0x009000FF,0x00902400,0x00902448,0x009024B4,0x009024FF,
  0x00904800,0x00904848,0x009048B4,0x009048FF,0x00906C00,0x00906C48,0x00906CB4,0x00906CFF,
  0x00909000,0x00909048,0x009090B4,0x009090FF,0x0090B400,0x0090B448,0x0090B4B4,0x0090B4FF,
  0x0090D800,0x0090D848,0x0090D8B4,0x0090D8FF,0x0090FF00,0x0090FF48,0x0090FFB4,0x0090FFFF,
  0x00B40000,0x00B40048,0x00B400B4,0x00B400FF,0x00B42400,0x00B42448,0x00B424B4,0x00B424FF,
  0x00B44800,0x00B44848,0x00B448B4,0x00B448FF,0x00B46C00,0x00B46C48,0x00B46CB4,0x00B46CFF,
  0x00B49000,0x00B49048,0x00B490B4,0x00B490FF,0x00B4B400,0x00B4B448,0x00B4B4B4,0x00B4B4FF,
  0x00B4D800,0x00B4D848,0x00B4D8B4,0x00B4D8FF,0x00B4FF00,0x00B4FF48,0x00B4FFB4,0x00B4FFFF,
  0x00D80000,0x00D80048,0x00D800B4,0x00D800FF,0x00D82400,0x00D82448,0x00D824B4,0x00D824FF,
  0x00D84800,0x00D84848,0x00D848B4,0x00D848FF,0x00D86C00,0x00D86C48,0x00D86CB4,0x00D86CFF,
  0x00D89000,0x00D89048,0x00D890B4,0x00D890FF,0x00D8B400,0x00D8B448,0x00D8B4B4,0x00D8B4FF,
  0x00D8D800,0x00D8D848,0x00D8D8B4,0x00D8D8FF,0x00D8FF00,0x00D8FF48,0x00D8FFB4,0x00D8FFFF,
  0x00FF0000,0x00FF0048,0x00FF00B4,0x00FF00FF,0x00FF2400,0x00FF2448,0x00FF24B4,0x00FF24FF,
  0x00FF4800,0x00FF4848,0x00FF48B4,0x00FF48FF,0x00FF6C00,0x00FF6C48,0x00FF6CB4,0x00FF6CFF,
  0x00FF9000,0x00FF9048,0x00FF90B4,0x00FF90FF,0x00FFB400,0x00FFB448,0x00FFB4B4,0x00FFB4FF,
  0x00FFD800,0x00FFD848,0x00FFD8B4,0x00FFD8FF,0x00FFFF00,0x00FFFF48,0x00FFFFB4,0x00FFFFFF,
};

void sms_display_init() {
  M5.Display.setSwapBytes(true);

  free(lineBuf);
  free(xmap);
  free(ymap);
  free(sms_palette_565);
  vdp_init_vram();

  // Alloc
  lineBuf         = (uint16_t*)malloc(LCD_W * sizeof(uint16_t));
  xmap            = (uint16_t*)malloc(LCD_W * sizeof(uint16_t));
  ymap            = (uint16_t*)malloc(LCD_H * sizeof(uint16_t));
  sms_palette_565 = (uint16_t*)malloc(256   * sizeof(uint16_t));

  if (!lineBuf || !xmap || !ymap || !sms_palette_565) {
    EMU_LOG("Erreur d'allocation display buffers !\n");
    while (true) delay(100);
  }

  M5.Display.fillScreen(TFT_BLACK);
  EMU_LOG("Display init: %dx%d OK\n", LCD_W, LCD_H);
}

void sms_palette_init_fixed(){
  for (int i=0;i<256;i++)
    sms_palette_565[i] = rgb888_to_565(sms_palette_rgb[i]);
}

static inline void compute_common(bool fullscreenMode){
  const bool isGG = (cart.type == TYPE_GG);
  srcW  = isGG ? 160 : 256;
  srcH  = isGG ? 144 : 192;
  srcX0 = isGG ? 48  : 0;
  srcY0 = isGG ? 24  : 0;

  M5.Display.setSwapBytes(true);
  M5.Display.fillScreen(TFT_BLACK);

  // --- base scaling ---
  if (fullscreenMode) {
    dstW = LCD_W; dstH = LCD_H; offX = 0; offY = 0;
  } else {
    const float sx = (float)LCD_W / (float)srcW;
    const float sy = (float)LCD_H / (float)srcH;
    const float s  = (sx < sy) ? sx : sy;
    dstW = std::max(1, (int)(srcW * s));
    dstH = std::max(1, (int)(srcH * s));
    offX = (LCD_W - dstW) / 2;
    offY = (LCD_H - dstH) / 2;
  }

  //ROI (zoom)
  int zp = (smsZoomPercent <= 0) ? 100 : smsZoomPercent;
  int roiW = srcW * 100 / zp;
  int roiH = srcH * 100 / zp;
  roiW = std::min(srcW, std::max(16, roiW));
  roiH = std::min(srcH, std::max(16, roiH));
  const int roiX0 = srcX0 + (srcW - roiW) / 2;
  const int roiY0 = srcY0 + (srcH - roiH) / 2;

  // mapping (xmap / ymap)
  for (int x = 0; x < dstW; ++x)
    xmap[x] = (uint16_t)(roiX0 + ((x * roiW) / dstW));

  for (int y = 0; y < dstH; ++y)
    ymap[y] = (uint16_t)(roiY0 + ((y * roiH) / dstH));
}

void video_compute_scaler_full()   { compute_common(true);  }
void video_compute_scaler_square() { compute_common(false); }

void sms_display_write_frame() {
  M5.Display.startWrite();

  M5.Display.setAddrWindow(offX, offY, dstW, dstH);

  const uint16_t* pal = sms_palette_565;
  const uint16_t* sx  = xmap;

  int last_sy = -1;

  for (int y = 0; y < dstH; ++y) {
    const int sy = ymap[y];

    if (sy != last_sy) {
      const uint8_t* srcLine = bitmap.data + sy * bitmap.pitch;
      uint16_t* out = lineBuf;

      int x = 0;
      for (; x + 8 <= dstW; x += 8) {
        out[x + 0] = pal[srcLine[sx[x + 0]]];
        out[x + 1] = pal[srcLine[sx[x + 1]]];
        out[x + 2] = pal[srcLine[sx[x + 2]]];
        out[x + 3] = pal[srcLine[sx[x + 3]]];
        out[x + 4] = pal[srcLine[sx[x + 4]]];
        out[x + 5] = pal[srcLine[sx[x + 5]]];
        out[x + 6] = pal[srcLine[sx[x + 6]]];
        out[x + 7] = pal[srcLine[sx[x + 7]]];
      }
      for (; x < dstW; ++x) out[x] = pal[srcLine[sx[x]]];

      last_sy = sy;
    }
      M5.Display.writePixels(lineBuf, dstW, true);
  }

  M5.Display.endWrite();
}

void sms_display_clear() {
  M5.Lcd.fillScreen(TFT_BLACK);
}
