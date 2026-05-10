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

#ifdef BENCHMARK_LOGS
typedef struct WsCoreStats {
    unsigned int frames;
    unsigned int cpuSteps;
    unsigned int refreshLines;
    unsigned int paintRequests;
    unsigned int apuTicks;
    unsigned int gdmaTransfers;
    unsigned int gdmaBytes;
    unsigned int keyIrqs;
    unsigned int htimerIrqs;
    unsigned int vtimerIrqs;
    unsigned int vblankIrqs;
    unsigned int lineIrqs;
    int frameSkip;
} WsCoreStats;
#endif

void WriteIO(DWORD A, BYTE V);
void WsReset (void);
void WsRomPatch(BYTE *buf);
int WsRun(void);
#ifdef BENCHMARK_LOGS
void WsGetAndResetStats(WsCoreStats* out);
#endif
void WsSplash(void);
void WsCpyPdata(BYTE* dst);
void Sleep(int);
void SetHVMode(int Mode);

void WsInit(void);
void WsDeInit(void);

#endif
