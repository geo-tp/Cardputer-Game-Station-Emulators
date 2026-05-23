/*---------------------------------------------------------------------------
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version. See also the license.txt file for
 *  additional informations.
 *---------------------------------------------------------------------------
 */

/* graphics.cpp: implementation of the graphics class. */

#include <string.h>
#include <stdint.h>

#include "types.h"
#include "graphics.h"
#include "race-memory.h"
#include "retro_compat.h"

#if defined(ABGR1555)
#define RMASK 0x001f
#define GMASK 0x03e0
#define BMASK 0x7c00
#else
#define RMASK 0xf800
#define GMASK 0x07e0
#define BMASK 0x001f
#endif

typedef struct {
    unsigned char flip;
    unsigned char x;
    unsigned char pal;
} MYSPRITE;

typedef struct {
    unsigned short tile;
    unsigned char  id;
} MYSPRITEREF;

typedef struct {
    unsigned char count;
    MYSPRITEREF   refs[64];
} MYSPRITELINE;

/* extern fournis ailleurs */
extern struct ngp_screen* screen;
extern int gfx_hacks;
extern int finscan;
extern volatile unsigned g_frame_ready;

/*
 * 16-bit graphics buffers
 */
#define TOTALPALETTE_SIZE  4096
uint16_t* totalpalette = NULL;
static unsigned short *myPalettes = NULL;
static unsigned dark_filter_level = 0;
extern unsigned short *drawBuffer;
static MYSPRITELINE *mySprPri40 = NULL;
static MYSPRITELINE *mySprPri80 = NULL;
static MYSPRITELINE *mySprPriC0 = NULL;
static MYSPRITELINE *mySprPriBlock = NULL;
static MYSPRITE *mySprites = NULL;

