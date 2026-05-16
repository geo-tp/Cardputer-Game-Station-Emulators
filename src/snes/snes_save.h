#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void snes_save_prepare_sram(void);
void snes_save_init(const char* romPathOrName);
void snes_save_load(void);
void snes_save_tick(void);
void snes_save_request_flush(void);
void snes_save_force_flush(void);
bool snes_save_has_sram();

#ifdef __cplusplus
}
#endif
