/*
$Date: 2009-10-30 05:26:46 +0100 (ven., 30 oct. 2009) $
$Rev: 71 $
*/
#include <string.h>

#ifdef WS_PPU_IRAM
#include <esp_attr.h>
#define WS_PPU_CODE IRAM_ATTR
#else
#define WS_PPU_CODE
#endif

#include "WSRender.h"
#include "WS.h"
#include "WSSegment.h"

#if defined(BENCHMARK_LOGS) && defined(WS_RENDER_PROFILE)
#define WS_RENDER_PROFILE_ON 1
extern unsigned long SDL_UXTimerRead(void);

static inline unsigned int WsRenderElapsedUs(unsigned long start)
{
    return (unsigned int)(SDL_UXTimerRead() - start);
}

#define WS_RENDER_DECODE_ROW(calls, index, data, packedMode, color16, hrev) \
    do { \
        (calls)++; \
        DecodeTileRow((index), (data), (packedMode), (color16), (hrev)); \
    } while (0)
#else
#define WS_RENDER_PROFILE_ON 0
#define WS_RENDER_DECODE_ROW(calls, index, data, packedMode, color16, hrev) \
    DecodeTileRow((index), (data), (packedMode), (color16), (hrev))
#endif

#define MAP_TILE 0x01FF
#define MAP_PAL  0x1E00
#define MAP_BANK 0x2000
#define MAP_HREV 0x4000
#define MAP_VREV 0x8000
BYTE *Scr1TMap;
BYTE *Scr2TMap;

#define SPR_TILE 0x01FF
#define SPR_PAL  0x0E00
#define SPR_CLIP 0x1000
#define SPR_LAYR 0x2000
#define SPR_HREV 0x4000
#define SPR_VREV 0x8000
#define SEG_X (LCD_MAIN_W - 32)
#define STRIDE 256

BYTE *SprTTMap;
BYTE *SprETMap;
BYTE* SprTMap = NULL;
WsSpriteMeta SprMeta[128];
int SprMetaCount = 0;
WORD* FrameBuffer = NULL;
static WORD* FrameBufferAlloc = NULL;
WORD (*Palette)[16] = NULL;
WORD MonoColor[8];
int Layer[3] = {1, 1, 1};
int Segment[11];
static DWORD PlaneLut[256];

#ifdef WS_USE_SEGMENT_BUFFER
    WORD* SegmentBuffer = NULL;
#endif

static void InitTileDecodeLut(void)
{
    for(int b = 0; b < 256; ++b)
    {
        DWORD packed = 0;
        for(int x = 0; x < 8; ++x)
        {
            if(b & (0x80 >> x))
            {
                packed |= (DWORD)1 << (x << 2);
            }
        }
        PlaneLut[b] = packed;
    }
}

static inline void StorePackedPixels(BYTE* index, DWORD pixels, int hrev)
{
    if(hrev)
    {
        index[0] = (BYTE)((pixels >> 28) & 0x0F);
        index[1] = (BYTE)((pixels >> 24) & 0x0F);
        index[2] = (BYTE)((pixels >> 20) & 0x0F);
        index[3] = (BYTE)((pixels >> 16) & 0x0F);
        index[4] = (BYTE)((pixels >> 12) & 0x0F);
        index[5] = (BYTE)((pixels >>  8) & 0x0F);
        index[6] = (BYTE)((pixels >>  4) & 0x0F);
        index[7] = (BYTE)( pixels        & 0x0F);
    }
    else
    {
        index[0] = (BYTE)( pixels        & 0x0F);
        index[1] = (BYTE)((pixels >>  4) & 0x0F);
        index[2] = (BYTE)((pixels >>  8) & 0x0F);
        index[3] = (BYTE)((pixels >> 12) & 0x0F);
        index[4] = (BYTE)((pixels >> 16) & 0x0F);
        index[5] = (BYTE)((pixels >> 20) & 0x0F);
        index[6] = (BYTE)((pixels >> 24) & 0x0F);
        index[7] = (BYTE)((pixels >> 28) & 0x0F);
    }
}

