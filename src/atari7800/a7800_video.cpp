#include "a7800_video.h"

#include <Arduino.h>
#include <M5Cardputer.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "share/emu_log_cpp.h"

static constexpr int kA7800PalWideVisibleHeight = 240;

bool a7800FullScreen = true;
int a7800ZoomPercent = 110;

static double s_targetFps = 60.0;
static float s_aspectRatio = 4.0f / 3.0f;
static unsigned s_baseWidth = 320;
static unsigned s_baseHeight = 223;

static uint16_t* s_lineBuf = nullptr;
static int s_lineCap = 0;

static constexpr int kVideoBatchLines = 8;

static int16_t* s_xmap = nullptr;
static int s_xmapCap = 0;
static int16_t* s_ymap = nullptr;
static int s_ymapCap = 0;

static int s_lastSrcW = -1;
static int s_lastSrcH = -1;
static int s_lastDstW = -1;
static int s_lastDstH = -1;
static int s_lastXOff = -1;
static int s_lastYOff = -1;
static int s_lastSrcX0 = -1;
static int s_lastSrcY0 = -1;
static int s_lastRoiW = -1;
static int s_lastRoiH = -1;
static bool s_lastFull = false;
static int s_lastZoom = -1;
static bool s_skipFrame = false;

static int64_t  s_videoTotalUs = 0;
static uint32_t s_videoCount   = 0;

static QueueHandle_t s_frameQ = nullptr;
static TaskHandle_t s_displayTask = nullptr;

struct A7800RenderPlan {
    int srcX0;
    int srcY0;
    int roiW;
    int roiH;
    int dstW;
    int dstH;
    int xOff;
    int yOff;
};

struct A7800FrameMsg {
    const void* frame;
    unsigned width;
    unsigned height;
    size_t pitch;
    const uint16_t* palette565;
    bool indexed;
    bool isPal;
};

