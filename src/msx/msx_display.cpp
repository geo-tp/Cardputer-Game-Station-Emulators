#pragma GCC optimize ("Os")

#include "msx_host_internal.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "esp_heap_caps.h"

pixel* XPal = nullptr;
pixel* BPal = nullptr;
pixel XPal0;
pixel* XBuf = nullptr;

static bool g_lineRenderMode = false;
static pixel* g_lineBuf = nullptr;
static int g_prevSrcY = -1;
static bool g_lcdWriteOpen = false;

extern "C" void PutImage(void);
extern "C" {
#define NARROW
#define WIDTH 272
#define HEIGHT 228
int msx_line_render_enabled(void);
pixel* msx_line_begin(int srcY, pixel borderColor, int xOffset);
void msx_line_end_frame(void);
#include "fMSX/Common.h"
#undef NARROW
#undef WIDTH
#undef HEIGHT
}

namespace {

using namespace msx;

static bool ensure_video_tables()
{
    if (!XPal) XPal = (pixel*)std::malloc(80 * sizeof(pixel));
    if (!BPal) BPal = (pixel*)std::malloc(256 * sizeof(pixel));
    if (!g_host.rgb565) g_host.rgb565 = (uint16_t*)std::malloc(256 * sizeof(uint16_t));
    if (!g_host.xmapFull) g_host.xmapFull = (uint16_t*)std::malloc(kDstW * sizeof(uint16_t));
    if (!g_host.xmap43) g_host.xmap43 = (uint16_t*)std::malloc(kFit43W * sizeof(uint16_t));
    if (!g_host.ymap) g_host.ymap = (uint16_t*)std::malloc(kDstH * sizeof(uint16_t));
    if (!g_host.xmapZoom) g_host.xmapZoom = (uint16_t*)std::malloc(kDstW * sizeof(uint16_t));
    if (!g_host.ymapZoom) g_host.ymapZoom = (uint16_t*)std::malloc(kDstH * sizeof(uint16_t));
        if (!g_lineBuf) g_lineBuf = (pixel*)std::malloc(kSrcW * sizeof(pixel));
    return XPal && BPal && g_host.rgb565 && g_host.xmapFull && g_host.xmap43 &&
            g_host.ymap && g_host.xmapZoom && g_host.ymapZoom && g_lineBuf;
}

static void free_video_tables()
{
    std::free(XPal);
    std::free(BPal);
    std::free(g_host.rgb565);
    std::free(g_host.xmapFull);
    std::free(g_host.xmap43);
    std::free(g_host.ymap);
    std::free(g_host.xmapZoom);
    std::free(g_host.ymapZoom);
    std::free(g_lineBuf);
    XPal = nullptr;
    BPal = nullptr;
    g_host.rgb565 = nullptr;
    g_host.xmapFull = nullptr;
    g_host.xmap43 = nullptr;
    g_host.ymap = nullptr;
    g_host.xmapZoom = nullptr;
    g_host.ymapZoom = nullptr;
    g_lineBuf = nullptr;
}

static void rebuild_scalers()
{
    if (!g_host.xmapFull || !g_host.xmap43 || !g_host.ymap) return;
    for (int x = 0; x < kDstW; ++x) {
        g_host.xmapFull[x] = (uint16_t)((x * kSrcW) / kDstW);
    }

    for (int x = 0; x < kFit43W; ++x) {
        g_host.xmap43[x] = (uint16_t)((x * kSrcW) / kFit43W);
    }

    for (int y = 0; y < kDstH; ++y) {
        g_host.ymap[y] = (uint16_t)((y * kSrcH) / kDstH);
    }
}


static void rebuild_zoom_maps(int zp)
{
    if (!g_host.xmapZoom || !g_host.ymapZoom) return;
    if (zp <= 0) zp = 100;
    const int roiW = std::max(16, std::min(kSrcW, kSrcW * 100 / zp));
    const int roiH = std::max(16, std::min(kSrcH, kSrcH * 100 / zp));
    const int roiX0 = (kSrcW - roiW) / 2;
    const int roiY0 = (kSrcH - roiH) / 2;
    for (int x = 0; x < kDstW; ++x)
        g_host.xmapZoom[x] = (uint16_t)(roiX0 + (x * roiW) / kDstW);
    for (int y = 0; y < kDstH; ++y)
        g_host.ymapZoom[y] = (uint16_t)(roiY0 + (y * roiH) / kDstH);
    g_host.zoomMapBuiltFor = zp;
}

static inline void emit_black_dst_line(int dstY)
{
    std::fill_n(g_host.line565, kDstW, (uint16_t)0x0000);
    M5Cardputer.Display.pushImage(0, dstY, kDstW, 1, g_host.line565);
}

static void flush_src_line(int srcY, const pixel* srcLine)
{
    if (!g_lcdWriteOpen) {
        M5Cardputer.Display.startWrite();
        g_lcdWriteOpen = true;
    }

    if (g_host.fullscreen) {
        if (g_host.zoomMapBuiltFor != g_host.zoomPercent) rebuild_zoom_maps(g_host.zoomPercent);

        for (int y = 0; y < kDstH; ++y) {
            if (g_host.ymapZoom[y] != srcY) continue;

            if (!srcLine) {
                emit_black_dst_line(y);
                continue;
            }

            for (int x = 0; x < kDstW; ++x) {
                g_host.line565[x] = g_host.rgb565[srcLine[g_host.xmapZoom[x]]];
            }
            M5Cardputer.Display.pushImage(0, y, kDstW, 1, g_host.line565);
        }
    } else {
        constexpr int xOff = (kDstW - kFit43W) / 2;

        for (int y = 0; y < kDstH; ++y) {
            if (g_host.ymap[y] != srcY) continue;

            if (!srcLine) {
                emit_black_dst_line(y);
                continue;
            }

            std::fill_n(g_host.line565, kDstW, (uint16_t)0x0000);
            for (int x = 0; x < kFit43W; ++x) {
                g_host.line565[xOff + x] = g_host.rgb565[srcLine[g_host.xmap43[x]]];
            }
            M5Cardputer.Display.pushImage(0, y, kDstW, 1, g_host.line565);
        }
    }
}

static void render_frame_from_src(const uint8_t* src)
{
    if (!src || !g_host.line565 || !g_host.rgb565) return;

    M5Cardputer.Display.startWrite();

    if (g_host.fullscreen) {
        if (g_host.zoomMapBuiltFor != g_host.zoomPercent)
            rebuild_zoom_maps(g_host.zoomPercent);
        for (int y = 0; y < kDstH; ++y) {
            const uint8_t* row = src + g_host.ymapZoom[y] * kSrcW;
            for (int x = 0; x < kDstW; ++x)
                g_host.line565[x] = g_host.rgb565[row[g_host.xmapZoom[x]]];
            M5Cardputer.Display.pushImage(0, y, kDstW, 1, g_host.line565);
        }
    } else {
        constexpr int xOff = (kDstW - kFit43W) / 2;
        for (int y = 0; y < kDstH; ++y) {
            const uint8_t* row = src + g_host.ymap[y] * kSrcW;
            std::fill_n(g_host.line565, kDstW, (uint16_t)0x0000);
            for (int x = 0; x < kFit43W; ++x)
                g_host.line565[xOff + x] = g_host.rgb565[row[g_host.xmap43[x]]];
            M5Cardputer.Display.pushImage(0, y, kDstW, 1, g_host.line565);
        }
    }

    M5Cardputer.Display.endWrite();
}

extern "C" int msx_line_render_enabled(void)
{
    return g_lineRenderMode ? 1 : 0;
}

extern "C" pixel* msx_line_begin(int srcY, pixel borderColor, int xOffset)
{
    if (!g_lineRenderMode || !g_lineBuf) return nullptr;

    if (g_prevSrcY < 0) {
        for (int y = 0; y < srcY; ++y) flush_src_line(y, nullptr);
    } else {
        flush_src_line(g_prevSrcY, g_lineBuf);
    }

    std::fill_n(g_lineBuf, kSrcW, borderColor);
    g_prevSrcY = srcY;

    if (xOffset < 0) xOffset = 0;
    if (xOffset >= kSrcW) xOffset = kSrcW - 1;
    return g_lineBuf + xOffset;
}

extern "C" void msx_line_end_frame(void)
{
    if (!g_lineRenderMode) return;

    if (g_prevSrcY >= 0) {
        flush_src_line(g_prevSrcY, g_lineBuf);
        for (int y = g_prevSrcY + 1; y < kSrcH; ++y) flush_src_line(y, nullptr);
    }

    if (g_lcdWriteOpen) {
        M5Cardputer.Display.endWrite();
        g_lcdWriteOpen = false;
    }

    g_prevSrcY = -1;
}

} // namespace