static inline void DecodeTileRow(BYTE* index, const BYTE* data, int packedMode, int color16, int hrev)
{
    DWORD pixels;

    if(packedMode)
    {
        if(color16)
        {
            pixels  = (DWORD)((data[0] >> 4) & 0x0F);
            pixels |= (DWORD)( data[0]       & 0x0F) << 4;
            pixels |= (DWORD)((data[1] >> 4) & 0x0F) << 8;
            pixels |= (DWORD)( data[1]       & 0x0F) << 12;
            pixels |= (DWORD)((data[2] >> 4) & 0x0F) << 16;
            pixels |= (DWORD)( data[2]       & 0x0F) << 20;
            pixels |= (DWORD)((data[3] >> 4) & 0x0F) << 24;
            pixels |= (DWORD)( data[3]       & 0x0F) << 28;
        }
        else
        {
            pixels  = (DWORD)((data[0] >> 6) & 0x03);
            pixels |= (DWORD)((data[0] >> 4) & 0x03) << 4;
            pixels |= (DWORD)((data[0] >> 2) & 0x03) << 8;
            pixels |= (DWORD)( data[0]       & 0x03) << 12;
            pixels |= (DWORD)((data[1] >> 6) & 0x03) << 16;
            pixels |= (DWORD)((data[1] >> 4) & 0x03) << 20;
            pixels |= (DWORD)((data[1] >> 2) & 0x03) << 24;
            pixels |= (DWORD)( data[1]       & 0x03) << 28;
        }
    }
    else
    {
        pixels = PlaneLut[data[0]] | (PlaneLut[data[1]] << 1);
        if(color16)
        {
            pixels |= (PlaneLut[data[2]] << 2) | (PlaneLut[data[3]] << 3);
        }
    }

    StorePackedPixels(index, pixels, hrev);
}

void AllocateBuffers(void) {
    InitTileDecodeLut();
    Palette = (WORD (*)[16])calloc(16, sizeof(*Palette));

    // SprTMap : 512 bytes
    SprTMap = (BYTE*)malloc(512 * sizeof(BYTE));
    memset(SprTMap, 0, 512 * sizeof(BYTE));

    // FrameBuffer has a small left guard because scrolled tile rendering can
    // start up to 7 pixels before x=0 on the first line.
    FrameBufferAlloc = (WORD*)malloc((LINE_SIZE * LCD_MAIN_H + 8) * sizeof(WORD));
    memset(FrameBufferAlloc, 0, (LINE_SIZE * LCD_MAIN_H + 8) * sizeof(WORD));
    FrameBuffer = FrameBufferAlloc + 8;

#ifdef WS_USE_SEGMENT_BUFFER
    // SegmentBuffer : (LCD_MAIN_H * 4) * (8 * 4) WORDs
    SegmentBuffer = (WORD*)malloc((LCD_MAIN_H * 4) * (8 * 4) * sizeof(WORD));
    memset(SegmentBuffer, 0, (LCD_MAIN_H * 4) * (8 * 4) * sizeof(WORD));
#endif
}

void WsPrecomputeSpriteTable(int count)
{
    if(!SprTMap)
    {
        SprMetaCount = 0;
        return;
    }
    if(count < 0)
    {
        count = 0;
    }
    if(count > 128)
    {
        count = 128;
    }
    SprMetaCount = count;
    for(int i = 0; i < count; ++i)
    {
        const BYTE* spr = SprTMap + (i << 2);
        SprMeta[i].map = (WORD)(spr[0] | (spr[1] << 8));
        SprMeta[i].y = (short)((spr[2] > 0xF8) ? (int)spr[2] - 0x100 : (int)spr[2]);
        SprMeta[i].x = (short)((spr[3] > 0xF8) ? (int)spr[3] - 0x100 : (int)spr[3]);
    }
}