/* NGP specific: precalculated pattern structures (nibbles) */
static const unsigned char mypatterns[256*4] =
{
    0,0,0,0, 0,0,0,1, 0,0,0,2, 0,0,0,3, 0,0,1,0, 0,0,1,1, 0,0,1,2, 0,0,1,3,
    0,0,2,0, 0,0,2,1, 0,0,2,2, 0,0,2,3, 0,0,3,0, 0,0,3,1, 0,0,3,2, 0,0,3,3,
    0,1,0,0, 0,1,0,1, 0,1,0,2, 0,1,0,3, 0,1,1,0, 0,1,1,1, 0,1,1,2, 0,1,1,3,
    0,1,2,0, 0,1,2,1, 0,1,2,2, 0,1,2,3, 0,1,3,0, 0,1,3,1, 0,1,3,2, 0,1,3,3,
    0,2,0,0, 0,2,0,1, 0,2,0,2, 0,2,0,3, 0,2,1,0, 0,2,1,1, 0,2,1,2, 0,2,1,3,
    0,2,2,0, 0,2,2,1, 0,2,2,2, 0,2,2,3, 0,2,3,0, 0,2,3,1, 0,2,3,2, 0,2,3,3,
    0,3,0,0, 0,3,0,1, 0,3,0,2, 0,3,0,3, 0,3,1,0, 0,3,1,1, 0,3,1,2, 0,3,1,3,
    0,3,2,0, 0,3,2,1, 0,3,2,2, 0,3,2,3, 0,3,3,0, 0,3,3,1, 0,3,3,2, 0,3,3,3,
    1,0,0,0, 1,0,0,1, 1,0,0,2, 1,0,0,3, 1,0,1,0, 1,0,1,1, 1,0,1,2, 1,0,1,3,
    1,0,2,0, 1,0,2,1, 1,0,2,2, 1,0,2,3, 1,0,3,0, 1,0,3,1, 1,0,3,2, 1,0,3,3,
    1,1,0,0, 1,1,0,1, 1,1,0,2, 1,1,0,3, 1,1,1,0, 1,1,1,1, 1,1,1,2, 1,1,1,3,
    1,1,2,0, 1,1,2,1, 1,1,2,2, 1,1,2,3, 1,1,3,0, 1,1,3,1, 1,1,3,2, 1,1,3,3,
    1,2,0,0, 1,2,0,1, 1,2,0,2, 1,2,0,3, 1,2,1,0, 1,2,1,1, 1,2,1,2, 1,2,1,3,
    1,2,2,0, 1,2,2,1, 1,2,2,2, 1,2,2,3, 1,2,3,0, 1,2,3,1, 1,2,3,2, 1,2,3,3,
    1,3,0,0, 1,3,0,1, 1,3,0,2, 1,3,0,3, 1,3,1,0, 1,3,1,1, 1,3,1,2, 1,3,1,3,
    1,3,2,0, 1,3,2,1, 1,3,2,2, 1,3,2,3, 1,3,3,0, 1,3,3,1, 1,3,3,2, 1,3,3,3,
    2,0,0,0, 2,0,0,1, 2,0,0,2, 2,0,0,3, 2,0,1,0, 2,0,1,1, 2,0,1,2, 2,0,1,3,
    2,0,2,0, 2,0,2,1, 2,0,2,2, 2,0,2,3, 2,0,3,0, 2,0,3,1, 2,0,3,2, 2,0,3,3,
    2,1,0,0, 2,1,0,1, 2,1,0,2, 2,1,0,3, 2,1,1,0, 2,1,1,1, 2,1,1,2, 2,1,1,3,
    2,1,2,0, 2,1,2,1, 2,1,2,2, 2,1,2,3, 2,1,3,0, 2,1,3,1, 2,1,3,2, 2,1,3,3,
    2,2,0,0, 2,2,0,1, 2,2,0,2, 2,2,0,3, 2,2,1,0, 2,2,1,1, 2,2,1,2, 2,2,1,3,
    2,2,2,0, 2,2,2,1, 2,2,2,2, 2,2,2,3, 2,2,3,0, 2,2,3,1, 2,2,3,2, 2,2,3,3,
    2,3,0,0, 2,3,0,1, 2,3,0,2, 2,3,0,3, 2,3,1,0, 2,3,1,1, 2,3,1,2, 2,3,1,3,
    2,3,2,0, 2,3,2,1, 2,3,2,2, 2,3,2,3, 2,3,3,0, 2,3,3,1, 2,3,3,2, 2,3,3,3,
    3,0,0,0, 3,0,0,1, 3,0,0,2, 3,0,0,3, 3,0,1,0, 3,0,1,1, 3,0,1,2, 3,0,1,3,
    3,0,2,0, 3,0,2,1, 3,0,2,2, 3,0,2,3, 3,0,3,0, 3,0,3,1, 3,0,3,2, 3,0,3,3,
    3,1,0,0, 3,1,0,1, 3,1,0,2, 3,1,0,3, 3,1,1,0, 3,1,1,1, 3,1,1,2, 3,1,1,3,
    3,1,2,0, 3,1,2,1, 3,1,2,2, 3,1,2,3, 3,1,3,0, 3,1,3,1, 3,1,3,2, 3,1,3,3,
    3,2,0,0, 3,2,0,1, 3,2,0,2, 3,2,0,3, 3,2,1,0, 3,2,1,1, 3,2,1,2, 3,2,1,3,
    3,2,2,0, 3,2,2,1, 3,2,2,2, 3,2,2,3, 3,2,3,0, 3,2,3,1, 3,2,3,2, 3,2,3,3,
    3,3,0,0, 3,3,0,1, 3,3,0,2, 3,3,0,3, 3,3,1,0, 3,3,1,1, 3,3,1,2, 3,3,1,3,
    3,3,2,0, 3,3,2,1, 3,3,2,2, 3,3,2,3, 3,3,3,0, 3,3,3,1, 3,3,3,2, 3,3,3,3,
};

/* target window / offsets */
#define BLIT_X_OFFSET 8
#define BLIT_Y_OFFSET 8
#define BLIT_OFFSET   (BLIT_X_OFFSET + (BLIT_Y_OFFSET*SIZEX))
#define BLIT_WIDTH    (160)
#define BLIT_HEIGHT   (152)

#define PSP_FUDGE 0

/* Palette init selector */
void (*palette_init)(DWORD dwRBitMask, DWORD dwGBitMask, DWORD dwBBitMask);

/* ---------------------- Palette ---------------------- */

