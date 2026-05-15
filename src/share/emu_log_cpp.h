#pragma once

#include <cstdio>

#if defined(REMOVE_PRINTF) && !defined(BENCHMARK_LOGS) && !defined(WS_LOGS_ENABLED)
  #define EMU_LOG(...) ((int)0)
#else
  #define EMU_LOG(...) ::printf(__VA_ARGS__)
#endif
