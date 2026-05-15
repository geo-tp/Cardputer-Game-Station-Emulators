#pragma once
/* Global log gating for C code.
 * When REMOVE_PRINTF is defined, printf becomes a no-op unless logs are enabled. */
#include <stdio.h>

#if defined(REMOVE_PRINTF) && !defined(BENCHMARK_LOGS) && !defined(WS_LOGS_ENABLED) && !defined(COLECO_DEBUG_LOGS) && !defined(__cplusplus)
  #ifdef printf
    #undef printf
  #endif
  #define printf(...) ((int)0)
#endif
