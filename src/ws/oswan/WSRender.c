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
WORD* FrameBuffer = NULL;
WORD (*Palette)[16] = NULL;
WORD MonoColor[8];
int Layer[3] = {1, 1, 1};
int Segment[11];

#ifdef WS_USE_SEGMENT_BUFFER
    WORD* SegmentBuffer = NULL;
#endif

void AllocateBuffers(void) {
    Palette = (WORD (*)[16])calloc(16, sizeof(*Palette));

    // SprTMap : 512 bytes
    SprTMap = (BYTE*)malloc(512 * sizeof(BYTE));
    memset(SprTMap, 0, 512 * sizeof(BYTE));

    // FrameBuffer : LINE_SIZE * LCD_MAIN_H WORDs
    FrameBuffer = (WORD*)malloc(LINE_SIZE * LCD_MAIN_H * sizeof(WORD));
    memset(FrameBuffer, 0, LINE_SIZE * LCD_MAIN_H * sizeof(WORD));

#ifdef WS_USE_SEGMENT_BUFFER
    // SegmentBuffer : (LCD_MAIN_H * 4) * (8 * 4) WORDs
    SegmentBuffer = (WORD*)malloc((LCD_MAIN_H * 4) * (8 * 4) * sizeof(WORD));
    memset(SegmentBuffer, 0, (LCD_MAIN_H * 4) * (8 * 4) * sizeof(WORD));
#endif
}

