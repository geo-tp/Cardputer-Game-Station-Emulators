#pragma once

#include <cstdio>

#if defined(REMOVE_PRINTF) && !defined(BENCHMARK_LOGS) && !defined(WS_LOGS_ENABLED) && !defined(COLECO_DEBUG_LOGS) && !defined(NES_DIAG_LOGS) && !defined(NGP_TRACE_LOGS)
  #define EMU_LOG(...) ((int)0)
#else
  #define EMU_LOG(...) ::printf(__VA_ARGS__)
#endif