static void darken_rgb(float *r, float *g, float *b)
{
    static const float luma_r = 0.299f;
    static const float luma_g = 0.587f;
    static const float luma_b = 0.114f;
    float luma = luma_r * (*r) + luma_g * (*g) + luma_b * (*b);
    float dark_factor = 1.0f - ((float)dark_filter_level * 0.01f) * luma;
    if (dark_factor < 0.0f) dark_factor = 0.0f;
    *r *= dark_factor; *g *= dark_factor; *b *= dark_factor;
}

void palette_init16(DWORD dwRBitMask, DWORD dwGBitMask, DWORD dwBBitMask)
{
    char RShiftCount = 0, GShiftCount = 0, BShiftCount = 0;
    char RBitCount = 0, GBitCount = 0, BBitCount = 0;
    int  r,g,b;
    DWORD i;

    i = dwRBitMask; while (!(i&1)) { i >>= 1; RShiftCount++; } while (i&1) { i >>= 1; RBitCount++; }
    i = dwGBitMask; while (!(i&1)) { i >>= 1; GShiftCount++; } while (i&1) { i >>= 1; GBitCount++; }
    i = dwBBitMask; while (!(i&1)) { i >>= 1; BShiftCount++; } while (i&1) { i >>= 1; BBitCount++; }

    switch(m_emuInfo.machine)
    {
        case NGP:
        case NGPC:
            if (dark_filter_level > 0)
            {
                static const float rgb_max     = 15.0f;
                static const float rgb_max_inv = 1.0f / 15.0f;
                float r_float, g_float, b_float;
                int r_final, g_final, b_final;

                for (b=0; b<16; b++)
                for (g=0; g<16; g++)
                for (r=0; r<16; r++)
                {
                    r_float = r * rgb_max_inv;
                    g_float = g * rgb_max_inv;
                    b_float = b * rgb_max_inv;
                    darken_rgb(&r_float, &g_float, &b_float);
                    r_final = ((int)(r_float * rgb_max + 0.5f)) & 0xF;
                    g_final = ((int)(g_float * rgb_max + 0.5f)) & 0xF;
                    b_final = ((int)(b_float * rgb_max + 0.5f)) & 0xF;

                    totalpalette[b*256+g*16+r] =
                        (((b_final<<(BBitCount-4))+(b_final>>(4-(BBitCount-4))))<<BShiftCount) +
                        (((g_final<<(GBitCount-4))+(g_final>>(4-(GBitCount-4))))<<GShiftCount) +
                        (((r_final<<(RBitCount-4))+(r_final>>(4-(RBitCount-4))))<<RShiftCount);
                }
            }
            else
            {
                for (b=0; b<16; b++)
                for (g=0; g<16; g++)
                for (r=0; r<16; r++)
                {
                    totalpalette[b*256+g*16+r] =
                        (((b<<(BBitCount-4))+(b>>(4-(BBitCount-4))))<<BShiftCount) +
                        (((g<<(GBitCount-4))+(g>>(4-(GBitCount-4))))<<GShiftCount) +
                        (((r<<(RBitCount-4))+(r>>(4-(RBitCount-4))))<<RShiftCount);
                }
            }
            break;
    }
}

void graphicsSetDarkFilterLevel(unsigned filterLevel)
{
    unsigned prev = dark_filter_level;
    dark_filter_level = (filterLevel > 100) ? 100 : filterLevel;
    if (dark_filter_level != prev) palette_init16(RMASK, GMASK, BMASK);
}

void palette_init8(DWORD dwRBitMask, DWORD dwGBitMask, DWORD dwBBitMask) { (void)dwRBitMask; (void)dwGBitMask; (void)dwBBitMask; }
void pngpalette_init(void) {}

/* ---------------------- Rendu NGP/NGPC ---------------------- */

static const unsigned short bwTable[8] = {
    0x0FFF, 0x0DDD, 0x0BBB, 0x0999, 0x0777, 0x0555, 0x0333, 0x0000
};

/* Sprite compact */
typedef struct {
    unsigned short offset;     /* x offset */
    unsigned short pattern;    /* flags & code*/
    unsigned short tile_index;
    unsigned short pal_index;  /* offset dans palette */
    unsigned char  line;       /* ligne 0..7  */
} SPRITE;