static int clampi(int value, int minValue, int maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static void a7800_clear_target(void)
{
    M5Cardputer.Display.fillScreen(TFT_BLACK);
}

static void a7800_reset_layout_cache(void)
{
    s_lastSrcW = -1;
    s_lastSrcH = -1;
    s_lastDstW = -1;
    s_lastDstH = -1;
    s_lastXOff = -1;
    s_lastYOff = -1;
    s_lastSrcX0 = -1;
    s_lastSrcY0 = -1;
    s_lastRoiW = -1;
    s_lastRoiH = -1;
    s_lastFull = false;
    s_lastZoom = -1;
}

static bool a7800_layout_changed(const A7800RenderPlan& plan, int srcW, int srcH)
{
    const bool changed =
        s_lastSrcW != srcW ||
        s_lastSrcH != srcH ||
        s_lastDstW != plan.dstW ||
        s_lastDstH != plan.dstH ||
        s_lastXOff != plan.xOff ||
        s_lastYOff != plan.yOff ||
        s_lastSrcX0 != plan.srcX0 ||
        s_lastSrcY0 != plan.srcY0 ||
        s_lastRoiW != plan.roiW ||
        s_lastRoiH != plan.roiH ||
        s_lastFull != a7800FullScreen ||
        s_lastZoom != a7800ZoomPercent;

    s_lastSrcW = srcW;
    s_lastSrcH = srcH;
    s_lastDstW = plan.dstW;
    s_lastDstH = plan.dstH;
    s_lastXOff = plan.xOff;
    s_lastYOff = plan.yOff;
    s_lastSrcX0 = plan.srcX0;
    s_lastSrcY0 = plan.srcY0;
    s_lastRoiW = plan.roiW;
    s_lastRoiH = plan.roiH;
    s_lastFull = a7800FullScreen;
    s_lastZoom = a7800ZoomPercent;

    return changed;
}

static void a7800_compute_plan(int srcW, int srcH, bool isPal, A7800RenderPlan& plan)
{
    const int targetW = M5Cardputer.Display.width();
    const int targetH = M5Cardputer.Display.height();

    if (a7800FullScreen) {
        const int zoom = clampi(a7800ZoomPercent, 100, 150);
        plan.roiW = clampi((srcW * 100) / zoom, 32, srcW);
        plan.roiH = clampi((srcH * 100) / zoom, 32, srcH);
        plan.srcX0 = (srcW - plan.roiW) / 2;
        plan.srcY0 = (srcH - plan.roiH) / 2;
        plan.dstW = targetW;
        plan.dstH = targetH;
        plan.xOff = 0;
        plan.yOff = 0;
        return;
    }

    plan.srcX0 = 0;
    plan.roiW = srcW;
    plan.roiH = isPal ? clampi(kA7800PalWideVisibleHeight, 64, srcH) : srcH;
    plan.srcY0 = (srcH - plan.roiH) / 2;

    float scaleX = (float)targetW / (float)plan.roiW;
    float scaleY = (float)targetH / (float)plan.roiH;
    float scale = (scaleX < scaleY) ? scaleX : scaleY;
    if (scale <= 0.0f) {
        scale = 1.0f;
    }

    plan.dstW = clampi((int)(plan.roiW * scale), 1, targetW);
    plan.dstH = clampi((int)(plan.roiH * scale), 1, targetH);
    plan.xOff = (targetW - plan.dstW) / 2;
    plan.yOff = (targetH - plan.dstH) / 2;
}

static bool a7800_prepare_luts(const A7800RenderPlan& plan)
{
    if (plan.dstW > s_xmapCap) {
        free(s_xmap);
        s_xmap = (int16_t*)malloc((size_t)plan.dstW * sizeof(int16_t));
        s_xmapCap = s_xmap ? plan.dstW : 0;
    }

    if (plan.dstH > s_ymapCap) {
        free(s_ymap);
        s_ymap = (int16_t*)malloc((size_t)plan.dstH * sizeof(int16_t));
        s_ymapCap = s_ymap ? plan.dstH : 0;
    }

    if (plan.dstW > s_lineCap) {
        free(s_lineBuf);
        s_lineBuf = (uint16_t*)heap_caps_malloc(
            (size_t)plan.dstW * kVideoBatchLines * sizeof(uint16_t),
            MALLOC_CAP_DMA | MALLOC_CAP_8BIT
        );
        if (!s_lineBuf) {
            s_lineBuf = (uint16_t*)malloc((size_t)plan.dstW * kVideoBatchLines * sizeof(uint16_t));
        }
        s_lineCap = s_lineBuf ? plan.dstW : 0;
    }

    if (!s_xmap || !s_ymap || !s_lineBuf) {
        return false;
    }

    for (int x = 0; x < plan.dstW; ++x) {
        s_xmap[x] = (int16_t)(plan.srcX0 + (x * plan.roiW) / plan.dstW);
    }

    for (int y = 0; y < plan.dstH; ++y) {
        s_ymap[y] = (int16_t)(plan.srcY0 + (y * plan.roiH) / plan.dstH);
    }

    return true;
}

static void a7800_display_task(void* arg)
{
    (void)arg;
    M5Cardputer.Display.startWrite();

    for (;;) {
        A7800FrameMsg msg = {};
        if (xQueueReceive(s_frameQ, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!msg.frame || msg.width == 0 || msg.height == 0 || msg.pitch == 0) {
            continue;
        }

        if (msg.indexed && !msg.palette565) {
            continue;
        }

        const int64_t tVideoStart = esp_timer_get_time();

        A7800RenderPlan plan = {};
        a7800_compute_plan((int)msg.width, (int)msg.height, msg.isPal, plan);

        if (!a7800_prepare_luts(plan)) {
            EMU_LOG("[A7800][DISP] buffer allocation failed\n");
            continue;
        }

        if (a7800_layout_changed(plan, (int)msg.width, (int)msg.height)) {
            a7800_clear_target();
        }

        M5Cardputer.Display.setAddrWindow(plan.xOff, plan.yOff, plan.dstW, plan.dstH);

        for (int y = 0; y < plan.dstH; y += kVideoBatchLines) {
            const int batch = (y + kVideoBatchLines <= plan.dstH)
                ? kVideoBatchLines
                : (plan.dstH - y);

            for (int row = 0; row < batch; ++row) {
                const uint8_t* srcBytes =
                    (const uint8_t*)msg.frame + (size_t)s_ymap[y + row] * msg.pitch;
                uint16_t* dst = s_lineBuf + (size_t)row * plan.dstW;

                if (msg.indexed) {
                    const uint8_t* srcLine = srcBytes;
                    for (int x = 0; x < plan.dstW; ++x) {
                        dst[x] = msg.palette565[srcLine[s_xmap[x]]];
                    }
                } else {
                    const uint16_t* srcLine = (const uint16_t*)srcBytes;
                    for (int x = 0; x < plan.dstW; ++x) {
                        dst[x] = srcLine[s_xmap[x]];
                    }
                }
            }

            M5Cardputer.Display.writePixels(s_lineBuf, plan.dstW * batch);
        }

        s_videoTotalUs += (esp_timer_get_time() - tVideoStart);
        s_videoCount++;
    }

    M5Cardputer.Display.endWrite();
}

void a7800_video_init(double fps, unsigned baseWidth, unsigned baseHeight, float aspectRatio)
{
    s_targetFps = fps;
    s_baseWidth = baseWidth;
    s_baseHeight = baseHeight;
    s_aspectRatio = aspectRatio > 0.0f ? aspectRatio : (4.0f / 3.0f);

    M5Cardputer.Display.setSwapBytes(true);
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    a7800_reset_layout_cache();

    if (!s_frameQ) {
        s_frameQ = xQueueCreate(2, sizeof(A7800FrameMsg));
        if (!s_frameQ) {
            EMU_LOG("[A7800][DISP] queue create failed\n");
        }
    }

    if (!s_displayTask && s_frameQ) {
        BaseType_t ok = xTaskCreatePinnedToCore(
            a7800_display_task,
            "A7800Disp",
            4096,
            nullptr,
            6,
            &s_displayTask,
            0
        );
        if (ok != pdPASS) {
            EMU_LOG("[A7800][DISP] task create failed\n");
            s_displayTask = nullptr;
        }
    }

    EMU_LOG("[A7800][DISP] fps=%.2f base=%ux%u aspect=%.3f\n",
           s_targetFps,
           s_baseWidth,
           s_baseHeight,
           s_aspectRatio);
}

void a7800_video_shutdown(void)
{
    if (s_displayTask) {
        vTaskDelete(s_displayTask);
        s_displayTask = nullptr;
    }

    if (s_frameQ) {
        vQueueDelete(s_frameQ);
        s_frameQ = nullptr;
    }

    free(s_lineBuf);
    s_lineBuf = nullptr;
    s_lineCap = 0;

    free(s_xmap);
    s_xmap = nullptr;
    s_xmapCap = 0;

    free(s_ymap);
    s_ymap = nullptr;
    s_ymapCap = 0;

    a7800_reset_layout_cache();
}

void a7800_video_toggle_fullscreen(void)
{
    if (!a7800FullScreen) {
        a7800FullScreen = true;
        a7800ZoomPercent = 100;
    } else {
        a7800ZoomPercent += 10;
        if (a7800ZoomPercent > 150) {
            a7800ZoomPercent = 100;
            a7800FullScreen = false;
        }
    }
    a7800_reset_layout_cache();
}

void a7800_video_adjust_zoom(int delta)
{
    if (!a7800FullScreen) {
        a7800FullScreen = true;
    }

    a7800ZoomPercent = clampi(a7800ZoomPercent + delta, 100, 150);
    a7800_reset_layout_cache();
}

void a7800_video_set_frame_skip(bool skip)
{
    s_skipFrame = skip;
}

void a7800_video_get_and_reset_stats(int64_t* totalUs, uint32_t* count)
{
    *totalUs = s_videoTotalUs;
    *count   = s_videoCount;
    s_videoTotalUs = 0;
    s_videoCount   = 0;
}

void a7800_video_submit_frame(const void* frame,
                              unsigned width,
                              unsigned height,
                              size_t pitch,
                              const uint16_t* palette565,
                              bool indexed,
                              bool isPal)
{
    if (s_skipFrame) {
        return;
    }
    if (!s_frameQ || !frame || width == 0 || height == 0 || pitch == 0) {
        return;
    }
    if (indexed && !palette565) {
        return;
    }

    A7800FrameMsg msg = {};
    msg.frame = frame;
    msg.width = width;
    msg.height = height;
    msg.pitch = pitch;
    msg.palette565 = palette565;
    msg.indexed = indexed;
    msg.isPal = isPal;

    BaseType_t ok = xQueueSend(s_frameQ, &msg, 0);
    (void)ok;
}
