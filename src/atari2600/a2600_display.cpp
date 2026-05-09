#pragma GCC optimize ("Os")

#include "a2600_display.h"
#include <M5Cardputer.h>
#include <Preferences.h>
#include <string>
#include "esp_heap_caps.h"
#include "share/emu_log_cpp.h"

static bool s_use_ext = false;
static bool s_use_12bit = false;
static A2600InternalViewMode s_internalViewMode = A2600InternalViewMode::Wide;

bool a2600FullScreen = true;
int a2600ZoomPercent = 110;

static uint16_t* s_lineBuf = nullptr;
static int s_lineCap = 0;
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
static int s_lastViewMode = -1;

static constexpr const char* kA2600DisplayNs = "a2600_disp";
static constexpr const char* kA2600ViewKey = "int_view";
static constexpr int kA2600NtscVisibleHeight = 192;
static constexpr int kA2600PalVisibleHeight = 228;

// ================== THREADED DISPLAY ==================

static constexpr int A2600_FRAME_SLOTS = 3;

struct A2600FrameSlot {
    uint8_t* pixels = nullptr;
    int capacity = 0;
    int width = 0;
    int height = 0;
    bool isPal = false;
    const uint16_t* palette565 = nullptr;
};

enum A2600SlotState : uint8_t {
    A2600_SLOT_FREE = 0,
    A2600_SLOT_QUEUED,
    A2600_SLOT_BUSY
};

static A2600FrameSlot* s_slots = nullptr;
static volatile A2600SlotState* s_slotState = nullptr;

static QueueHandle_t s_frameQ = nullptr;
static TaskHandle_t s_displayTask = nullptr;
static portMUX_TYPE s_slotMux = portMUX_INITIALIZER_UNLOCKED;

// ================== RENDER PLAN ==================

struct A2600RenderPlan {
    int srcX0;
    int srcY0;
    int roiW;
    int roiH;
    int dstW;
    int dstH;
    int xOff;
    int yOff;
};

static int clampi(int value, int minValue, int maxValue)
{
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static A2600InternalViewMode a2600_sanitize_internal_view_mode(uint8_t value)
{
    return value == static_cast<uint8_t>(A2600InternalViewMode::PixelPerfect)
               ? A2600InternalViewMode::PixelPerfect
               : A2600InternalViewMode::Wide;
}

const char* a2600_display_internal_view_mode_label(A2600InternalViewMode mode)
{
    return mode == A2600InternalViewMode::PixelPerfect ? "PIXEL" : "WIDE";
}

A2600InternalViewMode a2600_display_load_internal_view_mode(void)
{
    Preferences prefs;
    prefs.begin(kA2600DisplayNs, true);
    const uint8_t saved = prefs.getUChar(
        kA2600ViewKey,
        static_cast<uint8_t>(A2600InternalViewMode::Wide)
    );
    prefs.end();

    s_internalViewMode = a2600_sanitize_internal_view_mode(saved);
    return s_internalViewMode;
}

static void a2600_save_internal_view_mode(void)
{
    Preferences prefs;
    prefs.begin(kA2600DisplayNs, false);
    prefs.putUChar(kA2600ViewKey, static_cast<uint8_t>(s_internalViewMode));
    prefs.end();
}

static void a2600_clear_target(void)
{
    M5Cardputer.Display.fillScreen(TFT_BLACK);
}

static void a2600_reset_layout_cache(void)
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
    s_lastViewMode = -1;
}

static bool a2600_layout_changed(const A2600RenderPlan& plan, int srcW, int srcH)
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
        s_lastFull != a2600FullScreen ||
        s_lastZoom != a2600ZoomPercent ||
        s_lastViewMode != static_cast<int>(s_internalViewMode);

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
    s_lastFull = a2600FullScreen;
    s_lastZoom = a2600ZoomPercent;
    s_lastViewMode = static_cast<int>(s_internalViewMode);

    return changed;
}