/* Cache tuiles */
typedef struct {
    unsigned short *gbp;
    unsigned char oldScrollX;
    unsigned char *newScrollX;
    unsigned char oldScrollY;
    unsigned char *newScrollY;
    unsigned short *tileBase;
    short   tile[21];
    unsigned short *palettes[21];
    unsigned short *tilept[21];
    unsigned short *palette;
} TILECACHE;

uint16_t *palettes;
static TILECACHE  tCBack, tCFront;

static INLINE void lineClear(TILECACHE *tC, unsigned short col)
{
    for (int i = 0; i < 21 * 8; i++) tC->gbp[i] = col;
}

static INLINE void lineFront(TILECACHE *tC)
{
    unsigned short *gb = tC->gbp;
    for (int i=0;i<21;i++)
    {
        unsigned char a = *(((unsigned char *)tC->tilept[i])+1);
        unsigned char b = *((unsigned char *)tC->tilept[i]);
        const unsigned char *p2;

        if (tC->tile[i]&0x8000)
        {
            p2 = &mypatterns[b*4];
            if (p2[3]) gb[0] = tC->palettes[i][p2[3]];
            if (p2[2]) gb[1] = tC->palettes[i][p2[2]];
            if (p2[1]) gb[2] = tC->palettes[i][p2[1]];
            if (p2[0]) gb[3] = tC->palettes[i][p2[0]];
            p2 = &mypatterns[a*4];
            if (p2[3]) gb[4] = tC->palettes[i][p2[3]];
            if (p2[2]) gb[5] = tC->palettes[i][p2[2]];
            if (p2[1]) gb[6] = tC->palettes[i][p2[1]];
            if (p2[0]) gb[7] = tC->palettes[i][p2[0]];
        }
        else
        {
            p2 = &mypatterns[a*4];
            if (p2[0]) gb[0] = tC->palettes[i][p2[0]];
            if (p2[1]) gb[1] = tC->palettes[i][p2[1]];
            if (p2[2]) gb[2] = tC->palettes[i][p2[2]];
            if (p2[3]) gb[3] = tC->palettes[i][p2[3]];
            p2 = &mypatterns[b*4];
            if (p2[0]) gb[4] = tC->palettes[i][p2[0]];
            if (p2[1]) gb[5] = tC->palettes[i][p2[1]];
            if (p2[2]) gb[6] = tC->palettes[i][p2[2]];
            if (p2[3]) gb[7] = tC->palettes[i][p2[3]];
        }
        if (tC->tile[i]&0x4000) tC->tilept[i]-= 1;
        else                    tC->tilept[i]+= 1;
        gb+= 8;
    }
}

static INLINE void RenderTileCache(TILECACHE *tC, unsigned int bw)
{
    if ((tC->oldScrollX != *tC->newScrollX) ||
        (tC->oldScrollY != *tC->newScrollY) ||
        (((*tC->newScrollY + *scanlineY)&7) == 0))
    {
        tC->gbp = tC->gbp + (tC->oldScrollX&7) - (*tC->newScrollX&7);
        tC->oldScrollX = *tC->newScrollX;
        tC->oldScrollY = *tC->newScrollY;

        unsigned short *temp = tC->tileBase + (((tC->oldScrollY + *scanlineY)&0xf8)<<2);
        for (int i=0;i<21;i++)
        {
            tC->tile[i] = *(temp + (((tC->oldScrollX>>3) + i)&31));
            unsigned char line = (*tC->newScrollY + *scanlineY)&7;
            tC->palettes[i] = (bw)
                ? tC->palette + ((tC->tile[i] & 0x2000) ? 4 : 0)
                : tC->palette + ((tC->tile[i]>>7) & 0x3C);
            tC->tilept[i] = patterns
              + (((tC->tile[i] & 0x01ff) << 3)  // 8 words (16B)
              + (((tC->tile[i] & 0x4000) ? 7 - line : line)));
        }
    }
}

