#pragma GCC optimize ("Ofast")

#include "gx4000_sound.h"
#include <M5Cardputer.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

static int s_sample_rate = 22050;
static constexpr int kChannel = 0;
static constexpr bool kForceFixedRate = true;
static constexpr int kFrameBatch = 2;
static constexpr int kMaxFrameSamples = 1400;
static constexpr int kAudioQueueLen = 4;
static constexpr int kAudioMsgPoolCount = 4;

typedef struct {
    uint16_t count;
    uint16_t reserved;
    uint32_t rate;
    int16_t samples[kMaxFrameSamples];
} GX4000AudioMsg;

static QueueHandle_t s_audioFreeQ = nullptr;
static QueueHandle_t s_audioReadyQ = nullptr;
static TaskHandle_t s_audioTask = nullptr;
static volatile bool s_audioRunning = false;
static GX4000AudioMsg *s_msgPool = nullptr;
static int16_t *s_batchBuf = nullptr;
static int      s_batchCount = 0;

// Rate measurement / lock state.
static uint32_t s_statPushed        = 0;
static uint32_t s_effectiveRate     = 22050;
static uint32_t s_rateWindowStartMs = 0;
static uint32_t s_pushedAtWindow    = 0;
static uint32_t s_warmupStartMs     = 0;
static bool     s_rateLocked        = false;
static uint32_t s_statQueueSat      = 0;

static void update_effective_rate(void);
static bool gx4000_sound_ensure_runtime(void);

static void gx4000_audio_task(void *arg)
{
    (void)arg;
    uint8_t slot = 0;

    while (s_audioRunning) {
        if (xQueueReceive(s_audioReadyQ, &slot, pdMS_TO_TICKS(20)) != pdTRUE) {
            continue;
        }

        if (!s_msgPool || slot >= (uint8_t)kAudioMsgPoolCount) {
            continue;
        }

        GX4000AudioMsg *msg = &s_msgPool[slot];

        if (msg->count == 0 || msg->count > kMaxFrameSamples) {
            (void)xQueueSend(s_audioFreeQ, &slot, 0);
            continue;
        }

        while (s_audioRunning && (uint32_t)M5Cardputer.Speaker.isPlaying(kChannel) >= 2U) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        if (!s_audioRunning) {
            break;
        }

        bool ok = M5Cardputer.Speaker.playRaw(msg->samples,
                                              (size_t)msg->count,
                                              msg->rate,
                                              false, 1, kChannel, false);
        (void)xQueueSend(s_audioFreeQ, &slot, 0);
        if (!ok) {
            s_statQueueSat++;
            continue;
        }

        s_statPushed += (uint32_t)msg->count;
        if (!kForceFixedRate) {
            update_effective_rate();
        }
    }

    vTaskDelete(nullptr);
}

