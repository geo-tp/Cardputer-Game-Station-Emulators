#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
  #include "sms/smsplus/shared.h"
  #include "sms/smsplus/vdp.h"
}
#endif

enum SmsConsoleMode {
  SMS_MODE_SMS = 0,
  SMS_MODE_GG,
  SMS_MODE_SG1000,
  SMS_MODE_COLECO
};

void run_sms(const uint8_t* romPtr, size_t romLen, SmsConsoleMode mode, const char* romName,
             const uint8_t* colecoBiosPtr = nullptr, size_t colecoBiosLen = 0);