void FreeBuffers(void) {
    if (SprTMap) {
        free(SprTMap);
        SprTMap = NULL;
    }
    if (FrameBuffer) {
        free(FrameBuffer);
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
    unsigned int i, j, k;
    BYTE index[8];
    WORD BaseCol;           // 
    pSBuf = FrameBuffer + Line * SCREEN_WIDTH;
    pSWrBuf = pSBuf;

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
    if(!(LCDSLP & 0x01)) return;
/*********************************************************************/
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

            if(COLCTL & 0x40) // 16 colors
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

            if(COLCTL & 0x20)                       // Packed Mode
            {
                if(COLCTL & 0x40)                   // 16 Color
                {
                    index[0] = (pbTData[0] & 0xF0) >> 4;
                    index[1] = pbTData[0] & 0x0F;
                    index[2] = (pbTData[1] & 0xF0) >> 4;
                    index[3] = pbTData[1] & 0x0F;
                    index[4] = (pbTData[2] & 0xF0) >> 4;
                    index[5] = pbTData[2] & 0x0F;
                    index[6] = (pbTData[3] & 0xF0) >> 4;
                    index[7] = pbTData[3] & 0x0F;
                }
                else                            // 4 Color
                {
                    index[0] = (pbTData[0] & 0xC0) >> 6;
                    index[1] = (pbTData[0] & 0x30) >> 4;
                    index[2] = (pbTData[0] & 0x0C) >> 2;
                    index[3] = pbTData[0] & 0x03;
                    index[4] = (pbTData[1] & 0xC0) >> 6;
                    index[5] = (pbTData[1] & 0x30) >> 4;
                    index[6] = (pbTData[1] & 0x0C) >> 2;
                    index[7] = pbTData[1] & 0x03;
                }
            }
            else
            {
                if(COLCTL & 0x40)                   // 16 Color
                {
                    index[0]  = (pbTData[0] & 0x80) ? 0x1 : 0;
                    index[0] |= (pbTData[1] & 0x80) ? 0x2 : 0;
                    index[0] |= (pbTData[2] & 0x80) ? 0x4 : 0;
                    index[0] |= (pbTData[3] & 0x80) ? 0x8 : 0;
                    index[1]  = (pbTData[0] & 0x40) ? 0x1 : 0;
                    index[1] |= (pbTData[1] & 0x40) ? 0x2 : 0;
                    index[1] |= (pbTData[2] & 0x40) ? 0x4 : 0;
                    index[1] |= (pbTData[3] & 0x40) ? 0x8 : 0;
                    index[2]  = (pbTData[0] & 0x20) ? 0x1 : 0;
                    index[2] |= (pbTData[1] & 0x20) ? 0x2 : 0;
                    index[2] |= (pbTData[2] & 0x20) ? 0x4 : 0;
                    index[2] |= (pbTData[3] & 0x20) ? 0x8 : 0;
                    index[3]  = (pbTData[0] & 0x10) ? 0x1 : 0;
                    index[3] |= (pbTData[1] & 0x10) ? 0x2 : 0;
                    index[3] |= (pbTData[2] & 0x10) ? 0x4 : 0;
                    index[3] |= (pbTData[3] & 0x10) ? 0x8 : 0;
                    index[4]  = (pbTData[0] & 0x08) ? 0x1 : 0;
                    index[4] |= (pbTData[1] & 0x08) ? 0x2 : 0;
                    index[4] |= (pbTData[2] & 0x08) ? 0x4 : 0;
                    index[4] |= (pbTData[3] & 0x08) ? 0x8 : 0;
                    index[5]  = (pbTData[0] & 0x04) ? 0x1 : 0;
                    index[5] |= (pbTData[1] & 0x04) ? 0x2 : 0;
                    index[5] |= (pbTData[2] & 0x04) ? 0x4 : 0;
                    index[5] |= (pbTData[3] & 0x04) ? 0x8 : 0;
                    index[6]  = (pbTData[0] & 0x02) ? 0x1 : 0;
                    index[6] |= (pbTData[1] & 0x02) ? 0x2 : 0;
                    index[6] |= (pbTData[2] & 0x02) ? 0x4 : 0;
                    index[6] |= (pbTData[3] & 0x02) ? 0x8 : 0;
                    index[7]  = (pbTData[0] & 0x01) ? 0x1 : 0;
                    index[7] |= (pbTData[1] & 0x01) ? 0x2 : 0;
                    index[7] |= (pbTData[2] & 0x01) ? 0x4 : 0;
                    index[7] |= (pbTData[3] & 0x01) ? 0x8 : 0;
                }
                else                            // 4 Color
                {
                    index[0]  = (pbTData[0] & 0x80) ? 0x1 : 0;
                    index[0] |= (pbTData[1] & 0x80) ? 0x2 : 0;
                    index[1]  = (pbTData[0] & 0x40) ? 0x1 : 0;
                    index[1] |= (pbTData[1] & 0x40) ? 0x2 : 0;
                    index[2]  = (pbTData[0] & 0x20) ? 0x1 : 0;
                    index[2] |= (pbTData[1] & 0x20) ? 0x2 : 0;
                    index[3]  = (pbTData[0] & 0x10) ? 0x1 : 0;
                    index[3] |= (pbTData[1] & 0x10) ? 0x2 : 0;
                    index[4]  = (pbTData[0] & 0x08) ? 0x1 : 0;
                    index[4] |= (pbTData[1] & 0x08) ? 0x2 : 0;
                    index[5]  = (pbTData[0] & 0x04) ? 0x1 : 0;
                    index[5] |= (pbTData[1] & 0x04) ? 0x2 : 0;
                    index[6]  = (pbTData[0] & 0x02) ? 0x1 : 0;
                    index[6] |= (pbTData[1] & 0x02) ? 0x2 : 0;
                    index[7]  = (pbTData[0] & 0x01) ? 0x1 : 0;
                    index[7] |= (pbTData[1] & 0x01) ? 0x2 : 0;
                }
            }

            if(TMap & MAP_HREV)
            {
                j = index[0];
                index[0] = index[7];
                index[7] = j;
                j = index[1];
                index[1] = index[6];
                index[6] = j;
                j = index[2];
                index[2] = index[5];
                index[5] = j;
                j = index[3];
                index[3] = index[4];
                index[4] = j;
            }

            PalIndex = (TMap & MAP_PAL) >> 9;
            if((!index[0]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[0]];
            }
            if((!index[1]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[1]];
            }
            if((!index[2]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[2]];
            }
            if((!index[3]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[3]];
            }
            if((!index[4]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[4]];
            }
            if((!index[5]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[5]];
            }
            if((!index[6]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[6]];
            }
            if((!index[7]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[7]];
            }
        }
    }
