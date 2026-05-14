#include "ws_sound.h"
extern "C" {
  #include "oswan/WSApu.h" 
}
#include <Arduino.h>
#include <M5Cardputer.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr int kNativeSampleRate = 24000;
static int           g_sample_rate = kNativeSampleRate;
#ifndef WS_AUDIO_PERIOD_MS
#define WS_AUDIO_PERIOD_MS 8
#endif
#ifndef WS_AUDIO_DMA_LEN
#define WS_AUDIO_DMA_LEN 320
#endif
#ifndef WS_AUDIO_DMA_COUNT
#define WS_AUDIO_DMA_COUNT 6
#endif
#ifndef WS_AUDIO_POLL_MS
#define WS_AUDIO_POLL_MS 0
#endif
static constexpr int kDefaultPeriodMs = WS_AUDIO_PERIOD_MS;
static int           g_chunk       = 0; 
static constexpr int kChannel      = 0;
static constexpr int kMaxChunk     = 320;
static int16_t* s_buf0 = NULL;
static int16_t* s_buf1 = NULL;
static int16_t* s_buf[2] = { NULL, NULL };
static uint8_t  s_flip   = 0;
static int16_t  s_lastSample = 0;
#ifdef BENCHMARK_LOGS
static volatile uint32_t s_statBlocks = 0;
static volatile uint32_t s_statUnderflows = 0;
static volatile uint32_t s_statMaxAvailable = 0;
static volatile uint32_t s_statMaxQueueDepth = 0;
static volatile uint32_t s_statMinAvailable = 0xffffffffu;
static volatile uint32_t s_statAvailableSum = 0;
static volatile uint32_t s_statMissingTotal = 0;
static volatile uint32_t s_statMissingMax = 0;
static volatile uint32_t s_statQueueDepth[3] = { 0, 0, 0 };
static volatile uint32_t s_statPostQueueDepth[3] = { 0, 0, 0 };
static volatile uint32_t s_statPlayFails = 0;
#define WS_SOUND_BENCH_INC(field)       (field++)
#define WS_SOUND_BENCH_ADD(field, val)  (field += (uint32_t)(val))
#define WS_SOUND_BENCH_MAX(field, val)  do { uint32_t _v = (uint32_t)(val); if (_v > field) field = _v; } while (0)
#define WS_SOUND_BENCH_MIN(field, val)  do { uint32_t _v = (uint32_t)(val); if (_v < field) field = _v; } while (0)
#else
#define WS_SOUND_BENCH_INC(field)       ((void)0)
#define WS_SOUND_BENCH_ADD(field, val)  ((void)0)
#define WS_SOUND_BENCH_MAX(field, val)  ((void)0)
#define WS_SOUND_BENCH_MIN(field, val)  ((void)0)
#endif

// Task
static TaskHandle_t s_taskAudio  = nullptr;
static volatile bool s_runAudio  = false;
static volatile bool s_pauseAudio = false;
static TickType_t    s_periodTicks = 0;

// -----------------------------------------------------------------------------
// Utils
// -----------------------------------------------------------------------------
static inline int16_t clamp16(int32_t v) {
  if (v >  32767) return  32767;
  if (v < -32768) return -32768;
  return (int16_t)v;
}

static bool buffers_ok() {
  return g_chunk <= kMaxChunk;
}

static void set_chunk_for_period(uint32_t period_ms) {
  if (period_ms == 0) period_ms = kDefaultPeriodMs;
  int chunk = (int)(((uint32_t)g_sample_rate * period_ms + 500u) / 1000u);
  if (chunk < 64) chunk = 64;
  if (chunk > kMaxChunk) chunk = kMaxChunk;
  g_chunk = chunk;
}

static inline void build_block_from_apu(int16_t* dst) {
  int need = g_chunk;
  int have = apuBufLen();
  WS_SOUND_BENCH_INC(s_statBlocks);
  WS_SOUND_BENCH_MIN(s_statMinAvailable, have);
  WS_SOUND_BENCH_ADD(s_statAvailableSum, have);
  WS_SOUND_BENCH_MAX(s_statMaxAvailable, have);
  if (have < need) {
    const uint32_t missing = (uint32_t)(need - have);
    WS_SOUND_BENCH_INC(s_statUnderflows);
    WS_SOUND_BENCH_ADD(s_statMissingTotal, missing);
    WS_SOUND_BENCH_MAX(s_statMissingMax, missing);
  }

  // Si pas assez
  int to_read = (have >= need) ? need : have;

  // Consommation 
  for (int i = 0; i < to_read; ++i) {
    int16_t L = 0;
    int16_t R = 0;
    if (!apuReadStereo(&L, &R)) {
      dst[i] = 0;
      continue;
    }

    int32_t m = ((int32_t)L + (int32_t)R) / 2;
    dst[i] = clamp16(m);
    s_lastSample = dst[i];
  }

  // Pad with a short decaying hold instead of hard silence to soften underruns.
  for (int i = to_read; i < need; ++i) {
    s_lastSample = (int16_t)((int32_t)s_lastSample * 15 / 16);
    dst[i] = s_lastSample;
  }
}

