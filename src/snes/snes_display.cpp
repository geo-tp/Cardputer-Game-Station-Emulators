#include "snes_display.h"

#include <Arduino.h>
#include <M5Cardputer.h>
#include "esp_heap_caps.h"

#include "snes9x/snes9x.h"
#include "share/game_save.h"
#include "share/emu_log_cpp.h"

// Crop
static constexpr int CROP_X = (SNES_WIDTH - LCD_W) / 2; // 8
static constexpr int CROP_Y = (SNES_HEIGHT - LCD_H) / 2; // 44
extern bool snes_interlace_lock_parity;

#ifndef SNES_NO_THREADED_DISPLAY

// ================== DOUBLE BUFFER LINES ==================

typedef struct {
    uint16_t y;
    uint16_t _pad;          // align pixels[] to a 4-byte boundary
    uint16_t pixels[LCD_W]; // offset 4 — 4-aligned from any 4-aligned base
} SnesLineBuf;
// sizeof = 4 + 480 = 484 (divisible by 4)
// s_buf[0].pixels @ base+4   (4-aligned)
// s_buf[1].pixels @ base+488 (4-aligned)

enum BufState : uint8_t {
    BUF_FREE = 0,
    BUF_READY,
    BUF_DRAWING
};

static TaskHandle_t  s_task    = nullptr;
static volatile bool s_running = false;
static volatile bool s_spi_released_for_save = false;

// 2 buffers
static SnesLineBuf *s_buf = nullptr;     // [2]
static volatile BufState s_state[2] = { BUF_FREE, BUF_FREE };

// Notify value bits
static constexpr uint32_t NOTIF_BUF0 = (1u << 0);
static constexpr uint32_t NOTIF_BUF1 = (1u << 1);

// ================== DISPLAY TASK ==================

static void snes_display_task(void *arg)
{
    (void)arg;

    bool spiStarted = false;

    auto start_spi_if_needed = [&]() {
        if (!spiStarted) {
            M5Cardputer.Display.startWrite();
            spiStarted = true;
        }
    };

    auto stop_spi_if_needed = [&]() {
        if (spiStarted) {
            M5Cardputer.Display.endWrite();
            spiStarted = false;
        }
    };

    start_spi_if_needed();

    for (;;) {
        uint32_t notif = 0;
        xTaskNotifyWait(0, 0xFFFFFFFFu, &notif, portMAX_DELAY);

        if (!s_running) {
            stop_spi_if_needed();
            continue;
        }

        const bool saving = share::gameIsSaving();

        if (saving) {
            s_spi_released_for_save = true;
        } else {
            s_spi_released_for_save = false;
            start_spi_if_needed();
        }

        if (!snes_interlace_lock_parity) {
            // ===== FAST PATH: INTERLACE =====
            if (notif & NOTIF_BUF0) {
                if (s_state[0] == BUF_READY) {
                    s_state[0] = BUF_DRAWING;

                    uint16_t y = s_buf[0].y;
                    if (y < LCD_H) {
                        start_spi_if_needed();
                        M5Cardputer.Display.setAddrWindow(0, (int)y, LCD_W, 1);
                        M5Cardputer.Display.writePixels(s_buf[0].pixels, LCD_W);
                    }

                    s_state[0] = BUF_FREE;
                }
            }

            if (notif & NOTIF_BUF1) {
                if (s_state[1] == BUF_READY) {
                    s_state[1] = BUF_DRAWING;

                    uint16_t y = s_buf[1].y;
                    if (y < LCD_H) {
                        start_spi_if_needed();
                        M5Cardputer.Display.setAddrWindow(0, (int)y, LCD_W, 1);
                        M5Cardputer.Display.writePixels(s_buf[1].pixels, LCD_W);
                    }

                    s_state[1] = BUF_FREE;
                }
            }
        } else {
            // ===== FLINE DUP =====
            if (notif & NOTIF_BUF0) {
                if (s_state[0] == BUF_READY) {
                    s_state[0] = BUF_DRAWING;

                    uint16_t y = s_buf[0].y;
                    if (y < LCD_H) {
                        start_spi_if_needed();
                        M5Cardputer.Display.setAddrWindow(0, (int)y, LCD_W, 1);
                        M5Cardputer.Display.writePixels(s_buf[0].pixels, LCD_W);

                        uint16_t y2 = (y ^ 1u);
                        if (y2 < LCD_H) {
                            M5Cardputer.Display.setAddrWindow(0, (int)y2, LCD_W, 1);
                            M5Cardputer.Display.writePixels(s_buf[0].pixels, LCD_W);
                        }
                    }

                    s_state[0] = BUF_FREE;
                }
            }

            if (notif & NOTIF_BUF1) {
                if (s_state[1] == BUF_READY) {
                    s_state[1] = BUF_DRAWING;

                    uint16_t y = s_buf[1].y;
                    if (y < LCD_H) {
                        start_spi_if_needed();
                        M5Cardputer.Display.setAddrWindow(0, (int)y, LCD_W, 1);
                        M5Cardputer.Display.writePixels(s_buf[1].pixels, LCD_W);

                        uint16_t y2 = (y ^ 1u);
                        if (y2 < LCD_H) {
                            M5Cardputer.Display.setAddrWindow(0, (int)y2, LCD_W, 1);
                            M5Cardputer.Display.writePixels(s_buf[1].pixels, LCD_W);
                        }
                    }

                    s_state[1] = BUF_FREE;
                }
            }
        }

        if (saving) {
            stop_spi_if_needed();
            s_spi_released_for_save = true;
            taskYIELD();
        }
    }
}

