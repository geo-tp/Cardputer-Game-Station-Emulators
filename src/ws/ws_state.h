#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ws_state_init(const char* romPathOrName);
void ws_state_request_save(void);
void ws_state_request_load(void);
void ws_state_tick(void);
bool ws_state_save_now(void);
bool ws_state_load_now(void);

#ifdef __cplusplus
}
#endif