static void a2600_compute_internal_visible_roi(int srcW, int srcH, bool isPal, A2600RenderPlan& plan)
{
    const int preferredVisibleH = isPal ? kA2600PalVisibleHeight : kA2600NtscVisibleHeight;

    plan.srcX0 = 0;
    plan.roiW = srcW;
    plan.roiH = clampi(preferredVisibleH, 64, srcH);
    plan.srcY0 = (srcH - plan.roiH) / 2;
}

static void a2600_compute_plan(int srcW, int srcH, bool isPal, A2600RenderPlan& plan)
{
    const int targetW = M5Cardputer.Display.width();
    const int targetH = M5Cardputer.Display.height();
    const int zoom = clampi(a2600ZoomPercent, 100, 150);

    if (s_use_ext) {
        if (a2600FullScreen) {
            plan.roiW = clampi((srcW * 100) / zoom, 64, srcW);
            plan.roiH = clampi((srcH * 100) / zoom, 64, srcH);
            plan.srcX0 = (srcW - plan.roiW) / 2;
            plan.srcY0 = (srcH - plan.roiH) / 2;
            plan.dstW = targetW;
            plan.dstH = targetH;
            plan.xOff = 0;
            plan.yOff = 0;
        } else {
            plan.srcX0 = 0;
            plan.srcY0 = 0;
            plan.roiW = srcW;
            plan.roiH = srcH;

            if (isPal && plan.roiH > targetH) {
                plan.srcY0 = (plan.roiH - targetH) / 2;
                plan.roiH = targetH;
            }

            plan.dstW = targetW;
            plan.dstH = (plan.roiH > targetH) ? targetH : plan.roiH;
            plan.xOff = 0;
            plan.yOff = (targetH - plan.dstH) / 2;
        }
        return;
    }

    if (a2600FullScreen) {
        plan.roiW = clampi((srcW * 100) / zoom, 64, srcW);
        plan.roiH = clampi((srcH * 100) / zoom, 64, srcH);
        plan.srcX0 = (srcW - plan.roiW) / 2;
        plan.srcY0 = (srcH - plan.roiH) / 2;
        plan.dstW = targetW;
        plan.dstH = targetH;
        plan.xOff = 0;
        plan.yOff = 0;
    } else {
        a2600_compute_internal_visible_roi(srcW, srcH, isPal, plan);

        if (s_internalViewMode == A2600InternalViewMode::Wide) {
            plan.dstW = clampi((targetH * 4) / 3, 1, targetW);
            plan.dstH = targetH;
            plan.xOff = (targetW - plan.dstW) / 2;
            plan.yOff = 0;
        } else {
            const float scaleX = (float)targetW / (float)plan.roiW;
            const float scaleY = (float)targetH / (float)plan.roiH;
            float scale = (scaleX < scaleY) ? scaleX : scaleY;
            if (scale <= 0.0f) scale = 1.0f;

            plan.dstW = clampi((int)(plan.roiW * scale), 1, targetW);
            plan.dstH = clampi((int)(plan.roiH * scale), 1, targetH);
            plan.xOff = (targetW - plan.dstW) / 2;
            plan.yOff = (targetH - plan.dstH) / 2;
        }
    }
}

static bool a2600_prepare_luts(const A2600RenderPlan& plan)
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
            (size_t)plan.dstW * sizeof(uint16_t),
            MALLOC_CAP_DMA | MALLOC_CAP_8BIT
        );
        if (!s_lineBuf) {
            s_lineBuf = (uint16_t*)malloc((size_t)plan.dstW * sizeof(uint16_t));
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
        if (plan.dstH == plan.roiH && !a2600FullScreen && s_use_ext) {
            s_ymap[y] = (int16_t)(plan.srcY0 + y);
        } else {
            s_ymap[y] = (int16_t)(plan.srcY0 + (y * plan.roiH) / plan.dstH);
        }
    }

    return true;
}

static void a2600_draw_line_16bit(const uint8_t* srcLine,
                                  const uint16_t* palette565,
                                  int dstW)
{
    for (int x = 0; x < dstW; ++x) {
        s_lineBuf[x] = palette565[srcLine[s_xmap[x]]];
    }

    M5Cardputer.Display.pushPixels(s_lineBuf, dstW);
}