void sortSprites(unsigned int bw)
{
    const unsigned char lineY = (unsigned char)(*scanlineY);

    mySprPri40->count = 0;
    mySprPri80->count = 0;
    mySprPriC0->count = 0;

    unsigned char prevx = 0, prevy = 0;

    for (unsigned i = 0; i < 64; ++i)
    {
        unsigned short spriteCode = *((unsigned short *)(sprite_table + 4*i));
        if ((spriteCode <= 0x00FF) || ((spriteCode & 0x1800) == 0))
            continue;

        /* positions cumulatives  */
        prevx = (spriteCode & 0x0400 ? prevx : 0) + *(sprite_table + 4*i + 2);
        prevy = (spriteCode & 0x0200 ? prevy : 0) + *(sprite_table + 4*i + 3);

        unsigned char x = (unsigned char)(prevx + *scrollSpriteX);
        unsigned char y = (unsigned char)(prevy + *scrollSpriteY);

        /* hors screen */
        if ((x > 167 && x < 249) || (y > 151 && y < 249))
            continue;

        /* sprite touche ligne ? */
        int dy = (int)lineY - (int)y;
        if (dy < 0 || dy > 7)
            continue;

        /* meta pour drawSprites() */
        mySprites[i].x    = x;
        mySprites[i].pal  = bw ? (unsigned char)((spriteCode >> 11) & 0x04)
                               : (unsigned char)((sprite_palette_numbers[i] & 0x0F) << 2);
        mySprites[i].flip = (unsigned char)(spriteCode >> 8);

        /* tuile + ligne */
        const unsigned short baseTile = (unsigned short)((spriteCode & 0x01FF) << 3);
        const int flipV = (spriteCode & 0x4000) ? 1 : 0;
        const unsigned short tileLine = (unsigned short)(baseTile + (flipV ? (7 - dy) : dy));

        /* prio */
        MYSPRITELINE *dstList = NULL;
        switch (spriteCode & 0x1800) {
            case 0x1800: dstList = mySprPriC0; break;
            case 0x1000: dstList = mySprPri80; break;
            case 0x0800: dstList = mySprPri40; break;
            default: continue;
        }
        if (dstList->count >= 64) continue;

        MYSPRITEREF *dst = &dstList->refs[dstList->count++];
        dst->id   = (unsigned char)i;
        dst->tile = tileLine;
    }
}

void drawSprites(unsigned short* draw, MYSPRITEREF *sprites, int count, int x0, int x1)
{
    unsigned int pattern,pix,cnt;
    for (int i=count-1;i>=0;--i)
    {
        pattern = patterns[sprites[i].tile];
        if (pattern==0) continue;

        MYSPRITE *spr = &mySprites[sprites[i].id];
        int cx = (spr->x>248) ? (spr->x-256) : spr->x;
        if (cx+8<=x0 || cx>=x1) continue;

        unsigned short *pal = &myPalettes[spr->pal];

        if (cx<x0)
        {
            cnt = 8-(x0-cx);
            if (spr->flip&0x80) {
                pattern>>=((8-cnt)<<1);
                for (cx=x0; pattern; ++cx) {
                    pix = pattern & 0x3;
                    if (pix) draw[cx] = pal[pix];
                    pattern>>=2;
                }
            } else {
                pattern &= (0xffff>>((8-cnt)<<1));
                for (cx = x0+cnt-1; pattern; --cx) {
                    pix = pattern & 0x3;
                    if (pix) draw[cx] = pal[pix];
                    pattern>>=2;
                }
            }
        }
        else if (cx+7<x1)
        {
            if (spr->flip&0x80) {
                for (; pattern; ++cx) {
                    pix = pattern & 0x3;
                    if (pix) draw[cx] = pal[pix];
                    pattern>>=2;
                }
            } else {
                for (cx+=7; pattern; --cx) {
                    pix = pattern & 0x3;
                    if (pix) draw[cx] = pal[pix];
                    pattern>>=2;
                }
            }
        }
        else
        {
            cnt = x1-cx;
            if (spr->flip&0x80) {
                pattern &= (0xffff>>((8-cnt)<<1));
                for (; pattern; ++cx) {
                    pix = pattern & 0x3;
                    if (pix) draw[cx] = pal[pix];
                    pattern>>=2;
                }
            } else {
                pattern>>=((8-cnt)<<1);
                for (cx+=cnt-1; pattern; --cx) {
                    pix = pattern & 0x3;
                    if (pix) draw[cx] = pal[pix];
                    pattern>>=2;
                }
            }
        }
    }
}

