#pragma once
#include <stdint.h>

// Not implemented hooks from Snes9x 

#ifdef __cplusplus
extern "C" {
#endif

void S9xDeinitDisplay(void);
void S9xExtraUsage(void);
void S9xExit(void);
void S9xMessage(int type, int number, const char *message);

bool S9xReadMousePosition(int32_t which1, int32_t *x, int32_t *y, uint32_t *buttons);
bool S9xReadSuperScopePosition(int32_t *x, int32_t *y, uint32_t *buttons);

bool JustifierOffscreen(void);
void JustifierButtons(uint32_t *justifiers);

#ifdef SNES_NO_SOUND
void    S9xAPUWritePort(int32_t port, uint8_t value);
uint8_t S9xAPUReadPort(int32_t port);
void    S9xResetAPU(void);
#endif

#ifdef __cplusplus
}
#endif
