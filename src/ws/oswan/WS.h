/*
$Date: 2009-10-30 05:26:46 +0100 (ven., 30 oct. 2009) $
$Rev: 71 $
*/

#ifndef WS_H_
#define WS_H_

#include "WSHard.h"

struct EEPROM
{
    WORD *data;
    int we;
};

extern int Run;
extern BYTE *Page[0x10];
extern BYTE* IRAM;
extern BYTE *IO;
extern BYTE *MemDummy;
extern BYTE **ROMMap;     // C-ROM�o���N�}�b�v
extern int ROMBanks;            // C-ROM�o���N��
extern BYTE **RAMMap;     // C-RAM�o���N�}�b�v
extern int RAMBanks;            // C-RAM�o���N��
extern int RAMSize;             // C-RAM���e��
extern WORD IEep[64];
extern struct EEPROM sIEep;
extern struct EEPROM sCEep;

#define CK_EEP 1
extern int CartKind;
extern int FrameSkip;

#ifdef BENCHMARK_LOGS
typedef struct WsCoreStats {
    unsigned int frames;
    unsigned int cpuSteps;
    unsigned int refreshLines;
    unsigned int paintRequests;
    unsigned int apuTicks;
    unsigned int gdmaTransfers;
    unsigned int gdmaBytes;
    unsigned int spriteLines;
    unsigned int spriteCandidates;
    unsigned int spriteVisible;
    unsigned int spritePixels;
    unsigned int spriteLimitedLines;
    unsigned int spriteClipLeft;
    unsigned int spriteClipRight;
    unsigned int spriteWindowSkips;
    unsigned int spritePrioritySkips;
    unsigned int spriteTransparentSkips;
    unsigned int spriteTableBase;
    unsigned int spriteFirst;
    unsigned int spriteCountReg;
    unsigned int spriteCached;
    unsigned int spriteWrapped;
    unsigned int keyIrqs;
    unsigned int htimerIrqs;
    unsigned int vtimerIrqs;
    unsigned int vblankIrqs;
    unsigned int lineIrqs;
    unsigned int sramBankSwitches;
#ifdef WS_RENDER_MICROBENCH
    unsigned int renderLines;
    unsigned int renderClearUs;
    unsigned int renderBgUs;
    unsigned int renderFgUs;
    unsigned int renderSpriteWindowUs;
    unsigned int renderSpriteScanUs;
    unsigned int renderSpriteDrawUs;
    unsigned int renderBgDecodeCalls;
    unsigned int renderBgDecodeUs;
    unsigned int renderFgDecodeCalls;
    unsigned int renderFgDecodeUs;
    unsigned int renderSpriteDecodeCalls;
    unsigned int renderSpriteDecodeUs;
#endif
    int frameSkip;
} WsCoreStats;
#endif

void WriteIO(DWORD A, BYTE V);
void WsReset (void);
void WsSramBackingInit(int banks);
void WsSramBackingSelect(int bank);
BYTE WsSramBackingRead(int offset);
void WsSramBackingWrite(int offset, BYTE value);
void WsSramBackingClose(void);
void WsSramBackingFlush(void);
int WsSramBackingActive(void);
int WsSramBackingDirty(void);
void WsSramBackingClearDirty(void);
unsigned int WsSramBackingDirtyPages(void);
void WsSramBackingClearDirtyPages(unsigned int mask);
void WsRomPatch(BYTE *buf);
int WsRun(void);
#ifdef BENCHMARK_LOGS
void WsGetAndResetStats(WsCoreStats* out);
void WsBenchSpriteLine(unsigned int candidates, unsigned int visible,
                       unsigned int pixels, unsigned int clipLeft,
                       unsigned int clipRight, unsigned int windowSkips,
                       unsigned int prioritySkips, unsigned int transparentSkips,
                       unsigned int limited);
#ifdef WS_RENDER_MICROBENCH
void WsBenchRenderLine(unsigned int clearUs, unsigned int bgUs,
                       unsigned int fgUs, unsigned int spriteWindowUs,
                       unsigned int spriteScanUs, unsigned int spriteDrawUs,
                       unsigned int bgDecodeCalls, unsigned int bgDecodeUs,
                       unsigned int fgDecodeCalls, unsigned int fgDecodeUs,
                       unsigned int spriteDecodeCalls, unsigned int spriteDecodeUs);
#endif
#endif
void WsSplash(void);
void WsCpyPdata(BYTE* dst);
void Sleep(int);
void SetHVMode(int Mode);

void WsInit(void);
void WsDeInit(void);

#endif
