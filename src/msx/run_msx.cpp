#pragma GCC optimize ("Os")

#include "run_msx.h"

#include <M5Cardputer.h>
#ifdef word
#undef word
#endif
#define word arduino_word
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "msx_host.h"
#include "share/emu_log_cpp.h"

extern "C" {
#include "fMSX/MSX.h"
}
#undef word

namespace {

int choose_mode_from_extension(const char* romName)
{
    const char* dot = romName ? std::strrchr(romName, '.') : nullptr;
    if (!dot) return -1;

    if (!strcasecmp(dot, ".mx1")) return MSX_MSX1;
    return -1;
}

int choose_best_available_mode(int preferred)
{
    const int base = MSX_NTSC | MSX_GUESSA | MSX_GUESSB;

    if (preferred >= 0) {
        const int mode = base | preferred;
        if (msx_host_load_bios_for_mode(mode)) return mode;
        EMU_LOG("[MSX] Preferred BIOS set missing for requested mode\n");
    }

        const int candidates[] = { MSX_MSX1};
    for (int model : candidates) {
        const int mode = base | model;
        if (msx_host_load_bios_for_mode(mode)) return mode;
    }

    return -1;
}

} // namespace

void run_msx(const uint8_t* rom, size_t len, const char* romName, const char* romPath)
{
    EMU_LOG("[MSX] ===== fMSX Start =====\n");
    EMU_LOG("[MSX] ROM size: %u bytes (%s)\n", (unsigned)len, romName ? romName : "(null)");

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setSwapBytes(true);
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    msx_host_set_game(rom, (unsigned int)len, romName, romPath);
    msx_host_set_view_mode(MSX_HOST_VIEW_FIT43);
    EMU_LOG("[MSX] Default debug zoom enabled (~20%%)\n");

    const int preferred = choose_mode_from_extension(romName);
    const int mode = choose_best_available_mode(preferred);
    if (mode < 0) {
        EMU_LOG("[MSX][ERR] Missing BIOS. Expected MSX.ROM or full MSX2/MSX2+ sets in /sd/msx, /sd/msx_bios, or /sd/bios/msx\n");
        for (;;) delay(1000);
    }

    msx_host_set_model_mode(mode);
    EMU_LOG("[MSX] Selected model: %s\n",
           (mode & MSX_MODEL) == MSX_MSX2P ? "MSX2+" :
           (mode & MSX_MODEL) == MSX_MSX2  ? "MSX2"  : "MSX1");

    Verbose = 1;
    UPeriod = 100;

    if (!InitMachine()) {
        EMU_LOG("[MSX][ERR] InitMachine failed\n");
        for (;;) delay(1000);
    }

    if (!StartMSX(mode, 4, 2)) {
        EMU_LOG("[MSX][ERR] StartMSX failed\n");
        for (;;) delay(1000);
    }

    TrashMSX();
    TrashMachine();
    msx_host_unload_bios();
    msx_host_clear_game();
}
