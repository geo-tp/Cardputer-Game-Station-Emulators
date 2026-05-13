/*
$Date: 2009-10-30 05:26:46 +0100 (ven., 30 oct. 2009) $
$Rev: 71 $
*/

#ifndef WSRENDER_H_
#define WSRENDER_H_

#include "WSHard.h"

#define LINE_SIZE (256)

extern BYTE *Scr1TMap;
extern BYTE *Scr2TMap;
extern BYTE *SprTTMap;
extern BYTE *SprETMap;
extern BYTE *SprTMap;
typedef struct WsSpriteMeta {
    WORD map;
    short x;
    short y;
} WsSpriteMeta;
extern WsSpriteMeta SprMeta[128];
extern int SprMetaCount;
extern WORD (*Palette)[16];
extern WORD MonoColor[8];
extern WORD *FrameBuffer;
// extern WORD SegmentBuffer[(144 * 4) * (8 * 4)];
extern int Layer[3];
extern int Segment[11];

void AllocateBuffers(void);
void SetPalette(int addr);
void WsPrecomputeSpriteTable(int count);
void RefreshLine(int Line);
void RenderSegment(void);
void RenderSleep(void);

#endif
