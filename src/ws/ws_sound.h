#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ws_sound_init(int sample_rate_hz);
void ws_sound_set_volume(uint8_t vol);
void ws_sound_shutdown(void);
void ws_sound_frame(void);
void ws_sound_start_task(uint32_t period_ms, int core);
void ws_sound_stop_task(void);
#ifdef BENCHMARK_LOGS
void ws_sound_get_and_reset_stats(uint32_t* blocks,
                                  uint32_t* underflows,
                                  uint32_t* max_available,
                                  uint32_t* max_queue_depth);
#endif

#ifdef __cplusplus
}
#endif
