#pragma once

#include <stdint.h>

#include "msx_host.h"

#include <M5Cardputer.h>
#ifdef word
#undef word
#endif
#define word arduino_word

extern "C" {
#include "fMSX/MSX.h"
#include "EMULib/EMULib.h"
#include "EMULib/Sound.h"
}
#undef word

namespace msx {

constexpr int kSrcW = 272;
constexpr int kSrcH = 228;
constexpr int kDstW = 240;
constexpr int kDstH = 135;
constexpr int kFit43W = 216;
constexpr int kAudioRate = 44100;
constexpr int kAudioChunk = 256;
constexpr int kAudioBufCount = 4;
constexpr int kAudioChannel = 0;

struct BiosBlob {
    char name[24] = {0};
    const uint8_t* data = nullptr;
    unsigned int size = 0;
    bool ownedHeap = false;
};

struct HostState {
    const uint8_t* romData = nullptr;
    unsigned int romSize = 0;
    char* romName = nullptr;
    char* romPath = nullptr;
    int modelMode = MSX_MSX1;
    msx_host_view_mode_t viewMode = MSX_HOST_VIEW_FIT43;

    uint8_t* frame8 = nullptr;
    uint16_t* line565 = nullptr;
    uint16_t* rgb565 = nullptr;
    uint16_t* xmapFull = nullptr;
    uint16_t* xmap43 = nullptr;
    uint16_t* ymap = nullptr;

    sample** audioBuf = nullptr;

    // Zoom / fullscreen 
    bool fullscreen = true;
    int  zoomPercent = 120;       // 100..150
    int  zoomMapBuiltFor = -1;    // -1 = not built yet
    uint16_t* xmapZoom = nullptr;
    uint16_t* ymapZoom = nullptr;
    int audioBufIndex = 0;
    uint64_t pendingUsec = 0;
    BiosBlob bios[4];
    int biosCount = 0;
    bool cbiosFallbackActive = false;
};

extern HostState g_host;

static inline uint16_t rgb565(unsigned r, unsigned g, unsigned b)
{
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

} // namespace msx