// ================== DISPLAY TASK ==================

static void a2600_display_task(void* arg)
{
    (void)arg;
    M5Cardputer.Display.startWrite();

    for (;;) {
        int slotIndex = -1;
        if (xQueueReceive(s_frameQ, &slotIndex, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (slotIndex < 0 || slotIndex >= A2600_FRAME_SLOTS) {
            continue;
        }

        portENTER_CRITICAL(&s_slotMux);
        if (s_slotState[slotIndex] == A2600_SLOT_QUEUED) {
            s_slotState[slotIndex] = A2600_SLOT_BUSY;
        } else {
            portEXIT_CRITICAL(&s_slotMux);
            continue;
        }
        portEXIT_CRITICAL(&s_slotMux);

        A2600FrameSlot& slot = s_slots[slotIndex];

        if (!slot.pixels || !slot.palette565 || slot.width <= 0 || slot.height <= 0) {
            portENTER_CRITICAL(&s_slotMux);
            s_slotState[slotIndex] = A2600_SLOT_FREE;
            portEXIT_CRITICAL(&s_slotMux);
            continue;
        }

        A2600RenderPlan plan = {};
        a2600_compute_plan(slot.width, slot.height, slot.isPal, plan);

        if (!a2600_prepare_luts(plan)) {
            EMU_LOG("[A2600][DISP] buffer allocation failed\n");
            portENTER_CRITICAL(&s_slotMux);
            s_slotState[slotIndex] = A2600_SLOT_FREE;
            portEXIT_CRITICAL(&s_slotMux);
            continue;
        }

        if (a2600_layout_changed(plan, slot.width, slot.height)) {
            a2600_clear_target();
        }

        M5Cardputer.Display.setAddrWindow(plan.xOff, plan.yOff, plan.dstW, plan.dstH);

        for (int y = 0; y < plan.dstH; ++y) {
            const uint8_t* srcLine = slot.pixels + (size_t)s_ymap[y] * (size_t)slot.width;
            a2600_draw_line_16bit(srcLine, slot.palette565, plan.dstW);
        }


        portENTER_CRITICAL(&s_slotMux);
        s_slotState[slotIndex] = A2600_SLOT_FREE;
        portEXIT_CRITICAL(&s_slotMux);

        vTaskDelay(0);
    }
    
    M5Cardputer.Display.endWrite();
}

// ================== PUBLIC API ==================

static bool a2600_alloc_slots(void)
{
    if (!s_slots) {
        s_slots = (A2600FrameSlot*)calloc(A2600_FRAME_SLOTS, sizeof(A2600FrameSlot));
        if (!s_slots) {
            return false;
        }
    }

    if (!s_slotState) {
        s_slotState = (volatile A2600SlotState*)calloc(
            A2600_FRAME_SLOTS,
            sizeof(A2600SlotState)
        );
        if (!s_slotState) {
            free(s_slots);
            s_slots = nullptr;
            return false;
        }
    }

    for (int i = 0; i < A2600_FRAME_SLOTS; ++i) {
        s_slotState[i] = A2600_SLOT_FREE;
    }

    return true;
}

void a2600_display_init(void)
{
    s_internalViewMode = A2600InternalViewMode::Wide;
    M5Cardputer.Display.setSwapBytes(true);
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    a2600_reset_layout_cache();

    if (!a2600_alloc_slots()) {
        EMU_LOG("[A2600][DISP] slot allocation failed\n");
        return;
    }

    if (!s_frameQ) {
        s_frameQ = xQueueCreate(2, sizeof(int));
        if (!s_frameQ) {
            EMU_LOG("[A2600][DISP] queue create failed\n");
        }
    }
}

void a2600_display_start(void)
{
    if (!s_frameQ) {
        a2600_display_init();
    }

    if (!s_displayTask && s_frameQ) {
        BaseType_t ok = xTaskCreatePinnedToCore(
            a2600_display_task,
            "A2600Disp",
            4096,
            nullptr,
            5,
            &s_displayTask,
            0
        );

        if (ok != pdPASS) {
            EMU_LOG("[A2600][DISP] task create failed\n");
            if (s_displayTask) {
                vTaskDelete(s_displayTask);
            }
            s_displayTask = nullptr;
        }
    }
}

void a2600_display_stop(void)
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

    if (s_slots && s_slotState) {
        for (int i = 0; i < A2600_FRAME_SLOTS; ++i) {
            free(s_slots[i].pixels);
            s_slots[i].pixels = nullptr;
            s_slots[i].capacity = 0;
            s_slots[i].width = 0;
            s_slots[i].height = 0;
            s_slots[i].isPal = false;
            s_slots[i].palette565 = nullptr;
            s_slotState[i] = A2600_SLOT_FREE;
        }
    }

    free((void*)s_slotState);
    s_slotState = nullptr;

    free(s_slots);
    s_slots = nullptr;

    a2600_reset_layout_cache();
}

A2600InternalViewMode a2600_display_get_internal_view_mode(void)
{
    return s_internalViewMode;
}

void a2600_display_set_internal_view_mode(A2600InternalViewMode mode, bool persist)
{
    s_internalViewMode = a2600_sanitize_internal_view_mode(static_cast<uint8_t>(mode));
    if (persist) {
        a2600_save_internal_view_mode();
    }
    a2600_reset_layout_cache();
}

const char* a2600_display_get_internal_view_mode_label(void)
{
    return a2600_display_internal_view_mode_label(s_internalViewMode);
}

void a2600_display_toggle_internal_view_mode(void)
{
    const A2600InternalViewMode nextMode =
        (s_internalViewMode == A2600InternalViewMode::PixelPerfect)
            ? A2600InternalViewMode::Wide
            : A2600InternalViewMode::PixelPerfect;
    a2600_display_set_internal_view_mode(nextMode, true);
    EMU_LOG("[A2600][DISP] internal view=%s\n",
           a2600_display_internal_view_mode_label(s_internalViewMode));
}

void a2600_display_submit_frame(const uint8_t* indexedFrame,
                                int width,
                                int height,
                                const uint16_t* palette565,
                                bool isPal)
{
    if (!s_frameQ || !indexedFrame || !palette565 || width <= 0 || height <= 0) {
        return;
    }

    const int pixelCount = width * height;
    if (pixelCount <= 0) {
        return;
    }

    int slotIndex = -1;

    portENTER_CRITICAL(&s_slotMux);
    for (int i = 0; i < A2600_FRAME_SLOTS; ++i) {
        if (s_slotState[i] == A2600_SLOT_FREE) {
            s_slotState[i] = A2600_SLOT_QUEUED;
            slotIndex = i;
            break;
        }
    }
    portEXIT_CRITICAL(&s_slotMux);

    if (slotIndex < 0) {
        // pas de slot libre
        return;
    }

    A2600FrameSlot& slot = s_slots[slotIndex];

    if (pixelCount > slot.capacity) {
        free(slot.pixels);
        slot.pixels = (uint8_t*)heap_caps_malloc(
            (size_t)pixelCount,
            MALLOC_CAP_8BIT
        );
        if (!slot.pixels) {
            slot.pixels = (uint8_t*)malloc((size_t)pixelCount);
        }
        slot.capacity = slot.pixels ? pixelCount : 0;
    }

    if (!slot.pixels) {
        portENTER_CRITICAL(&s_slotMux);
        s_slotState[slotIndex] = A2600_SLOT_FREE;
        portEXIT_CRITICAL(&s_slotMux);
        return;
    }

    memcpy(slot.pixels, indexedFrame, (size_t)pixelCount);
    slot.width = width;
    slot.height = height;
    slot.isPal = isPal;
    slot.palette565 = palette565;

    BaseType_t ok = xQueueSend(s_frameQ, &slotIndex, 0);
    if (ok != pdTRUE) {
        portENTER_CRITICAL(&s_slotMux);
        s_slotState[slotIndex] = A2600_SLOT_FREE;
        portEXIT_CRITICAL(&s_slotMux);
    }
}