extern "C" void SetColor(byte N, byte R, byte G, byte B)
{
    if (!ensure_video_tables()) return;

    const uint16_t c = msx::rgb565(R, G, B);

    if (N < 80) {
        XPal[N] = (pixel)N;
        msx::g_host.rgb565[N] = c;
    }
}

extern "C" int InitMachine(void)
{
    if (!ensure_video_tables()) {
        printf("[MSX][ERR] Video table allocation failed\n");
        return 0;
    }

    rebuild_scalers();
    XBuf = nullptr;
    XPal0 = 0;

    for (int i = 0; i < 80; ++i) {
        XPal[i] = (pixel)i;
    }

    for (int i = 0; i < 256; ++i) {
        const unsigned r = ((i >> 2) & 0x07) * 255 / 7;
        const unsigned g = ((i >> 5) & 0x07) * 255 / 7;
        const unsigned b = (i & 0x03) * 255 / 3;
        BPal[i] = (pixel)i;
        msx::g_host.rgb565[i] = msx::rgb565(r, g, b);
    }

    return 1;
}

extern "C" int msx_host_prepare_runtime(void)
{
    using namespace msx;

    if (!ensure_video_tables()) {
        printf("[MSX][ERR] Video table allocation failed\n");
        return 0;
    }

    g_lineRenderMode = ((g_host.modelMode & MSX_MODEL) == MSX_MSX2);
    g_prevSrcY = -1;
    g_lcdWriteOpen = false;

    if (!g_lineRenderMode && !g_host.frame8) {
        g_host.frame8 = (uint8_t*)heap_caps_malloc(
            kSrcW * kSrcH,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
        );
    }

    if (!g_host.line565) {
        g_host.line565 = (uint16_t*)heap_caps_malloc(
            kDstW * sizeof(uint16_t),
            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT
        );
    }

    if ((!g_lineRenderMode && !g_host.frame8) || !g_host.line565) {
        printf("[MSX][ERR] Host video buffer allocation failed\n");
        return 0;
    }

    if (!g_lineRenderMode) {
        std::memset(g_host.frame8, 0, kSrcW * kSrcH);
        XBuf = (pixel*)g_host.frame8;
    } else {
        XBuf = nullptr;
    }

    if (!InitSound(kAudioRate, 120)) {
        printf("[MSX][ERR] InitSound failed\n");
        return 0;
    }

    return 1;
}

extern "C" void TrashMachine(void)
{
    TrashSound();

    std::free(msx::g_host.frame8);
    std::free(msx::g_host.line565);
    msx::g_host.frame8 = nullptr;
    msx::g_host.line565 = nullptr;
    XBuf = nullptr;

    free_video_tables();
}

extern "C" void PutImage(void)
{
    using namespace msx;

    const uint8_t* src = g_host.frame8;
    if (!src || !g_host.line565 || !g_host.rgb565) return;

    render_frame_from_src(src);
}
