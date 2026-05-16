#include "ngc_scheduler.h"
#include "ngc_sound.h"
#include "ngc_input.h"
#include "ngc_display.h"
#include <M5Cardputer.h>
#include <Arduino.h>

static TaskHandle_t s_taskInput  = nullptr;
static TaskHandle_t s_taskAudio  = nullptr;
static TaskHandle_t s_taskVideo  = nullptr;
static volatile bool s_running   = false;

extern volatile unsigned g_frame_ready;

#ifndef NGC_INPUT_CORE
#define NGC_INPUT_CORE  0
#endif
#ifndef NGC_AUDIO_CORE
#define NGC_AUDIO_CORE  0
#endif
#ifndef NGC_VIDEO_CORE
#define NGC_VIDEO_CORE  0
#endif

#ifndef NGC_INPUT_STACK
#define NGC_INPUT_STACK 2048
#endif
#ifndef NGC_AUDIO_STACK
#define NGC_AUDIO_STACK 2048
#endif
#ifndef NGC_VIDEO_STACK
#ifdef NGP_TRACE_LOGS
#define NGC_VIDEO_STACK 4096
#else
#define NGC_VIDEO_STACK 2048
#endif
#endif

static void taskInput(void* arg)
{
  (void)arg;
  //TickType_t last = xTaskGetTickCount();
  while (s_running) {
    //M5.update();
    ngc_input_poll();
    vTaskDelay(pdMS_TO_TICKS(32));
  }
  vTaskDelete(nullptr);
}

static void taskAudio(void* arg)
{
  (void)arg;
  TickType_t last = xTaskGetTickCount();
  while (s_running) {
    ngc_sound_frame();
    vTaskDelayUntil(&last, 16);
  }
  vTaskDelete(nullptr);
}

static void taskVideo(void*){
  while (s_running) {
    if (g_frame_ready) {
      g_frame_ready = 0;
      graphics_paint(1);
    } else {
      vTaskDelay(2);
    }
  }
}

extern "C" void ngc_scheduler_start(void)
{
  if (s_running) return;
  s_running = true;

  xTaskCreatePinnedToCore(taskInput, "ngp_input", NGC_INPUT_STACK, nullptr, 6, &s_taskInput, NGC_INPUT_CORE);
  xTaskCreatePinnedToCore(taskAudio, "ngp_audio", NGC_AUDIO_STACK, nullptr, 6, &s_taskAudio, NGC_AUDIO_CORE);
  xTaskCreatePinnedToCore(taskVideo, "ngp_video", NGC_VIDEO_STACK, nullptr, 6, &s_taskVideo, NGC_VIDEO_CORE);
}

extern "C" void ngc_scheduler_stop(void)
{
  if (!s_running) return;
  s_running = false;
}
