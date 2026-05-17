#pragma once

extern "C" {
    #include "sms/smsplus/shared.h"
}

#include <M5Cardputer.h>
#include "display.h"  // fullscreen/scanline bool
#include "run_sms.h"

extern bool fullscreen;
extern bool scanline;

void cardputer_input_init();
void cardputer_read_input(SmsConsoleMode mode);