static inline void queue_block(const int16_t* pcm) {
  const bool ok = M5Cardputer.Speaker.playRaw(
    pcm,
    (size_t)g_chunk,
    (uint32_t)g_sample_rate,
    false,     // mono
    1,         // play once
    kChannel,
    false // dont stop current sound
  );
  if (!ok) WS_SOUND_BENCH_INC(s_statPlayFails);
#ifdef BENCHMARK_LOGS
  size_t queued = M5Cardputer.Speaker.isPlaying(kChannel);
  s_statPostQueueDepth[queued < 2 ? queued : 2]++;
#endif
}

// -----------------------------------------------------------------------------
// API
// -----------------------------------------------------------------------------
extern "C" void ws_sound_init(int sample_rate_hz) {
  g_sample_rate = sample_rate_hz > 0 ? sample_rate_hz : kNativeSampleRate;
  set_chunk_for_period(kDefaultPeriodMs);

  if (!buffers_ok()) {
    g_chunk = kMaxChunk;
  }

  if (s_buf0 || s_buf1) {
          free(s_buf0);
          free(s_buf1);
          s_buf0 = s_buf1 = NULL;
          s_buf[0] = s_buf[1] = NULL;
      }

  s_buf0 = (int16_t*)malloc(kMaxChunk * sizeof(int16_t));
  s_buf1 = (int16_t*)malloc(kMaxChunk * sizeof(int16_t));
  memset(s_buf0, 0, kMaxChunk * sizeof(int16_t));
  memset(s_buf1, 0, kMaxChunk * sizeof(int16_t));
  s_buf[0] = s_buf0;
  s_buf[1] = s_buf1;


  if (!M5Cardputer.Speaker.isRunning()) {
    auto cfg = M5Cardputer.Speaker.config();
    cfg.sample_rate       = g_sample_rate;
    cfg.stereo            = false;  // sortie mono
    cfg.dma_buf_len       = WS_AUDIO_DMA_LEN;
    cfg.dma_buf_count     = WS_AUDIO_DMA_COUNT;
    cfg.task_priority     = 4;
    cfg.task_pinned_core  = 0;
    M5Cardputer.Speaker.config(cfg);
    M5Cardputer.Speaker.begin();
  }

  M5Cardputer.Speaker.setVolume(80);
  s_flip = 0;
  s_lastSample = 0;
#ifdef BENCHMARK_LOGS
  s_statBlocks = 0;
  s_statUnderflows = 0;
  s_statMaxAvailable = 0;
  s_statMaxQueueDepth = 0;
  s_statMinAvailable = 0xffffffffu;
  s_statAvailableSum = 0;
  s_statMissingTotal = 0;
  s_statMissingMax = 0;
  s_statQueueDepth[0] = 0;
  s_statQueueDepth[1] = 0;
  s_statQueueDepth[2] = 0;
  s_statPostQueueDepth[0] = 0;
  s_statPostQueueDepth[1] = 0;
  s_statPostQueueDepth[2] = 0;
  s_statPlayFails = 0;
#endif
}

extern "C" void ws_sound_set_volume(uint8_t vol) {
  M5Cardputer.Speaker.setVolume(vol);
}

extern "C" void ws_sound_shutdown(void) {
  ws_sound_stop_task();
  M5Cardputer.Speaker.stop(kChannel);
}