/*********************************************************************/
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

            if(COLCTL & 0x40)
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

            if(COLCTL & 0x20)                       // Packed Mode
            {
                if(COLCTL & 0x40)                   // 16 Color
                {
                    index[0] = (pbTData[0] & 0xF0) >> 4;
                    index[1] = pbTData[0] & 0x0F;
                    index[2] = (pbTData[1] & 0xF0) >> 4;
                    index[3] = pbTData[1] & 0x0F;
                    index[4] = (pbTData[2] & 0xF0) >> 4;
                    index[5] = pbTData[2] & 0x0F;
                    index[6] = (pbTData[3] & 0xF0) >> 4;
                    index[7] = pbTData[3] & 0x0F;
                }
                else                            // 4 Color
                {
                    index[0] = (pbTData[0] & 0xC0) >> 6;
                    index[1] = (pbTData[0] & 0x30) >> 4;
                    index[2] = (pbTData[0] & 0x0C) >> 2;
                    index[3] = pbTData[0] & 0x03;
                    index[4] = (pbTData[1] & 0xC0) >> 6;
                    index[5] = (pbTData[1] & 0x30) >> 4;
                    index[6] = (pbTData[1] & 0x0C) >> 2;
                    index[7] = pbTData[1] & 0x03;
                }
            }
            else
            {
                if(COLCTL & 0x40)                   // 16 Color
                {
                    index[0]  = (pbTData[0] & 0x80) ? 0x1 : 0;
                    index[0] |= (pbTData[1] & 0x80) ? 0x2 : 0;
                    index[0] |= (pbTData[2] & 0x80) ? 0x4 : 0;
                    index[0] |= (pbTData[3] & 0x80) ? 0x8 : 0;
                    index[1]  = (pbTData[0] & 0x40) ? 0x1 : 0;
                    index[1] |= (pbTData[1] & 0x40) ? 0x2 : 0;
                    index[1] |= (pbTData[2] & 0x40) ? 0x4 : 0;
                    index[1] |= (pbTData[3] & 0x40) ? 0x8 : 0;
                    index[2]  = (pbTData[0] & 0x20) ? 0x1 : 0;
                    index[2] |= (pbTData[1] & 0x20) ? 0x2 : 0;
                    index[2] |= (pbTData[2] & 0x20) ? 0x4 : 0;
                    index[2] |= (pbTData[3] & 0x20) ? 0x8 : 0;
                    index[3]  = (pbTData[0] & 0x10) ? 0x1 : 0;
                    index[3] |= (pbTData[1] & 0x10) ? 0x2 : 0;
                    index[3] |= (pbTData[2] & 0x10) ? 0x4 : 0;
                    index[3] |= (pbTData[3] & 0x10) ? 0x8 : 0;
                    index[4]  = (pbTData[0] & 0x08) ? 0x1 : 0;
                    index[4] |= (pbTData[1] & 0x08) ? 0x2 : 0;
                    index[4] |= (pbTData[2] & 0x08) ? 0x4 : 0;
                    index[4] |= (pbTData[3] & 0x08) ? 0x8 : 0;
                    index[5]  = (pbTData[0] & 0x04) ? 0x1 : 0;
                    index[5] |= (pbTData[1] & 0x04) ? 0x2 : 0;
                    index[5] |= (pbTData[2] & 0x04) ? 0x4 : 0;
                    index[5] |= (pbTData[3] & 0x04) ? 0x8 : 0;
                    index[6]  = (pbTData[0] & 0x02) ? 0x1 : 0;
                    index[6] |= (pbTData[1] & 0x02) ? 0x2 : 0;
                    index[6] |= (pbTData[2] & 0x02) ? 0x4 : 0;
                    index[6] |= (pbTData[3] & 0x02) ? 0x8 : 0;
                    index[7]  = (pbTData[0] & 0x01) ? 0x1 : 0;
                    index[7] |= (pbTData[1] & 0x01) ? 0x2 : 0;
                    index[7] |= (pbTData[2] & 0x01) ? 0x4 : 0;
                    index[7] |= (pbTData[3] & 0x01) ? 0x8 : 0;
                }
                else                            // 4 Color
                {
                    index[0]  = (pbTData[0] & 0x80) ? 0x1 : 0;
                    index[0] |= (pbTData[1] & 0x80) ? 0x2 : 0;
                    index[1]  = (pbTData[0] & 0x40) ? 0x1 : 0;
                    index[1] |= (pbTData[1] & 0x40) ? 0x2 : 0;
                    index[2]  = (pbTData[0] & 0x20) ? 0x1 : 0;
                    index[2] |= (pbTData[1] & 0x20) ? 0x2 : 0;
                    index[3]  = (pbTData[0] & 0x10) ? 0x1 : 0;
                    index[3] |= (pbTData[1] & 0x10) ? 0x2 : 0;
                    index[4]  = (pbTData[0] & 0x08) ? 0x1 : 0;
                    index[4] |= (pbTData[1] & 0x08) ? 0x2 : 0;
                    index[5]  = (pbTData[0] & 0x04) ? 0x1 : 0;
                    index[5] |= (pbTData[1] & 0x04) ? 0x2 : 0;
                    index[6]  = (pbTData[0] & 0x02) ? 0x1 : 0;
                    index[6] |= (pbTData[1] & 0x02) ? 0x2 : 0;
                    index[7]  = (pbTData[0] & 0x01) ? 0x1 : 0;
                    index[7] |= (pbTData[1] & 0x01) ? 0x2 : 0;
                }
            }

            if(TMap & MAP_HREV)
            {
                j = index[0];
                index[0] = index[7];
                index[7] = j;
                j = index[1];
                index[1] = index[6];
                index[6] = j;
                j = index[2];
                index[2] = index[5];
                index[5] = j;
                j = index[3];
                index[3] = index[4];
                index[4] = j;
            }

            PalIndex = (TMap & MAP_PAL) >> 9;
            if(((!index[0]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[0]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[1]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[1]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[2]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[2]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[3]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[3]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[4]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[4]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[5]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[5]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[6]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[6]];
                *pZ = 1;
            }
            pW++;pZ++;
            if(((!index[7]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800))))) || (*pW)) pSWrBuf++;
            else
            {
                *pSWrBuf++ = Palette[PalIndex][index[7]];
                *pZ = 1;
            }
            pW++;pZ++;
        }
    }