void FreeBuffers(void) {
    if (SprTMap) {
        free(SprTMap);
        SprTMap = NULL;
    }
    SprMetaCount = 0;
    if (FrameBufferAlloc) {
        free(FrameBufferAlloc);
        FrameBufferAlloc = NULL;
        FrameBuffer = NULL;
    }

#ifdef WS_USE_SEGMENT_BUFFER
    if (SegmentBuffer) {
        free(SegmentBuffer);
        SegmentBuffer = NULL;
    }
#endif
}

void SetPalette(int addr)
{
    WORD color, r, g, b;

    // RGB444 format
    color = *(WORD*)(IRAM + (addr & 0xFFFE));
	// RGB565
	r = (color & 0x0F00) << 4;
	g = (color & 0x00F0) << 3;
	b = (color & 0x000F) << 1;
    Palette[(addr & 0x1E0) >> 5][(addr & 0x1E) >> 1] = r | g | b;
}

WS_PPU_CODE void RefreshLine(int Line)
{
    WORD *pSBuf;            // �f�[�^�������݃o�b�t�@
    WORD *pSWrBuf;          // ���̏������݈ʒu�p�|�C���^
    BYTE *pZ;               // priority mask, values 0/1
    BYTE ZBuf[0x100];
    BYTE *pW;               // window mask, values 0/1
    BYTE WBuf[0x100];
    int OffsetX;            // 
    int OffsetY;            // 
    BYTE *pbTMap;           // 
    int TMap;               // 
    int TMapX;              // 
    int TMapXEnd;           // 
    BYTE *pbTData;          // 
    int PalIndex;               // 
    unsigned int i;
    BYTE index[8];
    WORD BaseCol;           // 
    const int packedMode = COLCTL & 0x20;
    const int color16 = COLCTL & 0x40;
#ifdef BENCHMARK_LOGS
    unsigned int sprCandidates = 0;
    unsigned int sprVisible = 0;
    unsigned int sprPixels = 0;
    unsigned int sprLimited = 0;
    unsigned int sprClipLeft = 0;
    unsigned int sprClipRight = 0;
    unsigned int sprWindowSkips = 0;
    unsigned int sprPrioritySkips = 0;
    unsigned int sprTransparentSkips = 0;
#endif
#if WS_RENDER_PROFILE_ON
    unsigned int renderClearUs = 0;
    unsigned int renderBgUs = 0;
    unsigned int renderFgUs = 0;
    unsigned int renderSpriteWindowUs = 0;
    unsigned int renderSpriteScanUs = 0;
    unsigned int renderSpriteDrawUs = 0;
    unsigned int bgDecodeCalls = 0;
    unsigned int fgDecodeCalls = 0;
    unsigned int spriteDecodeCalls = 0;
    unsigned long renderSectionStart;
#endif
    pSBuf = FrameBuffer + Line * SCREEN_WIDTH;
    pSWrBuf = pSBuf;

#if WS_RENDER_PROFILE_ON
    renderSectionStart = SDL_UXTimerRead();
#endif
    if(LCDSLP & 0x01)
    {
        if(COLCTL & 0xE0)
        {
            BaseCol = Palette[(BORDER & 0xF0) >> 4][BORDER & 0x0F];
        }
        else
        {
            BaseCol = MonoColor[BORDER & 0x07];
        }
    }
    else
    {
        BaseCol = 0;
    }
    for(i = 0; i < LCD_MAIN_W; i++)
    {
        {
            *pSWrBuf++ = BaseCol;
        }
    }
#if WS_RENDER_PROFILE_ON
    renderClearUs += WsRenderElapsedUs(renderSectionStart);
#endif
    if(!(LCDSLP & 0x01))
    {
#if WS_RENDER_PROFILE_ON
        WsBenchRenderLine(renderClearUs, renderBgUs, renderFgUs,
                          renderSpriteWindowUs, renderSpriteScanUs,
                          renderSpriteDrawUs, bgDecodeCalls, fgDecodeCalls,
                          spriteDecodeCalls);
#endif
        return;
    }
/*********************************************************************/
#if WS_RENDER_PROFILE_ON
    renderSectionStart = SDL_UXTimerRead();
#endif
    if((DSPCTL & 0x01) && Layer[0])                                 //BG layer
    {
        OffsetX = SCR1X & 0x07;
        pSWrBuf = pSBuf - OffsetX;
        i = Line + SCR1Y;
        OffsetY = (i & 0x07);

        pbTMap = Scr1TMap + ((i & 0xF8) << 3);
        TMapX = (SCR1X & 0xF8) >> 2;
        TMapXEnd = ((SCR1X + LCD_MAIN_W + 7) >> 2) & 0xFFE;

        for(; TMapX < TMapXEnd;)
        {
            TMap = *(pbTMap + (TMapX++ & 0x3F));
            TMap |= *(pbTMap + (TMapX++ & 0x3F)) << 8;

            if(color16) // 16 colors
            {
                if(TMap & MAP_BANK)
                {
                    pbTData = IRAM + 0x8000;
                }
                else
                {
                    pbTData = IRAM + 0x4000;
                }
                pbTData += (TMap & MAP_TILE) << 5;
                if(TMap & MAP_VREV)
                {
                    pbTData += (7 - OffsetY) << 2;
                }
                else
                {
                    pbTData += OffsetY << 2;
                }
            }
            else
            {
                if((COLCTL & 0x80) && (TMap & MAP_BANK))// 4 colors and bank 1
                {
                    pbTData = IRAM + 0x4000;
                }
                else
                {
                    pbTData = IRAM + 0x2000;
                }
                pbTData += (TMap & MAP_TILE) << 4;
                if(TMap & MAP_VREV)
                {
                    pbTData += (7 - OffsetY) << 1;
                }
                else
                {
                    pbTData += OffsetY << 1;
                }
            }

            WS_RENDER_DECODE_ROW(bgDecodeCalls, index, pbTData, packedMode,
                                 color16, TMap & MAP_HREV);
            const int zeroTransparent = color16 || (TMap & 0x0800);

            PalIndex = (TMap & MAP_PAL) >> 9;
            if((!index[0]) && zeroTransparent) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[0]];
            }
            if((!index[1]) && zeroTransparent) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[1]];
            }
            if((!index[2]) && zeroTransparent) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[2]];
            }
            if((!index[3]) && zeroTransparent) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[3]];
            }
            if((!index[4]) && zeroTransparent) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[4]];
            }
            if((!index[5]) && zeroTransparent) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[5]];
            }
            if((!index[6]) && zeroTransparent) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[6]];
            }
            if((!index[7]) && zeroTransparent) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[7]];
            }
        }
    }