void drawScrollPlane(unsigned short* draw,
                     unsigned short* tile_table,int scrpal,
                     unsigned char dx,unsigned char dy,
                     int x0,int x1,unsigned int bw)
{
    unsigned short*tiles;
    unsigned short*pal;
    unsigned int pattern;
    unsigned int j,count,pix,idy,tile;
    int i,x2;

    dx+=x0;
    tiles = tile_table+((dy>>3)<<5)+(dx>>3);

    count = 8-(dx&0x7);
    dx &= 0xf8;
    dy &= 0x7;
    idy = 7-dy;

    i = x0;

    if (count<8)
    {
        tile = *(tiles++);
        pattern = patterns[(((tile&0x1ff))<<3) + (tile&0x4000 ? idy:dy)];
        if (pattern)
        {
            pal = &myPalettes[scrpal + (bw ? (tile&0x2000 ? 4 : 0) : ((tile>>7)&0x3c))];
            if (tile&0x8000)
            {
                pattern>>=((8-count)<<1);
                for (j=i;pattern;++j) { pix = pattern & 0x3; if (pix) draw[j] = pal[pix]; pattern>>=2; }
            }
            else
            {
                pattern &= (0xffff>>((8-count)<<1));
                for (j=i+count-1;pattern;--j) { pix = pattern & 0x3; if (pix) draw[j] = pal[pix]; pattern>>=2; }
            }
        }
        i+=count;
        dx+=8;
        if (dx==0) tiles-=32;
    }

    x2 = i+((x1-i)&0xf8);

    for (;i<x2;i+=8)
    {
        tile = *(tiles++);
        pattern = patterns[(((tile&0x1ff))<<3) + (tile&0x4000 ? idy:dy)];
        if (pattern)
        {
            pal = &myPalettes[scrpal + (bw ? (tile&0x2000 ? 4 : 0) : ((tile>>7)&0x3c))];
            if (tile&0x8000)
            {
                for (j=i;pattern;++j) { pix = pattern & 0x3; if (pix) draw[j] = pal[pix]; pattern>>=2; }
            }
            else
            {
                for (j=i+7;pattern;--j) { pix = pattern & 0x3; if (pix) draw[j] = pal[pix]; pattern>>=2; }
            }
        }
        dx+=8;
        if (dx==0) tiles-=32;
    }

    if (x2!=x1)
    {
        count = x1-x2;
        tile = *(tiles++);
        pattern = patterns[(((tile&0x1ff))<<3) + (tile&0x4000 ? idy:dy)];
        if (pattern)
        {
            pal = &myPalettes[scrpal + (bw ? (tile&0x2000 ? 4 : 0) : ((tile>>7)&0x3c))];
            if (tile&0x8000)
            {
                pattern &= (0xffff>>((8-count)<<1));
                for (j=i;pattern;++j) { pix = pattern & 0x3; if (pix) draw[j] = pal[pix]; pattern>>=2; }
            }
            else
            {
                pattern>>=((8-count)<<1);
                for (j=i+count-1;pattern;--j) { pix = pattern & 0x3; if (pix) draw[j] = pal[pix]; pattern>>=2; }
            }
        }
    }
}

/* ---------------------- Blit / Frame ---------------------- */

static void graphicsBlitInit(void)
{
    /* back */
    tCBack.gbp   = &drawBuffer[8*SIZEX + (8-((*scrollBackX)&7))];
    tCBack.newScrollX = scrollBackX;
    tCBack.newScrollY = scrollBackY;
    tCBack.tileBase   = tile_table_back;
    tCBack.palette    = &palettes[16*4+16*4];

    /* front */
    tCFront.gbp   = &drawBuffer[8*SIZEX + (8-((*scrollFrontX)&7))];
    tCFront.newScrollX = scrollFrontX;
    tCFront.newScrollY = scrollFrontY;
    tCFront.tileBase   = tile_table_front;
    tCFront.palette    = &palettes[16*4];

    /* force recalculations for first line */
    tCBack.oldScrollX  = *tCBack.newScrollX;
    tCBack.oldScrollY  = *tCBack.newScrollY+1;
    tCFront.oldScrollX = *tCFront.newScrollX;
    tCFront.oldScrollY = *tCFront.newScrollY+1;
}