extern "C" void ws_sound_frame(void) {
  size_t queued = M5Cardputer.Speaker.isPlaying(kChannel);
  WS_SOUND_BENCH_MAX(s_statMaxQueueDepth, queued);
#ifdef BENCHMARK_LOGS
  s_statQueueDepth[queued < 2 ? queued : 2]++;
#endif

  if (queued == 0) {
    // Amorcer 2 blocs
    for (int i = 0; i < 2; ++i) {
      build_block_from_apu(s_buf[s_flip]);
      queue_block(s_buf[s_flip]);
      s_flip ^= 1;
    }
  } else if (queued == 1) {
    // Maintenir 2 blocs
    build_block_from_apu(s_buf[s_flip]);
    queue_block(s_buf[s_flip]);
    s_flip ^= 1;
  } else {
    // queued >= 2
  }
}

// -----------------------------------------------------------------------------
// Task
// -----------------------------------------------------------------------------
static void ws_audio_task(void* arg) {
  (void)arg;
  TickType_t last = xTaskGetTickCount();

  while (s_runAudio) {
    if (!s_pauseAudio) {
      ws_sound_frame();
    }
    vTaskDelayUntil(&last, s_periodTicks);
  }
  vTaskDelete(nullptr);
}

extern "C" void ws_sound_start_task(uint32_t period_ms, int core) {
  if (s_taskAudio) return;

  if (period_ms == 0) period_ms = kDefaultPeriodMs;
  set_chunk_for_period(period_ms);
  uint32_t poll_ms = WS_AUDIO_POLL_MS ? WS_AUDIO_POLL_MS : period_ms;
  if (poll_ms == 0) poll_ms = 1;
  s_periodTicks = pdMS_TO_TICKS(poll_ms);
  if (s_periodTicks == 0) s_periodTicks = 1;

  s_runAudio = true;
  xTaskCreatePinnedToCore(ws_audio_task, "ws_audio", 2048, nullptr, 6, &s_taskAudio, core);
}

extern "C" void ws_sound_stop_task(void) {
  if (!s_taskAudio) return;
  s_runAudio = false;
  s_taskAudio = nullptr;
}

extern "C" void ws_sound_pause_task(int pause) {
  s_pauseAudio = pause != 0;
  if (s_pauseAudio) {
    M5Cardputer.Speaker.stop(kChannel);
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

#ifdef BENCHMARK_LOGS
extern "C" void ws_sound_get_and_reset_stats(uint32_t* blocks,
                                              uint32_t* underflows,
                                              uint32_t* max_available,
                                              uint32_t* max_queue_depth,
                                              uint32_t* min_available,
                                              uint32_t* avg_available,
                                              uint32_t* missing_total,
                                              uint32_t* missing_max,
                                              uint32_t* queue0,
                                              uint32_t* queue1,
                                              uint32_t* queue2,
                                              uint32_t* post_queue0,
                                              uint32_t* post_queue1,
                                              uint32_t* post_queue2,
                                              uint32_t* play_fails) {
  const uint32_t blockCount = s_statBlocks;
  if (blocks) *blocks = blockCount;
  if (underflows) *underflows = s_statUnderflows;
  if (max_available) *max_available = s_statMaxAvailable;
  if (max_queue_depth) *max_queue_depth = s_statMaxQueueDepth;
  if (min_available) *min_available = (s_statMinAvailable == 0xffffffffu) ? 0 : s_statMinAvailable;
  if (avg_available) *avg_available = blockCount ? (s_statAvailableSum / blockCount) : 0;
  if (missing_total) *missing_total = s_statMissingTotal;
  if (missing_max) *missing_max = s_statMissingMax;
  if (queue0) *queue0 = s_statQueueDepth[0];
  if (queue1) *queue1 = s_statQueueDepth[1];
  if (queue2) *queue2 = s_statQueueDepth[2];
  if (post_queue0) *post_queue0 = s_statPostQueueDepth[0];
  if (post_queue1) *post_queue1 = s_statPostQueueDepth[1];
  if (post_queue2) *post_queue2 = s_statPostQueueDepth[2];
  if (play_fails) *play_fails = s_statPlayFails;
  s_statBlocks = 0;
  s_statUnderflows = 0;
  s_statMaxAvailable = 0;
  s_statMaxQueueDepth = 0;
  s_statMinAvailable = 0xffffffffu;
  s_statAvailableSum = 0;
  s_statMissingTotal = 0;
  s_statMissingMax = 0;
  s_statQueueDepth[0] = 0;
  s_statQueueDepth[1] = 0;
  s_statQueueDepth[2] = 0;
  s_statPostQueueDepth[0] = 0;
  s_statPostQueueDepth[1] = 0;
  s_statPostQueueDepth[2] = 0;
  s_statPlayFails = 0;
}
#endif