#if WS_RENDER_PROFILE_ON
    renderBgUs += WsRenderElapsedUs(renderSectionStart);
#endif
/*********************************************************************/
#if WS_RENDER_PROFILE_ON
    renderSectionStart = SDL_UXTimerRead();
#endif
    memset(ZBuf, 0, sizeof(ZBuf));
    if((DSPCTL & 0x02) && Layer[1])          //FG layer�\��
    {
        if((DSPCTL & 0x30) == 0x20) // �E�B���h�E�����݂̂ɕ\��
        {
            memset(WBuf + 8, 1, LCD_MAIN_W);
            if((Line >= SCR2WT) && (Line <= SCR2WB))
            {
                if((SCR2WL < LCD_MAIN_W) && (SCR2WL <= SCR2WR))
                {
                    int width = SCR2WR - SCR2WL + 1;
                    if(SCR2WL + width > LCD_MAIN_W) width = LCD_MAIN_W - SCR2WL;
                    memset(WBuf + 8 + SCR2WL, 0, width);
                }
            }
        }
        else if((DSPCTL & 0x30) == 0x30) // �E�B���h�E�O���݂̂ɕ\��
        {
            memset(WBuf + 8, 0, LCD_MAIN_W);
            if((Line >= SCR2WT) && (Line <= SCR2WB))
            {
                if((SCR2WL < LCD_MAIN_W) && (SCR2WL <= SCR2WR))
                {
                    int width = SCR2WR - SCR2WL + 1;
                    if(SCR2WL + width > LCD_MAIN_W) width = LCD_MAIN_W - SCR2WL;
                    memset(WBuf + 8 + SCR2WL, 1, width);
                }
            }
        }
        else
        {
            memset(WBuf + 8, 0, LCD_MAIN_W);
        }

        OffsetX = SCR2X & 0x07;
        pSWrBuf = pSBuf - OffsetX;
        i = Line + SCR2Y;
        OffsetY = (i & 0x07);

        pbTMap = Scr2TMap + ((i & 0xF8) << 3);
        TMapX = (SCR2X & 0xF8) >> 2;
        TMapXEnd = ((SCR2X + LCD_MAIN_W + 7) >> 2) & 0xFFE;

        pW = WBuf + 8 - OffsetX;
        pZ = ZBuf + 8 - OffsetX;
        
        for(; TMapX < TMapXEnd;)
        {
            TMap = *(pbTMap + (TMapX++ & 0x3F));
            TMap |= *(pbTMap + (TMapX++ & 0x3F)) << 8;

            if(color16)
            {
                if(TMap & MAP_BANK)
                {
                    pbTData = IRAM + 0x8000;
                }
                else
                {
                    pbTData = IRAM + 0x4000;
                }
                pbTData += (TMap & MAP_TILE) << 5;
                if(TMap & MAP_VREV)
                {
                    pbTData += (7 - OffsetY) << 2;
                }
                else
                {
                    pbTData += OffsetY << 2;
                }
            }
            else
            {
                if((COLCTL & 0x80) && (TMap & MAP_BANK))// 4 colors and bank 1
                {
                    pbTData = IRAM + 0x4000;
                }
                else
                {
                    pbTData = IRAM + 0x2000;
                }
                pbTData += (TMap & MAP_TILE) << 4;
                if(TMap & MAP_VREV)
                {
                    pbTData += (7 - OffsetY) << 1;
                }
                else
                {
                    pbTData += OffsetY << 1;
                }
            }

            WS_RENDER_DECODE_ROW(fgDecodeCalls, index, pbTData, packedMode,
                                 color16, TMap & MAP_HREV);
            const int zeroTransparent = color16 || (TMap & 0x0800);

            PalIndex = (TMap & MAP_PAL) >> 9;
            if(((!index[0]) && zeroTransparent) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[0]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[1]) && zeroTransparent) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[1]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[2]) && zeroTransparent) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[2]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[3]) && zeroTransparent) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[3]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[4]) && zeroTransparent) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[4]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[5]) && zeroTransparent) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[5]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[6]) && zeroTransparent) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[6]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[7]) && zeroTransparent) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[7]];
                *pZ = 1;
            }
            pW++;pZ++;
        }
    }