static void update_effective_rate(void)
{
    if (s_rateLocked) return;

    uint32_t now = (uint32_t)millis();

    if (s_rateWindowStartMs == 0U) {
        s_rateWindowStartMs = now;
        s_pushedAtWindow    = s_statPushed;
        return;
    }

    uint32_t elapsed = now - s_rateWindowStartMs;
    if (elapsed < 1000U) return;

    uint32_t produced = s_statPushed - s_pushedAtWindow;
    uint32_t measured = (produced * 1000U) / elapsed;
    if (measured < 8000U)  measured = 8000U;
    if (measured > 22050U) measured = 22050U;

    // Fast IIR: α = 1/4 (converges in ~4 windows = 4 s)
    s_effectiveRate  = (s_effectiveRate * 3U + measured) / 4U;
    s_rateWindowStartMs = now;
    s_pushedAtWindow    = s_statPushed;

    if (s_warmupStartMs == 0U) s_warmupStartMs = now;
    if ((now - s_warmupStartMs) >= 4000U) {
        s_rateLocked = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void gx4000_sound_init(int sample_rate)
{
    s_sample_rate       = sample_rate;
    s_effectiveRate     = (uint32_t)sample_rate;
    s_rateWindowStartMs = 0;
    s_pushedAtWindow    = 0;
    s_warmupStartMs     = 0;
    s_rateLocked        = false;
    s_statPushed        = 0;
    s_statQueueSat      = 0;
    s_batchCount        = 0;
    s_audioRunning      = false;

    auto cfg = M5Cardputer.Speaker.config();
    cfg.sample_rate      = (uint32_t)sample_rate;
    cfg.stereo           = false;
    cfg.dma_buf_len      = 512;
    cfg.dma_buf_count    = 8;
    cfg.task_priority    = 4;
    cfg.task_pinned_core = 0;
    M5Cardputer.Speaker.config(cfg);
    if (!M5Cardputer.Speaker.isRunning()) {
        M5Cardputer.Speaker.begin();
    }
    M5Cardputer.Speaker.setVolume(60);
    M5Cardputer.Speaker.stop(kChannel);
}

void gx4000_sound_shutdown(void)
{
    s_audioRunning = false;
    if (s_audioTask) {
        vTaskDelete(s_audioTask);
        s_audioTask = nullptr;
    }
    if (s_audioReadyQ) {
        vQueueDelete(s_audioReadyQ);
        s_audioReadyQ = nullptr;
    }
    if (s_audioFreeQ) {
        vQueueDelete(s_audioFreeQ);
        s_audioFreeQ = nullptr;
    }

    M5Cardputer.Speaker.stop(kChannel);

    if (s_batchBuf)   { heap_caps_free(s_batchBuf);   s_batchBuf = nullptr; }
    if (s_msgPool)    { heap_caps_free(s_msgPool);    s_msgPool = nullptr; }

    s_rateLocked = false;

    M5Cardputer.Speaker.end();
}

static bool gx4000_sound_ensure_runtime(void)
{
    if (!s_batchBuf) {
        s_batchBuf = (int16_t *)heap_caps_malloc(
            kMaxFrameSamples * sizeof(int16_t),
            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!s_batchBuf) return false;
    }

    if (!s_msgPool) {
        s_msgPool = (GX4000AudioMsg *)heap_caps_malloc(
            sizeof(GX4000AudioMsg) * kAudioMsgPoolCount,
            MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        if (!s_msgPool) return false;
    }

    if (!s_audioFreeQ) {
        s_audioFreeQ = xQueueCreate(kAudioMsgPoolCount, sizeof(uint8_t));
        if (!s_audioFreeQ) return false;
        for (uint8_t i = 0; i < (uint8_t)kAudioMsgPoolCount; ++i) {
            (void)xQueueSend(s_audioFreeQ, &i, 0);
        }
    }

    if (!s_audioReadyQ) {
        s_audioReadyQ = xQueueCreate(kAudioQueueLen, sizeof(uint8_t));
        if (!s_audioReadyQ) return false;
    }

    if (!s_audioRunning || !s_audioTask) {
        s_audioRunning = true;
        BaseType_t ok = xTaskCreatePinnedToCore(
            gx4000_audio_task,
            "gx4_audio",
            3072,
            nullptr,
            8,
            &s_audioTask,
            0);
        if (ok != pdPASS) {
            s_audioTask = nullptr;
            s_audioRunning = false;
            return false;
        }
    }

    return true;
}

unsigned long gx4000_sound_take_drop_count(void)
{
    uint32_t v = s_statQueueSat;
    s_statQueueSat = 0;
    return (unsigned long)v;
}

void gx4000_sound_push(const unsigned char *data, unsigned int byte_len)
{
    if (!data || byte_len < 2) return;
    if (!gx4000_sound_ensure_runtime()) return;

    int sample_count = (int)(byte_len / 2);
    if (sample_count <= 0) return;

    // Arnold mixer already outputs signed 16-bit PCM.
    const int16_t *src = (const int16_t *)data;
    int src_pos = 0;

    while (src_pos < sample_count) {
        int room = kMaxFrameSamples - s_batchCount;
        if (room <= 0) break;

        int take = sample_count - src_pos;
        if (take > room) take = room;

        memcpy(&s_batchBuf[s_batchCount], &src[src_pos], (size_t)take * sizeof(int16_t));
        s_batchCount += take;
        src_pos += take;

        int batch_target = (s_sample_rate / 50) * kFrameBatch;
        if (batch_target < 1) batch_target = 1;
        if (batch_target > kMaxFrameSamples) batch_target = kMaxFrameSamples;

        if (s_batchCount < batch_target && s_batchCount < kMaxFrameSamples) {
            continue;
        }

        uint8_t slot = 0;
        if (xQueueReceive(s_audioFreeQ, &slot, 0) != pdTRUE) {
            s_statQueueSat++;
            s_batchCount = 0;
            continue;
        }

        GX4000AudioMsg *msg = &s_msgPool[slot];
        msg->count = (uint16_t)s_batchCount;
        uint32_t rate_to_play = kForceFixedRate ? (uint32_t)s_sample_rate : s_effectiveRate;
        msg->rate = rate_to_play;
        memcpy(msg->samples, s_batchBuf, (size_t)s_batchCount * sizeof(int16_t));

        if (xQueueSend(s_audioReadyQ, &slot, 0) != pdTRUE) {
            (void)xQueueSend(s_audioFreeQ, &slot, 0);
            s_statQueueSat++;
        }
        s_batchCount = 0;
    }
}
