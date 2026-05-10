#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ws_display_init(void);
void ws_display_start(void);
void ws_display_stop(void);
#ifdef BENCHMARK_LOGS
void ws_display_get_and_reset_stats(uint32_t* frames,
                                    uint32_t* total_us,
                                    uint32_t* max_us,
                                    uint32_t* pending_notifications);
#endif

/* Oswan Core hook */
void ws_graphics_paint(void);

extern bool ws_fullscreen;
extern int  ws_zoomPercent;

#ifdef __cplusplus
}
#endif