static inline uint16_t bg_color()
{
  if (m_emuInfo.machine == NGP) return 0xFFFF;       // blanc
  if (palette_table) return palette_table[0];        // NGPC couleur 0
  return 0x0000;                                     // fallback noir
}

static inline uint8_t ngpc_ifr(void) {
    return tlcsMemReadB(0x00008010);
}

static inline void fill16_fast(uint16_t* __restrict dst, uint16_t v, int count)
{
    if (count <= 0) return;
    // pack 2x16 en 32
    uint32_t vv = ((uint32_t)v << 16) | v;
    uint32_t* __restrict d32 = (uint32_t*)dst;
    while (count >= 2) { *d32++ = vv; count -= 2; }
    if (count) { *((uint16_t*)d32) = v; }
}


void myGraphicsBlitLine(unsigned char render)
{
    if (!scanlineY) {
        static unsigned char dummy = 0;
        scanlineY = &dummy;
    }

    const uint8_t y = *scanlineY;

    // zone visible
    if (y < 152) {
        if (render) {
            // base pointeur
            uint16_t* __restrict draw = &drawBuffer[(size_t)y * 160];

            // cache registres
            const int is_bw   = (m_emuInfo.machine == NGP);

            const int wnd_tlx = *wndTopLeftX;
            const int wnd_w   = *wndSizeX;
            const int wnd_tly = *wndTopLeftY;
            const int wnd_h   = *wndSizeY;

            // clip horizontal
            int x0 = wnd_tlx;
            int x1 = wnd_tlx + wnd_w;
            if (x0 < 0)    x0 = 0;
            if (x1 > 160)  x1 = 160;

            // window vide ?
            const int win_empty =
                (wnd_w == 0) | (wnd_h == 0) |
                (y < (uint8_t)wnd_tly) |
                (y > (uint8_t)(wnd_tly + wnd_h));

            // couleurs OOW + fond
            const uint8_t  oow_idx = (uint8_t)(*oowSelect & 0x07);
            const uint16_t OOWCol  = oowTable ? NGPC_TO_SDL16(oowTable[oow_idx]) : 0;
            const uint8_t frame1 = *frame1Pri;
            const bool plane2_front = is_bw ? ((frame1 & 0x01) != 0) : ((frame1 & 0x80) != 0);

            uint16_t bgcol;
            const uint8_t bgsel = *bgSelect;
            if (is_bw) {
                bgcol = OOWCol;
            } else if (bgsel & 0x80) {
                bgcol = NGPC_TO_SDL16(bgTable[bgsel & 0x07]);
            } else {
                bgcol = palette_table ? NGPC_TO_SDL16(palette_table[0]) : 0;
            }

            if (win_empty) {
                // tout en OOW
                fill16_fast(draw, OOWCol, 160);
            } else {
                // gauche OOW
                if (x0 > 0) fill16_fast(draw, OOWCol, x0);
                // milieu BG
                const int mid = x1 - x0;
                if (mid > 0) fill16_fast(draw + x0, bgcol, mid);
                // droite OOW
                const int right = 160 - x1;
                if (right > 0) fill16_fast(draw + x1, OOWCol, right);
            }

            // Palettes
            if (!win_empty && ((y & 7u) == 0u)) {
                if (is_bw) {
                    for (int i = 0; i < 4; ++i) {
                        myPalettes[i]       = NGPC_TO_SDL16(bwTable[bw_palette_table[i]      & 0x07]);
                        myPalettes[4 + i]   = NGPC_TO_SDL16(bwTable[bw_palette_table[4  + i] & 0x07]);
                        myPalettes[64 + i]  = NGPC_TO_SDL16(bwTable[bw_palette_table[8  + i] & 0x07]);
                        myPalettes[68 + i]  = NGPC_TO_SDL16(bwTable[bw_palette_table[12 + i] & 0x07]);
                        myPalettes[128 + i] = NGPC_TO_SDL16(bwTable[bw_palette_table[16 + i] & 0x07]);
                        myPalettes[132 + i] = NGPC_TO_SDL16(bwTable[bw_palette_table[20 + i] & 0x07]);
                    }
                } else if (palette_table) {
                    // 192 entries NGPC
                    for (int i = 0; i < 192; ++i)
                        myPalettes[i] = NGPC_TO_SDL16(palette_table[i]);
                }
            }

            if (!win_empty) {
                sortSprites(is_bw);

                // Ordre empilement
                drawSprites(draw, mySprPri40->refs, mySprPri40->count, x0, x1);

                // plans + 0x80 entre les deux
                const uint8_t sfX = *scrollFrontX, sfY = (uint8_t)(*scrollFrontY + y);
                const uint8_t sbX = *scrollBackX,  sbY = (uint8_t)(*scrollBackY  + y);

                if (plane2_front) {
                    // FRONT, SPRITES 0x80, BACK
                    drawScrollPlane(draw, tile_table_front,  64,  sfX, sfY, x0, x1, is_bw);
                    drawSprites(draw, mySprPri80->refs, mySprPri80->count, x0, x1);
                    drawScrollPlane(draw, tile_table_back,   128, sbX, sbY, x0, x1, is_bw);
                } else {
                    // BACK, SPRITES 0x80, FRONT
                    drawScrollPlane(draw, tile_table_back,   128, sbX, sbY, x0, x1, is_bw);
                    drawSprites(draw, mySprPri80->refs, mySprPri80->count, x0, x1);
                    drawScrollPlane(draw, tile_table_front,  64,  sfX, sfY, x0, x1, is_bw);
                }

                // sprites prio 0xC0
                drawSprites(draw, mySprPriC0->refs, mySprPriC0->count, x0, x1);
            }
        }

        // fin zone visible
        if (y == 151u) {
            const uint8_t ifr = tlcsMemReadB(0x00008010);
            tlcsMemWriteB(0x00008010, (uint8_t)(ifr | 0x40));
            // graphics_paint(render);
            g_frame_ready = 1;
        }

        *scanlineY = (uint8_t)(y + 1);
        return;
    }

    if (y >= (uint8_t)finscan) {
        const uint8_t ifr = tlcsMemReadB(0x00008010);
        tlcsMemWriteB(0x00008010, (uint8_t)(ifr & (uint8_t)~0x40));
        *scanlineY = 0;
    } else {
        *scanlineY = (uint8_t)(y + 1);
    }
}