/*********************************************************************/
    if((DSPCTL & 0x04) && Layer[2])          //sprite
    {
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

        for (pbTMap = SprETMap; pbTMap >= SprTTMap; pbTMap -= 4) // 
        {
            TMap = pbTMap[0];
            TMap |= pbTMap[1] << 8;

            if (pbTMap[2] > 0xF8)
            {
                j = pbTMap[2] - 0x100;
            }
            else
            {
                j = pbTMap[2];
            }
            if (pbTMap[3] > 0xF8)
            {
                k = pbTMap[3] - 0x100;
            }
            else
            {
                k = pbTMap[3];
            }

            if (Line < j)
                continue;
            if (Line >= j + 8)
                continue;
            if (LCD_MAIN_W <= k)
                continue;

            i = k;
            pSWrBuf = pSBuf + i;

            if (COLCTL & 0x40)
            {
                pbTData = IRAM + 0x4000;
                pbTData += (TMap & SPR_TILE) << 5;
                if (TMap & SPR_VREV)
                {
                    pbTData += (7 - Line + j) << 2;
                }
                else
                {
                    pbTData += (Line - j) << 2;
                }
            }
            else
            {
                pbTData = IRAM + 0x2000;
                pbTData += (TMap & SPR_TILE) << 4;
                if (TMap & SPR_VREV)
                {
                    pbTData += (7 - Line + j) << 1;
                }
                else
                {
                    pbTData += (Line - j) << 1;
                }
            }

            if(COLCTL & 0x20)                       // Packed Mode
            {
                if(COLCTL & 0x40)                   // 16 Color
                {
                    index[0] = (pbTData[0] & 0xF0) >> 4;
                    index[1] =  pbTData[0] & 0x0F;
                    index[2] = (pbTData[1] & 0xF0) >> 4;
                    index[3] =  pbTData[1] & 0x0F;
                    index[4] = (pbTData[2] & 0xF0) >> 4;
                    index[5] =  pbTData[2] & 0x0F;
                    index[6] = (pbTData[3] & 0xF0) >> 4;
                    index[7] =  pbTData[3] & 0x0F;
                }
                else                            // 4 Color
                {
                    index[0] = (pbTData[0] & 0xC0) >> 6;
                    index[1] = (pbTData[0] & 0x30) >> 4;
                    index[2] = (pbTData[0] & 0x0C) >> 2;
                    index[3] =  pbTData[0] & 0x03;
                    index[4] = (pbTData[1] & 0xC0) >> 6;
                    index[5] = (pbTData[1] & 0x30) >> 4;
                    index[6] = (pbTData[1] & 0x0C) >> 2;
                    index[7] =  pbTData[1] & 0x03;
                }
            }
            else
            {
                if(COLCTL & 0x40)                   // 16 Color
                {
                    index[0]  = (pbTData[0] & 0x80) ? 0x1 : 0;
                    index[0] |= (pbTData[1] & 0x80) ? 0x2 : 0;
                    index[0] |= (pbTData[2] & 0x80) ? 0x4 : 0;
                    index[0] |= (pbTData[3] & 0x80) ? 0x8 : 0;
                    index[1]  = (pbTData[0] & 0x40) ? 0x1 : 0;
                    index[1] |= (pbTData[1] & 0x40) ? 0x2 : 0;
                    index[1] |= (pbTData[2] & 0x40) ? 0x4 : 0;
                    index[1] |= (pbTData[3] & 0x40) ? 0x8 : 0;
                    index[2]  = (pbTData[0] & 0x20) ? 0x1 : 0;
                    index[2] |= (pbTData[1] & 0x20) ? 0x2 : 0;
                    index[2] |= (pbTData[2] & 0x20) ? 0x4 : 0;
                    index[2] |= (pbTData[3] & 0x20) ? 0x8 : 0;
                    index[3]  = (pbTData[0] & 0x10) ? 0x1 : 0;
                    index[3] |= (pbTData[1] & 0x10) ? 0x2 : 0;
                    index[3] |= (pbTData[2] & 0x10) ? 0x4 : 0;
                    index[3] |= (pbTData[3] & 0x10) ? 0x8 : 0;
                    index[4]  = (pbTData[0] & 0x08) ? 0x1 : 0;
                    index[4] |= (pbTData[1] & 0x08) ? 0x2 : 0;
                    index[4] |= (pbTData[2] & 0x08) ? 0x4 : 0;
                    index[4] |= (pbTData[3] & 0x08) ? 0x8 : 0;
                    index[5]  = (pbTData[0] & 0x04) ? 0x1 : 0;
                    index[5] |= (pbTData[1] & 0x04) ? 0x2 : 0;
                    index[5] |= (pbTData[2] & 0x04) ? 0x4 : 0;
                    index[5] |= (pbTData[3] & 0x04) ? 0x8 : 0;
                    index[6]  = (pbTData[0] & 0x02) ? 0x1 : 0;
                    index[6] |= (pbTData[1] & 0x02) ? 0x2 : 0;
                    index[6] |= (pbTData[2] & 0x02) ? 0x4 : 0;
                    index[6] |= (pbTData[3] & 0x02) ? 0x8 : 0;
                    index[7]  = (pbTData[0] & 0x01) ? 0x1 : 0;
                    index[7] |= (pbTData[1] & 0x01) ? 0x2 : 0;
                    index[7] |= (pbTData[2] & 0x01) ? 0x4 : 0;
                    index[7] |= (pbTData[3] & 0x01) ? 0x8 : 0;
                }
                else                            // 4 Color
                {
                    index[0]  = (pbTData[0] & 0x80) ? 0x1 : 0;
                    index[0] |= (pbTData[1] & 0x80) ? 0x2 : 0;
                    index[1]  = (pbTData[0] & 0x40) ? 0x1 : 0;
                    index[1] |= (pbTData[1] & 0x40) ? 0x2 : 0;
                    index[2]  = (pbTData[0] & 0x20) ? 0x1 : 0;
                    index[2] |= (pbTData[1] & 0x20) ? 0x2 : 0;
                    index[3]  = (pbTData[0] & 0x10) ? 0x1 : 0;
                    index[3] |= (pbTData[1] & 0x10) ? 0x2 : 0;
                    index[4]  = (pbTData[0] & 0x08) ? 0x1 : 0;
                    index[4] |= (pbTData[1] & 0x08) ? 0x2 : 0;
                    index[5]  = (pbTData[0] & 0x04) ? 0x1 : 0;
                    index[5] |= (pbTData[1] & 0x04) ? 0x2 : 0;
                    index[6]  = (pbTData[0] & 0x02) ? 0x1 : 0;
                    index[6] |= (pbTData[1] & 0x02) ? 0x2 : 0;
                    index[7]  = (pbTData[0] & 0x01) ? 0x1 : 0;
                    index[7] |= (pbTData[1] & 0x01) ? 0x2 : 0;
                }
            }

            if (TMap & SPR_HREV)
            {
                j = index[0];
                index[0] = index[7];
                index[7] = j;
                j = index[1];
                index[1] = index[6];
                index[6] = j;
                j = index[2];
                index[2] = index[5];
                index[5] = j;
                j = index[3];
                index[3] = index[4];
                index[4] = j;
            }

            pW = WBuf + 8 + k;
            pZ = ZBuf + k + 8;
            PalIndex = ((TMap & SPR_PAL) >> 9) + 8;
            for(i = 0; i < 8; i++, pZ++, pW++)
            {
                if(DSPCTL & 0x08)
                {
                    if(TMap & SPR_CLIP)
                    {
                        if(!*pW)
                        {
                            pSWrBuf++;
                            continue;
                        }
                    }
                    else
                    {
                        if(*pW)
                        {
                            pSWrBuf++;
                            continue;
                        }
                    }
                }
                if((!index[i]) && (!(!(COLCTL & 0x40) && (!(TMap & 0x0800)))))
                {
                    pSWrBuf++;
                    continue;
                }
                if((*pZ) && (!(TMap & SPR_LAYR)))
                {
                    pSWrBuf++;
                    continue;
                }
                *pSWrBuf++ = Palette[PalIndex][index[i]];
            }
        }
    }
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