#if WS_RENDER_PROFILE_ON
    renderFgUs += WsRenderElapsedUs(renderSectionStart);
#endif
/*********************************************************************/
    if((DSPCTL & 0x04) && Layer[2])          //sprite
    {
#if WS_RENDER_PROFILE_ON
        renderSectionStart = SDL_UXTimerRead();
#endif
        if (DSPCTL & 0x08)      //sprite window
        {
            memset(WBuf + 8, 1, LCD_MAIN_W);
            if ((Line >= SPRWT) && (Line <= SPRWB))
            {
                if((SPRWL < LCD_MAIN_W) && (SPRWL <= SPRWR))
                {
                    int width = SPRWR - SPRWL + 1;
                    if(SPRWL + width > LCD_MAIN_W) width = LCD_MAIN_W - SPRWL;
                    memset(WBuf + 8 + SPRWL, 0, width);
                }
            }
        }
#if WS_RENDER_PROFILE_ON
        renderSpriteWindowUs += WsRenderElapsedUs(renderSectionStart);
        renderSectionStart = SDL_UXTimerRead();
#endif

        int lineSpriteCount = 0;
        const WsSpriteMeta* lineSprites[32];
        if (SprMetaCount > 0)
        {
            for (int metaIndex = 0; metaIndex < SprMetaCount; ++metaIndex)
            {
                const WsSpriteMeta* spriteMeta = &SprMeta[metaIndex];
#ifdef BENCHMARK_LOGS
                sprCandidates++;
#endif
                const int testY = spriteMeta->y;
                if (Line < testY)
                    continue;
                if (Line >= testY + 8)
                    continue;

                lineSprites[lineSpriteCount++] = spriteMeta;
                if (lineSpriteCount == 32)
                {
#ifdef BENCHMARK_LOGS
                    sprLimited++;
#endif
                    break;
                }
            }
        }
#if WS_RENDER_PROFILE_ON
        renderSpriteScanUs += WsRenderElapsedUs(renderSectionStart);
        renderSectionStart = SDL_UXTimerRead();
#endif

        for (int spriteIndex = lineSpriteCount - 1; spriteIndex >= 0; --spriteIndex)
        {
            const WsSpriteMeta* spriteMeta = lineSprites[spriteIndex];
            TMap = spriteMeta->map;

            const int sprY = spriteMeta->y;
            const int sprX = spriteMeta->x;

            if (sprX <= -8)
                continue;
            if (LCD_MAIN_W <= sprX)
                continue;

#ifdef BENCHMARK_LOGS
            sprVisible++;
#endif
            int firstPixel = 0;
            int lastPixel = 7;
            if (sprX < 0)
            {
                firstPixel = -sprX;
#ifdef BENCHMARK_LOGS
                sprClipLeft++;
#endif
            }
            if (sprX + 8 > LCD_MAIN_W)
            {
                lastPixel = LCD_MAIN_W - 1 - sprX;
#ifdef BENCHMARK_LOGS
                sprClipRight++;
#endif
            }
            pSWrBuf = pSBuf + sprX + firstPixel;

            if (color16)
            {
                pbTData = IRAM + 0x4000;
                pbTData += (TMap & SPR_TILE) << 5;
                if (TMap & SPR_VREV)
                {
                    pbTData += (7 - Line + sprY) << 2;
                }
                else
                {
                    pbTData += (Line - sprY) << 2;
                }
            }
            else
            {
                pbTData = IRAM + 0x2000;
                pbTData += (TMap & SPR_TILE) << 4;
                if (TMap & SPR_VREV)
                {
                    pbTData += (7 - Line + sprY) << 1;
                }
                else
                {
                    pbTData += (Line - sprY) << 1;
                }
            }

            WS_RENDER_DECODE_ROW(spriteDecodeCalls, index, pbTData, packedMode,
                                 color16, TMap & SPR_HREV);
            const int zeroTransparent = color16 || (TMap & 0x0800);

            pW = WBuf + 8 + sprX + firstPixel;
            pZ = ZBuf + 8 + sprX + firstPixel;
            PalIndex = ((TMap & SPR_PAL) >> 9) + 8;
            for(i = firstPixel; i <= (unsigned int)lastPixel; i++, pZ++, pW++)
            {
                if(DSPCTL & 0x08)
                {
                    if(TMap & SPR_CLIP)
                    {
                        if(!*pW)
                        {
                            pSWrBuf++;
#ifdef BENCHMARK_LOGS
                            sprWindowSkips++;
#endif
                            continue;
                        }
                    }
                    else
                    {
                        if(*pW)
                        {
                            pSWrBuf++;
#ifdef BENCHMARK_LOGS
                            sprWindowSkips++;
#endif
                            continue;
                        }
                    }
                }
                if((!index[i]) && zeroTransparent)
                {
                    pSWrBuf++;
#ifdef BENCHMARK_LOGS
                    sprTransparentSkips++;
#endif
                    continue;
                }
                if((*pZ) && (!(TMap & SPR_LAYR)))
                {
                    pSWrBuf++;
#ifdef BENCHMARK_LOGS
                    sprPrioritySkips++;
#endif
                    continue;
                }
                *pSWrBuf++ = Palette[PalIndex][index[i]];
#ifdef BENCHMARK_LOGS
                sprPixels++;
#endif
            }
        }
#if WS_RENDER_PROFILE_ON
        renderSpriteDrawUs += WsRenderElapsedUs(renderSectionStart);
#endif
    }