BOOL graphics_init(void)
{
    if (!mySprPriBlock) {
        mySprPriBlock = calloc(3, sizeof(MYSPRITELINE));
        if (!mySprPriBlock) return FALSE;

        mySprPri40 = &mySprPriBlock[0];
        mySprPri80 = &mySprPriBlock[1];
        mySprPriC0 = &mySprPriBlock[2];
    }

    if (!totalpalette) {
        totalpalette = calloc(TOTALPALETTE_SIZE, sizeof(uint16_t));
    }

    if (!myPalettes) {
        myPalettes = calloc(192, sizeof(uint16_t));
    }

    if (palettes == NULL) {
        palettes = calloc(192, sizeof(uint16_t));
    }

    if (!mySprites) {
        mySprites = calloc(64, sizeof(MYSPRITE));
    }

#ifdef __LIBRETRO__
    palette_init = palette_init16;
    palette_init(RMASK, GMASK, BMASK);
    drawBuffer = (unsigned short*)screen->pixels;
#else
    dbg_print("in graphics_init\n");

    palette_init = palette_init16;
    palette_init(0xF800, 0x07E0, 0x001F);
#endif
    if (!scanlineY) {
        scanlineY = (unsigned char*) get_address(0x00008009);
        if (!scanlineY) {
            static unsigned char dummy = 0;
            scanlineY = &dummy;
            printf("[SAFE INIT] scanlineY not mapped yet -> using dummy\n");
        }
    }


    switch(m_emuInfo.machine)
    {
        case NGP:
            bgTable  = (unsigned short *)bwTable;
            oowTable = (unsigned short *)bwTable;
            graphicsBlitInit();
            *scanlineY = 0;
            break;
        case NGPC:
            bgTable  = (unsigned short *)get_address(0x000083E0);
            oowTable = (unsigned short *)get_address(0x000083F0);
            graphicsBlitInit();
            *scanlineY = 0;
            break;
    }

    return TRUE;
}