// ================== PUBLIC API ==================

extern "C" void snes_display_init(void)
{
    M5Cardputer.Display.setSwapBytes(true);
    M5Cardputer.Display.fillScreen(TFT_BLACK);

   if (s_buf) {
        heap_caps_free(s_buf);
        s_buf = nullptr;
    }

    // 2 lines buffer
    s_buf = (SnesLineBuf *)heap_caps_malloc(
        sizeof(SnesLineBuf) * 2,
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );

    if (!s_buf) {
        EMU_LOG("[SNES-DISP] buffer alloc failed\n");
        return;
    }

    // reset states
    s_state[0] = BUF_FREE;
    s_state[1] = BUF_FREE;
}

extern "C" void snes_display_start()
{
    if (s_task) return;
    s_running = true;

    BaseType_t ok = xTaskCreatePinnedToCore(
        snes_display_task,
        "SnesDisp",
        1800, // stack
        nullptr,
        6, // prio
        &s_task,
        0 // core 0
    );

    if (ok != pdPASS) {
        EMU_LOG("[SNES-DISP] task create failed (stack=%u heap=%u largest8=%u)\n",
                1800u,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        if (s_task) vTaskDelete(s_task);
        s_task = nullptr;
        s_running = false;
    }
}

extern "C" void snes_display_stop(void)
{
    s_running = false;

    if (s_task) {
        xTaskNotify(s_task, 0, eNoAction);
        vTaskDelete(s_task);
        s_task = nullptr;
    }

    if (s_buf) {
        heap_caps_free(s_buf);
        s_buf = nullptr;
    }

    s_state[0] = BUF_FREE;
    s_state[1] = BUF_FREE;
}

// Called by the core for each line
extern "C" void snes_display_submit_line(uint32_t y,
                                         const uint16_t *pixels,
                                         uint32_t width)
{
    if (!pixels || !s_running || !s_task) return;
    if (y >= LCD_H) return;

    // Find a free buffer
    int idx = -1;
    if (s_state[0] == BUF_FREE) idx = 0;
    else if (s_state[1] == BUF_FREE) idx = 1;
    else {
        // the buffers are full, drop the line
        return;
    }

    // Mark DRAWING
    s_state[idx] = BUF_DRAWING;
    s_buf[idx].y = (uint16_t)y;

    if (width >= 512)
    {
        const uint16_t *src = pixels + (CROP_X * 2);
        for (int i = 0; i < LCD_W; ++i)
            s_buf[idx].pixels[i] = src[i * 2];
    }
    else
    {
        // Normal path
        if (width > SNES_WIDTH) width = SNES_WIDTH;

        int srcX0 = CROP_X;
        int srcX1 = srcX0 + LCD_W;

        if (srcX0 < 0) srcX0 = 0;
        if ((uint32_t)srcX1 > width) srcX1 = (int)width;

        int copyW = srcX1 - srcX0;
        if (copyW <= 0) {
            s_state[idx] = BUF_FREE;
            return;
        }
        if (copyW > LCD_W) copyW = LCD_W;

        const uint16_t *src = pixels + srcX0;
        for (int i = 0; i < copyW; ++i) s_buf[idx].pixels[i] = src[i];
        for (int i = copyW; i < LCD_W; ++i) s_buf[idx].pixels[i] = 0x0000;
    }

    // Mark READY
    s_state[idx] = BUF_READY;
    xTaskNotify(s_task, (idx == 0) ? NOTIF_BUF0 : NOTIF_BUF1, eSetBits);
}

#else

// ===================== NO TASK VERSION =====================

extern "C" void snes_display_init(void)
{
    M5Cardputer.Display.setSwapBytes(true);
    M5Cardputer.Display.fillScreen(TFT_BLACK);
}

extern "C" void snes_display_start(void)
{
    M5Cardputer.Display.startWrite();
}

extern "C" void snes_display_stop(void)
{
    M5Cardputer.Display.endWrite();
}

extern "C" void snes_display_submit_line(uint32_t y,
                                         const uint16_t *pixels,
                                         uint32_t width)
{
    if (!pixels) return;
    if (y >= LCD_H) return; 

    if (width > SNES_WIDTH) width = SNES_WIDTH;

    static uint16_t lineBuf[LCD_W];

    int srcX0 = CROP_X;          // 8
    int srcX1 = srcX0 + LCD_W;   // 8 + 240 = 248

    if (srcX0 < 0)              srcX0 = 0;
    if ((uint32_t)srcX1 > width) srcX1 = width;

    int copyW = srcX1 - srcX0;
    if (copyW <= 0) {
        return;
    }
    if (copyW > LCD_W) copyW = LCD_W;

    const uint16_t *src = pixels + srcX0;
    for (int i = 0; i < copyW; ++i) {
        lineBuf[i] = src[i];
    }
    for (int i = copyW; i < LCD_W; ++i) {
        lineBuf[i] = 0x0000;
    }

    M5Cardputer.Display.setAddrWindow(0, (int)y, LCD_W, 1);
    M5Cardputer.Display.writePixels(lineBuf, LCD_W);
}

#endif

extern "C" void snes_display_wake(void)
{
    if (s_task) {
        xTaskNotify(s_task, 0, eNoAction);
    }
}

extern "C" bool snes_display_is_spi_released(void)
{
    return s_spi_released_for_save;
}