#ifdef BENCHMARK_LOGS
    WsBenchSpriteLine(sprCandidates, sprVisible, sprPixels, sprClipLeft,
                      sprClipRight, sprWindowSkips, sprPrioritySkips,
                      sprTransparentSkips, sprLimited);
#endif
#if WS_RENDER_PROFILE_ON
    WsBenchRenderLine(renderClearUs, renderBgUs, renderFgUs,
                      renderSpriteWindowUs, renderSpriteScanUs,
                      renderSpriteDrawUs, bgDecodeCalls, fgDecodeCalls,
                      spriteDecodeCalls);
#endif
}

/*
 8 * 144 �̃T�C�Y�� 32 * 576 �ŕ`��
*/

#ifdef WS_USE_SEGMENT_BUFFER
void RenderSegment(void)
{
	int bit, x, y, i;
	WORD* p = SegmentBuffer;

	for (i = 0; i < 11; i++)
	{
		for (y = 0; y < segLine[i]; y++)
		{
			for (x = 0; x < 4; x++)
			{
				BYTE ch = seg[i][y * 4 + x];
				for (bit = 0; bit < 8; bit++)
				{
					if (ch & 0x80)
					{
						if (Segment[i])
						{
							*p++ = 0xFCCC;
						}
						else
						{
							*p++ = 0xF222;
						}
					}
					else
					{
						*p++ = 0xF000;
					}
					ch <<= 1;
				}
			}
		}
	}
}
#else
// Without SegmentBuffer
void RenderSegment(void)
{
    for (int yOut = 0; yOut < LCD_MAIN_H; yOut++)
    {
        const int yStart = yOut * 4;
        const int yEnd   = yStart + 3;

        // Accumulation 32 
        unsigned char accum[4] = {0, 0, 0, 0};

        int yBase = 0;
        for (int i = 0; i < 11; i++)
        {
            const int lines = segLine[i];

            int first = yStart - yBase; if (first < 0) first = 0;
            int last  = yEnd   - yBase; if (last >= lines) last = lines - 1;

            if (first <= last)
            {
                const unsigned char* s = seg[i] + first * 4; // 4 bytes per sub-line
                for (int y = first; y <= last; y++)
                {
                    accum[0] |= s[0];
                    accum[1] |= s[1];
                    accum[2] |= s[2];
                    accum[3] |= s[3];
                    s += 4;
                }
            }
            yBase += lines;
        }

        int anyActive = 0; yBase = 0;
        for (int i = 0; i < 11; i++)
        {
            int first = yStart - yBase; if (first < 0) first = 0;
            int last  = yEnd   - yBase; if (last >= segLine[i]) last = segLine[i] - 1;
            if (first <= last && Segment[i]) { anyActive = 1; break; }
            yBase += segLine[i];
        }

        const WORD onCol  = anyActive ? 0xFCCC : 0xF222;
        const WORD offCol = 0xF000;

        if (SEG_X >= LCD_MAIN_W) continue;           // completely offscreen to the right

        int maxWidth = LCD_MAIN_W - SEG_X;           // clip to screen
        if (maxWidth > 32) maxWidth = 32;

        WORD* dst = FrameBuffer + yOut * SCREEN_WIDTH + SEG_X;

        // 32 pixels MSB first
        int written = 0;
        for (int byte = 0; byte < 4 && written < maxWidth; byte++)
        {
            unsigned char v = accum[byte];
            for (int bit = 0; bit < 8 && written < maxWidth; bit++, written++)
            {
                *dst++ = (v & 0x80) ? onCol : offCol;
                v <<= 1;
            }
        }
    }
}
#endif

void RenderSleep(void)
{
    int x, y;
    WORD* p;

    // �w�i���O���C�ŃN���A
    p = FrameBuffer;
    for (y = 0; y < LCD_MAIN_H; y++)
    {
        for (x = 0; x < LCD_MAIN_W; x++)
        {
            *p++ = 0x4208;
        }
    }
	p += SCREEN_WIDTH - LCD_MAIN_W;
}
