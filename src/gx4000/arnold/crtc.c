/*
 *  Arnold emulator (c) Copyright, Kevin Thacker 1995-2001
 *
 *  This file is part of the Arnold emulator source code distribution.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
/*
	CRTC 6845 emulation
	(c) Kevin Thacker

	Emulates:
	Type 0 (HD6845S, UM6845), Type 1 (UM6845R), Type 2 (MC6845)
	Type 3 (CRTC in CPC+ ASIC).
  */
/* THIS CODE IS QUITE SLOW AND COMPLEX, IT WOULD BE GOOD TO SPEED IT UP A LOT AND FIX MORE BUGS IN IT */
void	CRTC_MonitorReset(void);

#pragma GCC optimize ("Ofast")

#define SIMPLE_MONITOR_EMULATION
#define HD6845S
/*#define WIP */

#include "crtc.h"
#include "cpcglob.h"
#include "render.h"
#include "asic.h"
#include "z80/z80.h"
#include "cpc.h"

#include "garray.h"

#include "headers.h"


#ifndef LESS_MULTS
#define LESS_MULTS
#endif


/*******************************************************/

/*******************************************************/

void	CRTC_LightPen_Update(unsigned long NopsPassed);

static int CPCHardware = CPC_HW_CPC;

void    CRTC_SetCPCMode(int NewCPCHardware)
{
        CPCHardware = NewCPCHardware;
}


static void     CRTC_GetGraphicsDataCPC_TrueColour(void);
static void     CRTC_GetGraphicsDataPLUS_TrueColour(void);
static void     CRTC_RenderBorder_TrueColour(void);
static void		CRTC_RenderSync_TrueColour(void);

static void     CRTC_GetGraphicsDataCPC_Paletted(void);
static void     CRTC_GetGraphicsDataPLUS_Paletted(void);
static void     CRTC_RenderBorder_Paletted(void);
static void		CRTC_RenderSync_Paletted(void);

static void     CRTC_SetRenderingFunction(void);
static void     CRTC_InitVsync(void);
static void CRTC_DoLineChecks(void);
static void CRTC_DoLineChecksCRTC2(void);
static void     CRTC_DoReg8(void);
static void ASICCRTC_VTot(void);
static void     CRTC_DoReg3(void);
static void ASICCRTC_GenerateLineNumber(void);

static char UM6845R_StatusRegister=1;
static unsigned long CRTC_LightPenMA;

static unsigned long ASIC_RasterMA;

#define CRTC_ClearFlag(x) \
	CRTC_InternalState.CRTC_Flags &=~x

#define CRTC_SetFlag(x)	\
	CRTC_InternalState.CRTC_Flags |=x


CRTC_INTERNAL_STATE *pCRTC_InternalState = NULL;

CRTC_INTERNAL_STATE *CRTC_GetInternalState(void)
{
        return pCRTC_InternalState;
}


extern unsigned char *Z80MemoryBase;

extern GATE_ARRAY_STATE GateArray_State;

/* CRTC internal register file */
/* Registers 0-18 are used, 19-31 are not */
static unsigned char            CRTCRegisters[32];
static unsigned char            CRTCRegistersBackup[32];

static void    CRTC0_DoLine(void);
static void    CRTC1_DoLine(void);
static void    CRTC2_DoLine(void);
static void    ASICCRTC_DoLine(void);

static void    CRTC0_UpdateState(int);
static void    CRTC1_UpdateState(int);
static void    CRTC2_UpdateState(int);
static void    ASICCRTC_UpdateState(int);


static void	CRTC_NextRenderStage(unsigned long Flags);
static void CRTC_FirstRenderStage(void);

/* The following table represents the conversion between the CRTC HSync Width,
and the HSync Width passed to the monitor from the Gate Array. Any value >=6
results in a fixed HSync width of 4, any value >5 has a shorter width. */
const unsigned char   GA_HSyncWidth[16]=
{
        0,                      /* 0 */
        0,                      /* 1 */
        0,                      /* 2 */
        1+2,                      /* 3 */
        2+2,                      /* 4 */
        3+2,                      /* 5 */
        4+2,                      /* 6 */
        4+2,                      /* 7 */
        4+2,                      /* 8 */
        4+2,                      /* 9 */
        4+2,                      /* 10 */
        4+2,                      /* 11 */
        4+2,                      /* 12 */
        4+2,                      /* 13 */
        4+2,                      /* 14 */
        4+2,                      /* 15 */
};

/*--------------------------------------------------------------------------*/
/* values OR'd onto values when reading */
/*static unsigned char *pCRTC_ReadMaskTable;*/
/* values to and off values written to registers */
/*static unsigned char    *pCRTC_WriteMaskTable;*/


/* value to OR to status register before returning value */
/*static unsigned long  CRTC_StatusRegReadOR;*/
/* value to AND with status register before returning value */
/*static unsigned long  CRTC_StatusRegReadAND;*/

/*--------------------------------------------------------------------------*/

/*
REG 0: Actual - 1
REG 1: Actual
REG 2: Actual
REG 3: Actual
REG 4: Actual - 1
REG 5: Actual
REG 6: Actual
REG 7: Actual - 1
REG 8:
REG 9: Actual - 1
*/

/*
Type 2A, IIRC, behaves like a type 2 but has the input port of a type 0.
(I had one on my 464, and remember alluding to it in the Equinoxe part
of Inspiration all those years ago.)
*/

/*------------------------------------------------------------------------*/
/* PRE-ASIC CRTC */
/* V-Sync does come when (R2+R3)>R0 */
/* Programmable VSYNC */
/* &BE can be read, &BF can be read . &BExx and &BFxx report register contents */
/* Has Skew? */
const unsigned char PreAsicCRTC_ReadMaskTable[32]=
{
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x000,
        0x000,
        0x000,
        0x000,

        0x000,  /* Light Pen (H) */
        0x000,  /* Light Pen (L) */
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
};

const unsigned char PreAsicCRTC_WriteMaskTable[32] =
{
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x07f,
        0x01f,
        0x07f,
        0x07f,
        0x0f3,
        0x01f,
        0x07f,
        0x01f,
        0x03f,
        0x0ff,
        0x03f,
        0x0ff,

        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,

};


/*-----------------------------------------------------------------------*/
/* ASIC CRTC */
/* Programmable VSYNC. No CRTC 2 type problems. */
/* Port &bexx, &bfxx return contents of readable registers */
/* Writing to registers 18-31 has no effect */
/* Has Skew */
const unsigned char AsicCRTC_ReadMaskTable[32]=
{
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x000,
        0x000,
        0x000,
        0x000,

        0x000,  /* Light Pen (H) */
        0x000,  /* Light Pen (L) */
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
};

const unsigned char AsicCRTC_WriteMaskTable[32] =
{
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x07f,
        0x01f,
        0x07f,
        0x07f,
        0x0f3,
        0x01f,
        0x07f,
        0x01f,
        0x03f,
        0x0ff,
        0x03f,
        0x0ff,

        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,

};

/*-----------------------------------------------------------------------*/
/* HD6845S */

/* these are or'ed on before returning result */
const unsigned char HD6845S_ReadMaskTable[32]=
{
        0x0ff,  /* Horizontal Total */
        0x0ff,  /* Horizontal Displayed */
        0x0ff,  /* Horizontal Sync Position */
        0x0ff,  /* Sync Widths */
        0x0ff,  /* Vertical Total */
        0x0ff,  /* Vertical Adjust */
        0x0ff,  /* Vertical Displayed */
        0x0ff,  /* Vertical Sync Position */
        0x0ff,  /* Interlace and Skew */
        0x0ff,  /* Maximum Raster Address */
        0x0ff,  /* Cursor Start */
        0x0ff,  /* Cursor End */
        0x000,  /* Screen Addr (H) */
        0x000,  /* Screen Addr (L) */
        0x000,  /* Cursor (H) */
        0x000,  /* Cursor (L) */

        0x000,  /* Light Pen (H) */
        0x000,  /* Light Pen (L) */
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,

};

/* these are anded before data is written */
const unsigned char HD6845S_WriteMaskTable[32] =
{
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x07f,
        0x01f,
        0x07f,
        0x07f,
        0x0f3,
        0x01f,
        0x07f,
        0x01f,
        0x03f,
        0x0ff,
        0x03f,
        0x0ff,

        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
};

/*---------------------------------------------------------------------------*/

/* HD6845R */

/* these are or'ed on before returning result */
static unsigned char HD6845R_ReadMaskTable[32]=
{
        0x0ff, /* Horizontal Total */
        0x0ff, /* Horizontal Displayed */
        0x0ff, /* Horizontal Sync Position */
        0x0ff, /* Sync Widths */
        0x0ff, /* Vertical Total */
        0x0ff, /* Vertical Total Adjust */
        0x0ff, /* Vertical Displayed */
        0x0ff, /* Vertical Sync Position */
        0x0ff, /* Interlace and Skew */
        0x0ff, /* Maximum Raster Address */
        0x0ff, /* Cursor Start */
        0x0ff, /* Cursor End */
        0x0ff, /* Screen Addr (H) */
        0x0ff, /* Screen Addr (L) */
        0x000, /* Cursor (H) */
        0x000, /* Cursor (L) */

        0x000, /* Light Pen (H) */
        0x000, /* Light Pen (L) */
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,

};

/* these are anded before data is written */
static unsigned char HD6845R_WriteMaskTable[32] =
{
        0x0ff,
        0x0ff,
        0x0ff,
        0x00f,
        0x07f,
        0x01f,
        0x07f,
        0x07f,
        0x003,
        0x01f,
        0x07f,
        0x01f,
        0x03f,
        0x0ff,
        0x03f,
        0x0ff,

        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
};


/*---------------------------------------------------------------------------*/

/* UM6845R */

const unsigned char UM6845R_ReadMaskTable[32]=
{
        0x0ff,  /* Horizontal Total */
        0x0ff,  /* Horizontal Displayed */
        0x0ff,  /* Horizontal Sync Position */
        0x0ff,  /* Sync Width */
        0x0ff,  /* Vertical Total */
        0x0ff,  /* Vertical Total Adjust */
        0x0ff,  /* Vertical Displayed */
        0x0ff,  /* V. Sync Position */
        0x0ff,  /* Interlace Mode and Skew */
        0x0ff,  /* Max Scan Line Address */
        0x0ff,  /* Cursor Start */
        0x0ff,  /* Cursor End */
        0x0ff,  /* Start Address (H) */
        0x0ff,  /* Start Address (L) */
        0x000,  /* Cursor (H) */
        0x000,  /* Cursor (L) */

        0x000,  /* Light Pen (H) */
        0x000,  /* Light Pen (L) */
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x000,
};

const unsigned char UM6845R_WriteMaskTable[32]=
{
        0x0ff,
        0x0ff,
        0x0ff,
        0x00f,
        0x07f,
        0x01f,
        0x07f,
        0x07f,
        0x003,
        0x01f,
        0x07f,
        0x01f,
        0x03f,
        0x0ff,
        0x03f,
        0x0ff,

        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,

};

/*-------------------------------------------------------------------*/
/* these are or'ed on before returning result */

/* UM6845 is really a type 0 */
static unsigned char UM6845_ReadMaskTable[32]=
{
        0x0ff,  /* Horizontal Total */
        0x0ff,  /* Horizontal Displayed */
        0x0ff,  /* Horizontal Sync Position */
        0x0ff,  /* Sync Widths */
        0x0ff,  /* Vertical Total */
        0x0ff,  /* Vertical Adjust */
        0x0ff,  /* Vertical Displayed */
        0x0ff,  /* Vertical Sync Position */
        0x0ff,  /* Interlace and Skew */
        0x0ff,  /* Maximum Raster Address */
        0x0ff,  /* Cursor Start */
        0x0ff,  /* Cursor End */
        0x000,  /* Screen Addr (H) */
        0x000,  /* Screen Addr (L) */
        0x000,  /* Cursor (H) */
        0x000,  /* Cursor (L) */

        0x000,  /* Light Pen (H) */
        0x000,  /* Light Pen (L) */
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,

};

/*------------------------------------------------------------------*/
/* these are anded before data is written */

static unsigned char UM6845_WriteMaskTable[32] =
{
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x07f,
        0x01f,
        0x07f,
        0x07f,
        0x0f3,
        0x01f,
        0x07f,
        0x01f,
        0x03f,
        0x0ff,
        0x03f,
        0x0ff,

        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
};


/*---------------------------------------------------------------------------*/

/* MC6845 (type 2) */

const unsigned char MC6845_ReadMaskTable[32]=
{
        0x0ff,  /* Horizontal Total */
        0x0ff,  /* Horizontal Displayed */
        0x0ff,  /* Horizontal Sync Position */
        0x0ff,  /* Sync Width */
        0x0ff,  /* Vertical Total */
        0x0ff,  /* Vertical Total Adjust */
        0x0ff,  /* Vertical Displayed */
        0x0ff,  /* V. Sync Position */
        0x0ff,  /* Interlace Mode and Skew */
        0x0ff,  /* Max Scan Line Address */
        0x0ff,  /* Cursor Start */
        0x0ff,  /* Cursor End */
        0x0ff,  /* Start Address (H) */
        0x0ff,  /* Start Address (L) */
        0x000,  /* Cursor (H) */
        0x000,  /* Cursor (L) */

        0x000,  /* Light Pen (H) */
        0x000,  /* Light Pen (L) */
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,

};

const unsigned char MC6845_WriteMaskTable[32]=
{
        0x0ff,
        0x0ff,
        0x0ff,
        0x00f,
        0x07f,
        0x01f,
        0x07f,
        0x07f,
        0x003,
        0x01f,
        0x07f,
        0x01f,
        0x03f,
        0x0ff,
        0x03f,
        0x0ff,

        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,
        0x0ff,

};

void    CRTC_Initialise(void)
{
                if (pCRTC_InternalState==NULL)
                {
                                pCRTC_InternalState = (CRTC_INTERNAL_STATE *)calloc(1, sizeof(*pCRTC_InternalState));
                                if (pCRTC_InternalState==NULL)
                                {
                                                return;
                                }
                }

/*		CRTC_FirstRenderStage(); */

        CRTC_SetType(0);

		CRTC_Reset();
}
#if 0
int		GA_CalculateCyclesToNextInterrupt(void)
{
	int CyclesToInterrupt;
	int NumberOfLinesToInterrupt;
	CyclesToInterrupt = 0;

	/* calc number of lines to interrupt */
	NumberOfLinesToInterrupt = 52 - CRTC_InternalState.GA_State.InterruptLineCount;

	/* we have not passed interrupt point yet */
	NumberOfLinesToInterrupt--;

	/* interrupt triggers on this line? */
	if (NumberOfLinesToInterrupt==0)
	{
		/* yes */
		return (CRTCRegisters[2] - CRTC_InternalState.HCount);
	}

	/* require cycles to end of line */
	CyclesToInterrupt+=CRTCRegisters[0] - CRTC_InternalState.HCount;

	/* number of complete lines to interrupt */
	if (NumberOfLinesToInterrupt!=0)
	{
		CyclesToInterrupt = (NumberOfLinesToInterrupt-1) * CRTCRegisters[0];
	}

	/* number of cycles to hsync on this line */
	CyclesToInterrupt += CRTCRegisters[2];
}
#endif
/* event triggered or not:


Events are:

HStart, HEnd, HTot/2, HTot, HSyncStart, HSyncEnd

  0		40		32		64	46
 */

/*int CyclesToNextCRTCEvent( */


/*---------------------------------------------------------------------------*/
/* holds ASIC Soft Scroll register value - for CRTC use */
static unsigned char ASICCRTC_SoftScroll = 0;
/* holds ASIC vertical soft scroll */
static unsigned char ASICCRTC_VerticalSoftScroll = 0;
/* holds current ASIC CRTC line used for displaying sprites */
static unsigned long ASICCRTC_Line = 0;
/* ASIC RA count */
static unsigned char ASICCRTC_RasterLine;
/* ASIC MA Line */
static int ASICCRTC_MALine;
/* Raster line match flag */
static BOOL     ASIC_RasterLineMatch = FALSE;

/* if TRUE, draw black on the display. This is caused if reg8 delay is set to  ,and the "extend border over garbage" in soft scroll is set */
static BOOL ASICCRTC_BlackDisplay = FALSE;

/* set HStart and HEnd */
static void CRTC_DoReg1(void)
{
        /* if Reg 8 is used, start and end positions are delayed by amount
        programmed. HStart can also be additionally delayed by ASIC. */

        /* set start and end positions of lines */
        CRTC_InternalState.HEnd = (unsigned char)(CRTCRegisters[1] + CRTC_InternalState.HDelayReg8);
        CRTC_InternalState.HStart = CRTC_InternalState.HDelayReg8;

        /* update hstart depending on ASIC Soft scroll */
        if ((ASICCRTC_SoftScroll & 0x080)!=0)
        {
                /* max delay and extend border? */
                if (CRTC_InternalState.HDelayReg8 == 3)
                {
                        /* Reg8 delay is set to max, and extend border is also set */
                        /* on CPC+ this generates a black display */
                        ASICCRTC_BlackDisplay = TRUE;
                }
                else
                {

                        /* do not black the display */
                        ASICCRTC_BlackDisplay=FALSE;

                        /* update H start depending on if ASIC Soft Scroll has been enabled */
                        CRTC_InternalState.HStart++;
                }
        }

        /* set HStart and HEnd to same, because Reg1 is set to 0 */
        if (CRTCRegisters[1]==0)
        {
                CRTC_InternalState.HStart = CRTC_InternalState.HEnd = 0;
        }

        /* update rendering function */
        CRTC_SetRenderingFunction();

}

static void     CRTC_DoReg8(void)
{
        int Delay;

        /* number of characters delay */
        Delay = (CRTCRegisters[8]>>4) & 0x03;
        CRTC_ClearFlag(CRTC_R8DT_FLAG);

        if (Delay==3)
        {
                /* Disable display of graphics */
                CRTC_SetFlag(CRTC_R8DT_FLAG);
				Delay = 0;
        }

        CRTC_InternalState.HDelayReg8 = (unsigned char)Delay;

        CRTC_DoReg1();

        /* interlace mode and video mode*/
        CRTC_InternalState.InterlaceAndVideoMode = (unsigned char)(CRTCRegisters[8] & 0x003);

        CRTC_SetRenderingFunction();

        if (CRTC_InternalState.InterlaceAndVideoMode ==3)
        {
                /* make sure raster counter is even, otherwise the
                interlace and video mode will not always start correctly! */
                CRTC_InternalState.RasterCounter &=~0x01;
        }
}

static void CRTC_DoReg3(void)
{
	int HorizontalSyncWidth = CRTCRegisters[3] & 0x0f;

	/* HORIZONTAL SYNC */

	/* get horizontal sync - CRTC */
    CRTC_InternalState.HorizontalSyncWidth =
		CRTC_InternalState.CRTC_HSyncTable[HorizontalSyncWidth];

	/* get horizontal sync - GA */
	CRTC_InternalState.GA_State.GA_HSyncWidth =
	CRTC_InternalState.GA_HsyncTable[HorizontalSyncWidth];

	/* horizontal sync counter active? */
	if (CRTC_InternalState.CRTC_Flags & CRTC_HS_FLAG)
	{



	}


	/* VERTICAL SYNC */

    /* CRTC has programmable VSYNC width */
    CRTC_InternalState.VerticalSyncWidth = (unsigned char)((CRTCRegisters[3] & 0x0f0)>>4) & 0x0f;

    /* VSYNC Width = 0 */
    if (CRTC_InternalState.VerticalSyncWidth == 0)
         CRTC_InternalState.VerticalSyncWidth = 16;
}



/*
  HD6845S (Hitachi)     - Type 0

  UM6845R (UMC) - Type 1

  MC6845 (Motorola) - Type 2

  CPC+ ASIC CRTC - Type 3 (according to S&KOH) (actually 2 of them)

  Pre-ASIC - type 4

  and type 2b which is a type 2 with a status register at Be00


UM6845 Type 0???--(but some can act like Type 0 - )
HD6845R ???
*/


static void	CRTC_InitialiseHsyncTable(void)
{
}

void    CRTC_SetType(unsigned int Type)
{
        int i;

        CRTC_InternalState.CRTC_Type = (unsigned char)Type;

        /* initialise crtc emulation */

		/* init hsync table */
		for (i=0; i<16; i++)
		{
			CRTC_InternalState.CRTC_HSyncTable[i] = i;
		}

		/* init GA Hsync Table */
		memcpy(CRTC_InternalState.GA_HsyncTable,
				GA_HSyncWidth, 16);


        switch (Type)
        {
                case 0:
                {
                    /* NWC's testing code thinks it's a type 0 */
                    /* The demo thinks it's a type 0 */
                    /* S&KOH thinks it's a type 0 */

					memcpy(CRTC_InternalState.CRTC_ReadMaskTable,
						HD6845S_ReadMaskTable, 16);

					memcpy(CRTC_InternalState.CRTC_WriteMaskTable,
						HD6845S_WriteMaskTable, 16);

					/* set light pen registers - this is what my CPC
					type 0 reports! */
					CRTC_LightPenMA = 0x0147c;
 				}
				break;

                case 1:
                {
                    /* NWC's test code thinks it is a type 1 */
                    /* The demo thinks it's a type 1 */
                    /* S&KOH thinks it's a type 1 */

					memcpy(CRTC_InternalState.CRTC_ReadMaskTable,
						UM6845R_ReadMaskTable, 16);

					memcpy(CRTC_InternalState.CRTC_WriteMaskTable,
						UM6845R_WriteMaskTable, 16);

					/* set light pen registers - this is what my type
					1 reports */
					CRTC_LightPenMA = 0;
                }
                break;

                case 2:
                {
                    /* S&KOH currently thinks it's a type 1 */
                    memcpy(CRTC_InternalState.CRTC_ReadMaskTable,
						MC6845_ReadMaskTable, 16);

					memcpy(CRTC_InternalState.CRTC_WriteMaskTable,
						MC6845_WriteMaskTable, 16);

					/* on type 2, if hsync width programmed as 0,
					then ints are generated. Hsync width is 16! */
					CRTC_InternalState.CRTC_HSyncTable[0] = 16;
					CRTC_InternalState.GA_HsyncTable[0] = 6;
				}
                break;

                case 3:
                {
                        /* The Demo says it's a ASIC. NWC's CRTC test code says ASIC */
                        /* S&KOH says type 3 (meaning ASIC) */
					memcpy(CRTC_InternalState.CRTC_ReadMaskTable,
						AsicCRTC_ReadMaskTable, 16);

					memcpy(CRTC_InternalState.CRTC_WriteMaskTable,
						AsicCRTC_WriteMaskTable, 16);

					/* on type 3, if hsync width programmed as 0,
					then ints are generated. Hsync width is 16! */
					CRTC_InternalState.CRTC_HSyncTable[0] = 16;
					CRTC_InternalState.GA_HsyncTable[0] = 6;
                    CRTC_LightPenMA = 0;
                }
                break;

                case 4:
                {
                        /* The Demo says it's a type 4. NWC's CRTC test code says PreASIC */
                        /* S&KOH says type 4 */
					memcpy(CRTC_InternalState.CRTC_ReadMaskTable,
						PreAsicCRTC_ReadMaskTable, 16);

					memcpy(CRTC_InternalState.CRTC_WriteMaskTable,
						PreAsicCRTC_WriteMaskTable, 16);

					/* on type 3, if hsync width programmed as 0,
					then ints are generated. Hsync width is 16! */
/*					CRTC_InternalState.CRTC_HsyncWidth[0] = 16;
					CRTC_InternalState.GA_HsyncTable[0] = 6;
                  CRTC_LightPenMA = 0;
*/
                }
                break;
        }

        /* take backup registers, and re-mask them according to the selected
        CRTC */
        for (i=0; i<32; i++)
        {
                CRTCRegisters[i] = (unsigned char)(CRTCRegistersBackup[i] & CRTC_InternalState.CRTC_WriteMaskTable[i]);
        }

        /* do these registers now, if these have been setup, but not changed,
        we need to re-do them depending on the CRTC chosen, otherwise we will
        not get the correct display !!! */
        CRTC_DoReg8();
        CRTC_DoReg3();

        /* clear display so that un-rendered bits are black - necessary for Nega Part of "The
        Demo" currently. Some programs do not like it when the CRTC is changed, change CRTC
        at Vsync or VTot?*/
        Render_ClearDisplay();

}

void    CRTC_RegisterSelect(unsigned int RegisterIndex)
{
        CRTC_InternalState.CRTC_Reg = (unsigned char)RegisterIndex;

        /* if CPC mode is CPC Plus */
        if (CPCHardware==CPC_HW_CPCPLUS)
        {
                /* data is passed to ASIC for enable/disable detection */
                ASIC_EnableDisable(RegisterIndex);
        }
}

int             CRTC_GetSelectedRegister(void)
{
        return CRTC_InternalState.CRTC_Reg;
}

unsigned char   CRTC_GetRegisterData(int RegisterIndex)
{
        return (unsigned char)CRTCRegisters[RegisterIndex & 0x01f];
}


#define CRTC_STATUS_IN_VSYNC                    (1<<5)
#define CRTC_STATUS_LPEN_REGISTER_FULL  (1<<6)

unsigned int            CRTC_ReadStatusRegister(void)
{
        switch (CRTC_InternalState.CRTC_Type)
        {
                /* Type 0 and type 2 have no "status register" and
                do not mirror register contents in status register area */
                case 0:
                case 2:
                {
                        return 0x0ff;
                }

                /* Type 1 has a status register */
                case 1:
                {
                        int CRTC_StatusRegister=0;

                        CRTC_StatusRegister&=~CRTC_STATUS_IN_VSYNC;
                        if (CRTC_InternalState.CRTC_Flags & CRTC_VS_FLAG)
                                CRTC_StatusRegister|=CRTC_STATUS_IN_VSYNC;

                        CRTC_StatusRegister|=UM6845R_StatusRegister;

                        return CRTC_StatusRegister &=(0x0ff^CRTC_STATUS_LPEN_REGISTER_FULL);
                }
                break;

                /* Type 3 and 4 mirror their register data in status register area */
                case 3:
                case 4:
                {
                        return CRTC_ReadData();
                }
                break;

                default:
                {
                        return 0x0ff;
                }
        }
}

unsigned char CRTC_ReadData(void)
{
       int CRTC_RegIndex;

        if (CRTC_InternalState.CRTC_Type==3)
        {
                switch (CRTC_InternalState.CRTC_Reg & 0x07)
                {

                        /* light pen high */
                        case 0:
                                return CRTCRegisters[16];
                        /* light pen low */
                        case 1:
                                return CRTCRegisters[17];
                        /* status 1 */
                        case 2:
                        {
				                unsigned char Status;
								unsigned char *pCRTCRegisters = CRTCRegisters;

                                Status = 0x0ff;

                                Status &= ~(0x020 | 0x010 | 0x008 | 0x004 | 0x002 |0x001 );

                                /* bit 5 = 1 if not on last line of vertical sync, 0 if on last line of vsync */
                                if (CRTC_InternalState.VerticalSyncCount!=CRTC_InternalState.VerticalSyncWidth)
                                {
                                        Status |= 0x020;
                                }

								/* bit 0 = 1 if HC == HTOT, 0 otherwise */
								if (CRTC_InternalState.HCount == pCRTCRegisters[0])
								{
										Status |= 0x001;
								}

								/* bit 1 = 1 if HC != HTOT/2, 0 otherwise */
								if (CRTC_InternalState.HCount != (pCRTCRegisters[0]>>1))
								{
										Status |= 0x002;
								}

								/* bit 2 = 1 if HC != HDISP, 0 otherwise. ** confirmed ** */
								if (CRTC_InternalState.HCount != pCRTCRegisters[1])
								{
										Status |= 0x004;
								}

								/* bit 3 = 1 if HC != HSYNC POS, 0 otherwise */
								if (CRTC_InternalState.HCount != pCRTCRegisters[2])
								{
										Status |= 0x008;
								}

								/* never sees end of horizontal sync!?? */
								/* bit 4 = 1 if HC != HSYNC END, 0 otherwise */
								if (CRTC_InternalState.HorizontalSyncCount!=CRTC_InternalState.HorizontalSyncWidth)
								{
										Status |= 0x010;
								}

		                        return Status;

						}

                        /* status 2 */
                        case 3:
		                {
							unsigned char Status;

								Status = 0x0ff;

                               /* clear bits */
                               Status &= ~(0x080 | 0x040 | 0x020);

                               /* bit 7: 1 if RC==0, 0 otherwise */
                               if (CRTC_InternalState.RasterCounter==0)
                               {
                                   Status|=0x080;
                               }

                               /* bit 5: 1 if RC!=MR, 0 otherwise */
                               if (!(CRTC_InternalState.RasterCounter>=CRTCRegisters[9]))
                               {
                                   Status|=0x020;
                               }

							   /* bit 3 -? begining of new line but not every frame??,
							   bit 2 - hend? */

	                           return Status;

						}

                        /* screen addr high */
                        case 4:
                                return CRTCRegisters[12];
                        /* screen addr low */
                        case 5:
                                return CRTCRegisters[13];
                        default:
                                break;

                }

                return 0;
                /* ASIC CRTC - returns same 8 registers repeated; reg 8-15 */
/*              CRTC_RegIndex = (CRTC_InternalState.CRTC_Reg & 0x07)+8; */
        }
        else
        {
                /* not ASIC CRTC - return data from registers - if readable!! */
                CRTC_RegIndex = (CRTC_InternalState.CRTC_Reg & 0x01f);
        }

       if (CRTC_InternalState.CRTC_Type==1)
        {
                if ((CRTC_RegIndex == 16) || (CRTC_RegIndex == 17))
                {
                        /* reading LPEN registers on type 1, clears bit in status register */
                        UM6845R_StatusRegister &=~0x01;
                }
        }


        /* un-readable registers return 0 on CRTC type 0 and type 1 */
        return (CRTCRegisters[CRTC_RegIndex] & (~CRTC_InternalState.CRTC_ReadMaskTable[CRTC_RegIndex]));
}

static void		CRTC_DoLineChecks(void)
{
    /* check Vertical Displayed */
    if (CRTC_InternalState.LineCounter == CRTCRegisters[6])
    {
		if (CRTC_InternalState.CRTC_Type==3)
		{
			if (CRTC_InternalState.RasterCounter == 0)
			{
				CRTC_ClearFlag(CRTC_VDISP_FLAG);

				CRTC_SetRenderingFunction();
			}
		}
		else
		{
				CRTC_ClearFlag(CRTC_VDISP_FLAG);

				CRTC_SetRenderingFunction();
		}
    }

    /* check Vertical sync position */
    if (CRTC_InternalState.LineCounter==CRTCRegisters[7])
    {
		if (CRTC_InternalState.CRTC_Type==3)
		{
			/* on CRTC type 3, Vsync will only be triggered on line 0. */
			if (CRTC_InternalState.RasterCounter==0)
			{
				CRTC_InitVsync();
			}
		}
		else
		{
			/* on CRTC type 0 and 1, Vsync can be triggered on any line of the char. */
			CRTC_InitVsync();
		}
    }
}

/* do line checks for CRTC type 2 */
static void             CRTC_DoLineChecksCRTC2(void)
{
		if (CRTC_InternalState.InterlaceAndVideoMode==3)
		{
		        /* check Vertical Displayed */
        		if ((CRTC_InternalState.LineCounter>>1) == CRTCRegisters[6])
        		{
                		CRTC_ClearFlag(CRTC_VDISP_FLAG);

                		CRTC_SetRenderingFunction();
        		}

        		/* check Vertical sync position */
        		if ((CRTC_InternalState.LineCounter>>1)==CRTCRegisters[7])
        		{
                		CRTC_InitVsync();
       			}
		}
		else
		{
			CRTC_DoLineChecks();
		}
}


void    CRTC_WriteData(unsigned int Data)
{
	     int CRTC_RegIndex = CRTC_InternalState.CRTC_Reg & 0x1f;


        /* store in backup set, used to change CRTC type */
        CRTCRegistersBackup[CRTC_RegIndex] = (unsigned char)Data;

        /* store registers using current CRTC information - masking out appropiate bits etc for this CRTC*/
        CRTCRegisters[CRTC_RegIndex] = (unsigned char)(Data & CRTC_InternalState.CRTC_WriteMaskTable[CRTC_RegIndex]);

	/* write CRTC specific information back into read-only registers */
	switch (CRTC_InternalState.CRTC_Type)
	{
		case 1:
		{
			/* crtc type 1 has 0x0ff for reg 31 */
			CRTCRegisters[31] = 0x0ff;
		}
		break;

		default:
			break;
	}



		/* Update CRTC state */
		switch (CRTC_InternalState.CRTC_Type)
		{
			case 0:
			{
				CRTC0_UpdateState(CRTC_RegIndex);
			}
			break;

			case 1:
			{
				CRTC1_UpdateState(CRTC_RegIndex);
			}
			break;

			case 2:
			{
				CRTC2_UpdateState(CRTC_RegIndex);
			}
			break;

			case 3:
			{
				ASICCRTC_UpdateState(CRTC_RegIndex);
			}
			break;

			case 4:
			{
				CRTC1_UpdateState(CRTC_RegIndex);
			}
			break;
		}
}




/*

  Bit 15: MA13
  Bit 14: MA12
  Bit 13: RA3
  Bit 12: RA2
  Bit 11: RA0
  Bit 10: MA9
  Bit 9: MA8
  Bit 8: MA7
  Bit 7: MA6
  Bit 6: MA5
  Bit 5: MA4
  Bit 4: MA3
  Bit 3: MA2
  Bit 2: MA1
  Bit 1: MA0
  Bit 0: CCLK
*/


static int CRTC_RenderMode;

void    CRTC_SetRenderFunction(int RenderMode)
{
	CRTC_RenderMode = RenderMode;

    switch (RenderMode)
    {
        case RENDER_MODE_STANDARD:
        {
            CRTC_InternalState.pCRTC_RenderGraphicsFunction = CRTC_InternalState.pCRTC_GetGraphicsDataCPC;
        }
        break;
        case RENDER_MODE_ASIC_FEATURES:
        {
            CRTC_InternalState.pCRTC_RenderGraphicsFunction = CRTC_InternalState.pCRTC_GetGraphicsDataPLUS;
        }
        break;
    }
}

void	CRTC_SetTrueColourRender(BOOL State)
{
	if (State==TRUE)
	{
		CRTC_InternalState.pCRTC_GetGraphicsDataCPC = CRTC_GetGraphicsDataCPC_TrueColour;
		CRTC_InternalState.pCRTC_GetGraphicsDataPLUS = CRTC_GetGraphicsDataPLUS_TrueColour;
		CRTC_InternalState.pCRTC_RenderBorder = CRTC_RenderBorder_TrueColour;
		CRTC_InternalState.pCRTC_RenderSync = CRTC_RenderSync_TrueColour;
	}
	else
	{
		CRTC_InternalState.pCRTC_GetGraphicsDataCPC = CRTC_GetGraphicsDataCPC_Paletted;
		CRTC_InternalState.pCRTC_GetGraphicsDataPLUS = CRTC_GetGraphicsDataPLUS_Paletted;
		CRTC_InternalState.pCRTC_RenderBorder = CRTC_RenderBorder_Paletted;
		CRTC_InternalState.pCRTC_RenderSync = CRTC_RenderSync_Paletted;
	}

	CRTC_SetRenderFunction(CRTC_RenderMode);
}


void    CRTC_RenderBorder_Paletted(void)
{
        Render_Paletted_PutBorder(CRTC_InternalState.Monitor_State.MonitorHorizontalCount, CRTC_InternalState.Monitor_State.MonitorScanLineCount);
}


void    CRTC_RenderBorder_TrueColour(void)
{
        Render_TrueColourRGB_PutBorder(CRTC_InternalState.Monitor_State.MonitorHorizontalCount, CRTC_InternalState.Monitor_State.MonitorScanLineCount);
}

void    CRTC_GetGraphicsDataCPC_TrueColour(void)
{
        unsigned long Addr;
        unsigned long LocalMA;
        unsigned long GraphicsWord;

        /* CPC version */
        LocalMA = (unsigned long)((CRTC_InternalState.MALine + CRTC_InternalState.HCount)<<1);

        /* get screen scrolling */
        Addr = (unsigned long)(((LocalMA & (0x03000<<1))<<1) | (LocalMA & (0x03ff<<1)));

        /* take address, and put in vertical line count in place of these 3 bits. */
        Addr |= CRTC_InternalState.RasterMA;

        GraphicsWord = ((Z80MemoryBase[(unsigned long)(Addr)])<<8)
                                        | (Z80MemoryBase[(unsigned long)(Addr+1)]);

        Render_TrueColourRGB_PutDataWord(CRTC_InternalState.Monitor_State.MonitorHorizontalCount, GraphicsWord, CRTC_InternalState.Monitor_State.MonitorScanLineCount);
}

void    CRTC_GetGraphicsDataCPC_Paletted(void)
{
        unsigned short Addr;
        unsigned short LocalMA;
        unsigned long GraphicsWord;

        /* CPC version */

        LocalMA = (unsigned short)(CRTC_InternalState.MALine + CRTC_InternalState.HCount);

        /* get screen scrolling */
        Addr = (unsigned short)(((LocalMA & 0x03000)<<2) | ((LocalMA & 0x03ff)<<1));

        /* take address, and put in vertical line count in place of these 3 bits. */
        Addr |= CRTC_InternalState.RasterMA;


        GraphicsWord = ((Z80MemoryBase[(unsigned long)(Addr)])<<8)
                                        | (Z80MemoryBase[(unsigned long)(Addr+1)]);

        Render_Paletted_PutDataWord(CRTC_InternalState.Monitor_State.MonitorHorizontalCount, GraphicsWord, CRTC_InternalState.Monitor_State.MonitorScanLineCount);
}



/* only does vertical soft scroll at this time */
void    CRTC_GetGraphicsDataPLUS_Paletted(void)
{
        unsigned short Addr;
        unsigned short LocalMA;
        unsigned long Mask;

        LocalMA = (unsigned short)(ASICCRTC_MALine + CRTC_InternalState.HCount);

        /* get screen scrolling */
        Addr = (unsigned short)(((LocalMA & 0x03000)<<2) | ((LocalMA & 0x03ff)<<1));

        /* take address, and put in vertical line count in place of these 3 bits. */
        Addr |= ASIC_RasterMA;

        /* shift up previous 16 bits of data */
        CRTC_InternalState.GraphicsLong = CRTC_InternalState.GraphicsLong<<16;

        /* add in new 16 bits of data */
        CRTC_InternalState.GraphicsLong = CRTC_InternalState.GraphicsLong |
                                        ((Z80MemoryBase[(unsigned long)(Addr)])<<8)
                                        | (Z80MemoryBase[(unsigned long)(Addr+1)]);

        /* Line, Column, ActualX, ActualY */
        Mask = ASIC_BuildDisplayReturnMaskWithPixels(ASICCRTC_Line /*VisibleRasterCount*/,CRTC_InternalState.HCount, /*MonitorHorizontalCount, MonitorScanLineCount,*/CRTC_InternalState.Pixels);

        Render_Paletted_PutDataWordPLUS(CRTC_InternalState.Monitor_State.MonitorHorizontalCount, CRTC_InternalState.GraphicsLong, CRTC_InternalState.Monitor_State.MonitorScanLineCount, Mask,CRTC_InternalState.Pixels);
}

void    CRTC_GetGraphicsDataPLUS_TrueColour(void)
{
        unsigned short Addr;
        unsigned short LocalMA;
        unsigned long Mask;

        LocalMA = (unsigned short)(ASICCRTC_MALine + CRTC_InternalState.HCount);

        /* get screen scrolling */
        Addr = (unsigned short)(((LocalMA & 0x03000)<<2) | ((LocalMA & 0x03ff)<<1));

        /* take address, and put in vertical line count in place of these 3 bits. */
        Addr |= ASIC_RasterMA;

        /* shift up previous 16 bits of data */
        CRTC_InternalState.GraphicsLong = CRTC_InternalState.GraphicsLong<<16;

        /* add in new 16 bits of data */
        CRTC_InternalState.GraphicsLong = CRTC_InternalState.GraphicsLong |
                                        ((Z80MemoryBase[(unsigned long)(Addr)])<<8)
                                        | (Z80MemoryBase[(unsigned long)(Addr+1)]);

	   /* Line, Column, ActualX, ActualY */
		Mask = ASIC_BuildDisplayReturnMaskWithPixels(ASICCRTC_Line /*VisibleRasterCount*/,CRTC_InternalState.HCount, /*MonitorHorizontalCount, MonitorScanLineCount,*/CRTC_InternalState.Pixels);

/*		if (Mask==0x0ffffffff)
		{
			Render_TrueColourRGB_PutDataWord(CRTC_InternalState.Monitor_State.MonitorHorizontalCount, CRTC_InternalState.GraphicsLong, CRTC_InternalState.Monitor_State.MonitorScanLineCount);
		}
		else
		{
*/
		Render_TrueColourRGB_PutDataWordPLUS(CRTC_InternalState.Monitor_State.MonitorHorizontalCount, CRTC_InternalState.GraphicsLong, CRTC_InternalState.Monitor_State.MonitorScanLineCount, Mask,CRTC_InternalState.Pixels);
/*		} */
}





void    CRTC_RenderSync_Paletted(void)
{
        Render_Paletted_PutSync(CRTC_InternalState.Monitor_State.MonitorHorizontalCount, CRTC_InternalState.Monitor_State.MonitorScanLineCount);
}

void    CRTC_RenderSync_TrueColour(void)
{
        Render_TrueColourRGB_PutSync(CRTC_InternalState.Monitor_State.MonitorHorizontalCount, CRTC_InternalState.Monitor_State.MonitorScanLineCount);
}

/* vertical count:
 vertical total - vertical sync position
 horizontal count:
 horizontal total - horizontal sync position

 Vertical Line Count, VerticalCharCount, MonitorHorizontalCount
 select graphics to pull out
*/
/*unsigned long NewMA; */
/*
void    CRTC_SetMA(int NewMA)
{
        MA = NewMA;
        MALine = MA;

        ASICCRTC_RasterLine = CRTC_InternalState.RasterCounter;
        ASICCRTC_MALine = MALine;

}
*/
/*void  CRTC_SetMAReload(int NewMAToSet)
{
      NewMA = NewMAToSet;
}
*/

/*---------------------------------------------------------------------------*/

#define GET_MA  \
        (CRTCRegisters[12]<<8) | (CRTCRegisters[13])

/*static int Mode = -1; */


#define MONITOR_VSYNC_COUNT	(24+1)

/* executed when Hsync is triggered by CRTC */
void CRTC_DoHsync(void)
{
	/* has vsync state changed? */
	if (((CRTC_InternalState.CRTC_Flags^CRTC_InternalState.CRTC_FlagsAtLastHsync) & CRTC_VS_FLAG)!=0)
	{
		if ((CRTC_InternalState.CRTC_Flags & CRTC_VS_FLAG)!=0)
		{

			/* vsync state has changed since last HSYNC */
			CRTC_InternalState.Monitor_State.MonitorFlags |= MONITOR_IN_VSYNC;
			CRTC_InternalState.Monitor_State.MonitorVsyncCount = MONITOR_VSYNC_COUNT;
			CRTC_SetRenderingFunction();
#ifdef SIMPLE_MONITOR_EMULATION
           /* monitor vsync */
            CRTC_InternalState.Monitor_State.MonitorFlags |= MONITOR_VSYNC_SYNC;
#endif

			/* enable interrupt synchronisation */
/*			CRTC_InternalState.GA_State.GA_Flags |= GA_INT_SYNC_FLAG;
			CRTC_InternalState.GA_State.InterruptSyncCount = 0;
*/
			GateArray_TriggerVsyncSynchronisation();
		}
	}

	/* store new state */
	CRTC_InternalState.CRTC_FlagsAtLastHsync = CRTC_InternalState.CRTC_Flags;


        /* The "InMonitorVsync" is used to render black where
        the Monitor Vsync is active. In some demos such as the
        TwinBlast demo this is very obvious. By reproducing this
        effect we can see this demo screen reproduced exactly as seen */
        if (CRTC_InternalState.Monitor_State.MonitorFlags & MONITOR_IN_VSYNC)
        {
                CRTC_InternalState.Monitor_State.MonitorVsyncCount--;

                if (CRTC_InternalState.Monitor_State.MonitorVsyncCount==0)
                {
                        CRTC_InternalState.Monitor_State.MonitorFlags &= ~MONITOR_IN_VSYNC;

                        CRTC_SetRenderingFunction();
                }
        }


        /* The GA takes the CRTC VSYNC and CRTC HSYNC and generates
         the GA VSYNC which is sent to the monitor.
        */

        /* Maybe we should be using this to synchronise the screen with.
         The effect will be to only shift the screen up or down a bit.
		 */
/*      if (GA_VSyncActive)
        {
                GA_VSyncCounter++;

                if (GA_VSyncCounter == GA_VSyncWidth)
                {
                        GA_VSyncActive = FALSE;

                }
        }
*/


	/* are we operating in CPC Plus mode ? */
	if (CPCHardware==CPC_HW_CPCPLUS)
	{
			/* yes. See if ASIC interrupts are required */
			ASIC_HSync(CRTC_InternalState.LineCounter, CRTC_InternalState.RasterCounter);
	}

	GateArray_Update();

        #ifndef GX4000_TRIM_NONPLUS
        if (CPCHardware==CPC_HW_KCCOMPACT)
        {
                KCC_Update();
        }
        #endif
}

/*------------------------------------------------------------------------------*/

static void     CRTC_InitVsync(void)
{
	/* CRTC type 2? */
	if (CRTC_InternalState.CRTC_Type==2)
	{
		/* if hsync is active, don't let vsync start */
		if (CRTC_InternalState.CRTC_Flags & CRTC_HS_FLAG)
		{
			return;
		}
	}

	if (!(CRTC_InternalState.CRTC_Flags & CRTC_VSCNT_FLAG))
	{
		CRTC_InternalState.VerticalSyncCount = 0;
		CRTC_SetFlag(CRTC_VSCNT_FLAG);
	}

#if 0
	/* it has not been triggered.. */
	if (!(CRTC_InternalState.CRTC_Flags & CRTC_VSYNC_TRIGGERED_FLAG))
	{
		/* re-set counter only if it is active */
		if (!(CRTC_InternalState.CRTC_Flags & CRTC_VSCNT_FLAG))
		{
			/* counter is not active! */

			CRTC_SetFlag(CRTC_VSYNC_TRIGGERED_FLAG);

			/* reset vertical sync count */
			CRTC_InternalState.VerticalSyncCount=0;

			/* enable counter */
			CRTC_SetFlag(CRTC_VSCNT_FLAG);


			CRTC_InterlaceControl_VsyncStart();


		}
	}
#endif
}

/*static void CRTC_RenderCycles(); */

/* depending on HDisp, VDisp, Reg8, and Syncs, set the rendering function */
static void     CRTC_SetRenderingFunction(void)
{
/*	int PreviousRenderType = CRTC_InternalState.CRTC_RenderType; */

#if 0

    /* set graphics rendering function - either graphics or border */
    if (
		(CRTC_InternalState.CRTC_Flags & CRTC_HDISP_FLAG) &&
		(CRTC_InternalState.CRTC_Flags & CRTC_VDISP_FLAG) &&
		(!(CRTC_InternalState.CRTC_Flags & CRTC_R8DT_FLAG))
		)
    {
		/* render graphics */
       CRTC_InternalState.CRTC_RenderType = CRTC_RENDER_GRAPHICS;
    }
    else
    {
		/* render border */
        CRTC_InternalState.CRTC_RenderType = CRTC_RENDER_BORDER;
    }
#endif
    /* set graphics rendering function - either graphics or border */
    if (
		((CRTC_InternalState.CRTC_Flags & (CRTC_HDISP_FLAG | CRTC_VDISP_FLAG | CRTC_R8DT_FLAG))==(CRTC_HDISP_FLAG | CRTC_VDISP_FLAG))
		)
    {
		/* render graphics */
       CRTC_InternalState.CRTC_RenderType = CRTC_RENDER_GRAPHICS;
    }
    else
    {
		/* render border */
        CRTC_InternalState.CRTC_RenderType = CRTC_RENDER_BORDER;
    }

    /* change rendering function if we are in vsync or hsync, or
    reg 8 is set to max delay + ASIC extend border is set */
    if ((CRTC_InternalState.Monitor_State.MonitorFlags & MONITOR_IN_VSYNC) || (CRTC_InternalState.CRTC_Flags & CRTC_HS_FLAG) || ASICCRTC_BlackDisplay)
    {
		/* render sync */
        CRTC_InternalState.CRTC_RenderType = CRTC_RENDER_SYNC;
    }

/*	if (PreviousRenderType!=CRTC_InternalState.CRTC_RenderType)
	{
		CRTC_NextRenderStage(0);
	}
*/
}

static void CRTC_DoVerticalSyncCounter(void)
{
    /* are we counting vertical syncs? */
    if (CRTC_InternalState.CRTC_Flags & CRTC_VSCNT_FLAG)
    {
        /* update vertical sync counter */
        CRTC_InternalState.VerticalSyncCount++;

        /* if vertical sync count = vertical sync width then
         stop vertical sync */
        if (CRTC_InternalState.VerticalSyncCount>=CRTC_InternalState.VerticalSyncWidth)
        {
			/* clear counter */
			CRTC_InternalState.VerticalSyncCount=0;

			/* clear counter */
            CRTC_ClearFlag(CRTC_VSCNT_FLAG);
        }
    }
}

/* appears that on crtc type 0 and type 3, Vertical Sync width can be reprogrammed
while it is active. The Vertical Sync Counter is 4-bit. Comparison for both appears to be equal! */
static void CRTC3_DoVerticalSyncCounter(void)
{
    /* are we counting vertical syncs? */
    if (CRTC_InternalState.CRTC_Flags & CRTC_VSCNT_FLAG)
    {
        /* update vertical sync counter */
        CRTC_InternalState.VerticalSyncCount=(CRTC_InternalState.VerticalSyncCount+1)&0x0f;

        /* if vertical sync count = vertical sync width then
         stop vertical sync */
        if (CRTC_InternalState.VerticalSyncCount==(CRTC_InternalState.VerticalSyncWidth & 0x0f))
        {
			CRTC_InternalState.VerticalSyncCount=0;

            CRTC_ClearFlag(CRTC_VSCNT_FLAG);
        }
    }
}


static BOOL     ASIC_DoScreenSplit = FALSE;
static BOOL ASIC_DoScreenSplitDelayed = FALSE;

static void ASICCRTC_GenerateLineNumber(void)
{
        /* generate line number for sprites etc */
        ASICCRTC_Line = ((CRTC_InternalState.LineCounter<<3) | (CRTC_InternalState.RasterCounter & 0x07));
}

/* do functions required when Vertical Scroll has been set */
void    ASICCRTC_SetSoftScroll(int SoftScroll)
{
        ASICCRTC_SoftScroll = (unsigned char)SoftScroll;

        ASICCRTC_VerticalSoftScroll = (unsigned char)((SoftScroll>>4) & 0x07);

        ASICCRTC_RasterLine = (unsigned char)(((CRTC_InternalState.RasterCounter + ASICCRTC_VerticalSoftScroll)&0x07));

        /* update HStart and HEnd */
        CRTC_DoReg1();
}

/* do functions required at VTot */
static void ASICCRTC_VTot(void)
{
        /* following assumes that the raster counter */
        /* is reloaded at VTot and that scroll cannot be changed */
        /* until next frame */
        if (CPCHardware==CPC_HW_CPCPLUS)
        {
/*              VisibleRasterCount = 0; */

                ASICCRTC_RasterLine = ASICCRTC_VerticalSoftScroll;
                ASICCRTC_MALine = CRTC_InternalState.MALine;
        }
}

void ASICCRTC_ScreenSplit(void)
{
        if (CPCHardware==CPC_HW_CPCPLUS)
        {

                /* this works if scroll is 0,1,2,3,4,5 or 6.
                 When scroll is 7, then it doesn't do the
                 split fault. At a guess, it does do it,
                 but the screen address is also set
                 by the scrolling hardware for the next char
                 and the error is not reproduced.

                 The fault doesn't appear when there is
                 a Vertical Adjust, because the raster count
                 will be different.
				*/
                if (ASIC_DoScreenSplit == TRUE)
                {
                        ASIC_DoScreenSplit = FALSE;

                        CRTC_InternalState.MA = ASIC_GetSSA();
                        CRTC_InternalState.MALine = CRTC_InternalState.MA;

                        /*ASICCRTC_RasterLine = CRTC_InternalState.RasterCounter; */
                        ASICCRTC_MALine = CRTC_InternalState.MALine;
                }

                if (ASIC_DoScreenSplitDelayed)
                {
                        ASIC_DoScreenSplitDelayed = FALSE;
                        ASIC_DoScreenSplit = TRUE;
                }

                if (ASIC_RasterSplitLineMatch(CRTC_InternalState.LineCounter, CRTC_InternalState.RasterCounter))
                {
                        if (!(CRTC_InternalState.CRTC_Flags & CRTC_VTOT_FLAG))
                        {
/*                              if (!ASIC_RasterLineMatch) */
                                {
                                        ASIC_DoScreenSplit = TRUE;
                                }
                        }
                        else
                        {
                                /*if (!ASIC_RasterLineMatch)*/
                                {
                                        ASIC_DoScreenSplitDelayed = TRUE;
                                }
                        }
                }

        }
}


/********************************/
/* CRTC type 3 - CPC+ ASIC CRTC */
/********************************/

void    ASICCRTC_MaxRasterMatch(void)
{
     CRTC_ClearFlag(CRTC_MR_FLAG);

     if (CRTC_InternalState.RasterCounter>=CRTCRegisters[9])
     {
        CRTC_SetFlag(CRTC_MR_FLAG);
     }
}

/* cannot update reg 7 and get it to start mid line...?
 reg 7 must be changed before line 0 starts!
*/

/* ASIC CRTC Update State */
static void ASICCRTC_UpdateState(int RegIndex)
{
  switch (RegIndex)
  {
		case 12:
		{
			 /* With CPC Plus, bit 3 of register 12 determines
				if bit 7 of printer data is 1 or 0 */
			Printer_SetDataBit7State((CRTCRegisters[12]>>3) & 1);
		}
		break;

		case 6:
		{
			if (CRTC_InternalState.RasterCounter>=CRTCRegisters[9])
			{
				CRTC_DoLineChecks();
			}
		}
		break;

		case 7:
		{
			CRTC_DoLineChecks();
		}
		break;


		case 1:
		{
			CRTC_DoReg1();
		}
		break;

		case 8:
		{
			CRTC_DoReg8();
		}
		break;

		case 3:
		{
			CRTC_DoReg3();
		}
		break;


		case 9:
		{

            if (CRTC_InternalState.HCount!=CRTCRegisters[0])
            {
                    /* CRTC_InternalState.RasterCounter>=CRTC_REG9, Not latched */
                    if (CRTC_InternalState.RasterCounter>=CRTCRegisters[9])
                    {
                            CRTC_SetFlag(CRTC_MR_FLAG);

                            if (CRTC_InternalState.LineCounter==CRTCRegisters[4])
                            {
                                    CRTC_SetFlag(CRTC_VTOT_FLAG);
                            }
                    }
                    else
                    {
                            if (CRTC_InternalState.CRTC_Flags & CRTC_MR_FLAG)
                            {
                                    /* was max raster set? */
                                    if (CRTC_InternalState.LineCounter == CRTCRegisters[4])
                                    {
                                            /* we were going to end the frame - but don't!*/
                                            CRTC_ClearFlag(CRTC_VTOT_FLAG);
                                    }
                            }

							CRTC_ClearFlag(CRTC_MR_FLAG);
                    }
            }
		}
		break;

		case 4:
		{
            /* not sure if should be equals or greater.. */
            if (CRTC_InternalState.CRTC_Flags & CRTC_MR_FLAG)
            {
                    if (CRTC_InternalState.HCount!=CRTCRegisters[0])
                    {
                            if (CRTC_InternalState.LineCounter >= CRTCRegisters[4])
                            {
                                    CRTC_SetFlag(CRTC_VTOT_FLAG);
                            }
                            else
                            {
                                    CRTC_ClearFlag(CRTC_VTOT_FLAG);
                            }
                    }
            }
		}
		break;

		default:
			break;

   }

}

static void ASICCRTC_RestartFrame(void)
{

		CRTC_InternalState.HDispAdd= 0;
        CRTC_InternalState.MA = GET_MA;
        CRTC_InternalState.MALine = CRTC_InternalState.MA;

        CRTC_InternalState.MAStore = CRTC_InternalState.MALine;

        CRTC_InternalState.RasterCounter = 0;
        CRTC_InternalState.LineCounter = 0;

        if (CRTC_InternalState.InterlaceAndVideoMode == 3)
        {
                CRTC_InternalState.RasterCounter = CRTC_InternalState.Frame;
        }
        else
        {
                CRTC_InternalState.RasterCounter = 0;
        }

        ASICCRTC_VTot();

        CRTC_SetFlag(CRTC_VDISP_FLAG);

        CRTC_SetRenderingFunction();

        CRTC_DoLineChecks();
}


/* executed for each complete line done by the CRTC */
void    ASICCRTC_DoLine(void)
{

        ASICCRTC_RasterLine = (unsigned char)((ASICCRTC_RasterLine + 1) & 0x07);

        if (CRTC_InternalState.InterlaceAndVideoMode!=3)
        {
                /* increment raster counter */
                CRTC_InternalState.RasterCounter = (unsigned char)((CRTC_InternalState.RasterCounter + 1) & 0x01f);
        }
        else
        {
                CRTC_InternalState.RasterCounter = (unsigned char)((CRTC_InternalState.RasterCounter + 2) & 0x01f);
        }


        /* update lines after VSYNC count - DEBUG */
/*        CRTC_InternalState.Monitor_State.LinesAfterVsync++; */

		CRTC3_DoVerticalSyncCounter();

        if (ASIC_RasterLineMatch == TRUE)
        {
                ASIC_RasterLineMatch = FALSE;

                ASICCRTC_MALine += CRTC_InternalState.HDispAdd;
        }


        if (CRTC_InternalState.CRTC_Flags & CRTC_MR_FLAG)
        {
/*			CRTC_ClearFlag(CRTC_VSYNC_TRIGGERED_FLAG); */

			CRTC_ClearFlag(CRTC_MR_FLAG);

                if (CRTC_InternalState.InterlaceAndVideoMode == 3)
                {
                        if (CRTCRegisters[9] & 1)
                        {
                                /* MaxRaster is odd */
                                CRTC_InternalState.RasterCounter = (unsigned char)((CRTC_InternalState.RasterCounter & 0x01)^ 0x01);
                        }
                        else
                        {
                                /* MaxRaster is even */
                                CRTC_InternalState.RasterCounter = (unsigned char)(CRTC_InternalState.RasterCounter & 0x01);
                        }
                }
                else
                {
                        CRTC_InternalState.RasterCounter = 0;
                }

                CRTC_InternalState.MALine += CRTC_InternalState.HDispAdd;
                /*MALine = MAStore; */

                if (ASICCRTC_VerticalSoftScroll==0)
                {
                        ASICCRTC_MALine += CRTC_InternalState.HDispAdd;
                }

                /* if we are to restart the frame, do not increment counter */
                if (!(CRTC_InternalState.CRTC_Flags & CRTC_VTOT_FLAG))
                {
                        CRTC_InternalState.LineCounter = (unsigned char)((CRTC_InternalState.LineCounter+1) & 0x07f);

                        /*CRTC_DoLineChecks(); */
                }

                /* following assumes that the raster counter */
                /* is reloaded at VTot and that scroll cannot be changed */
                /* until next frame */
                if (CPCHardware==CPC_HW_CPCPLUS)
                {
                        ASICCRTC_RasterLine = ASICCRTC_VerticalSoftScroll;
                }
        }


        /* are we in vertical adjust ? */
        if (CRTC_InternalState.CRTC_Flags & CRTC_VADJ_FLAG)
        {
                /* vertical adjust matches counter? */
                if (CRTC_InternalState.RasterCounter>=CRTCRegisters[5])
                {
			CRTC_ClearFlag(CRTC_VADJ_FLAG);

                        ASICCRTC_RestartFrame();
                }
        }




        if (CRTC_InternalState.CRTC_Flags & CRTC_VTOT_FLAG)
        {
		CRTC_ClearFlag(CRTC_VTOT_FLAG);

                CRTC_InternalState.Frame ^= 0x01;

                /* is it active? i.e. VertAdjust!=0 */
                if (CRTCRegisters[5]!=0)
                {
                        /* yes */
                        CRTC_InternalState.VertAdjustCount = 0;
                        CRTC_SetFlag(CRTC_VADJ_FLAG);
		}
                else
                {
                        /* restart frame */

                        ASICCRTC_RestartFrame();
                }
        }



        if (!(CRTC_InternalState.CRTC_Flags & CRTC_VADJ_FLAG))
        {
                if (CRTC_InternalState.RasterCounter>=CRTCRegisters[9])
                {
                        if (!(CRTC_InternalState.CRTC_Flags & CRTC_VADJ_FLAG))
                        {
                                if (CRTC_InternalState.LineCounter >= CRTCRegisters[4])
                                {
                                        /* Vtot match too */

                                        CRTC_SetFlag(CRTC_VTOT_FLAG);

                                }
                        }
                        CRTC_SetFlag(CRTC_MR_FLAG);
                }
        }
        if ((ASICCRTC_VerticalSoftScroll!=0) && (!(CRTC_InternalState.CRTC_Flags & CRTC_VADJ_FLAG)))
        {
                if ((ASICCRTC_RasterLine>=CRTCRegisters[9]) || (ASICCRTC_RasterLine==7))
                {
                        ASIC_RasterLineMatch = TRUE;
                }
        }

        ASICCRTC_ScreenSplit();

        CRTC_InternalState.RasterMA = (CRTC_InternalState.RasterCounter & 0x07)<<11;
		ASIC_RasterMA = ((ASICCRTC_RasterLine & 0x07)<<11);

        ASICCRTC_GenerateLineNumber();

		CRTC_DoLineChecks();

}

/********************************/
/* CRTC type 0 - HD6845S/UM6845 */
/********************************/

static void CRTC0_UpdateState(int RegIndex)
{
  switch (RegIndex)
  {
	  case 1:
          CRTC_DoReg1();
		  break;

	  case 3:
          CRTC_DoReg3();
		  break;

	  case 8:
		  CRTC_DoReg8();
		  break;

	  case 6:
		  CRTC_DoLineChecks();
		  break;

	  case 9:
            /* Reg 9 uses == compare, reg 9 latched! */
            if ((CRTC_InternalState.RasterCounter==CRTCRegisters[9]) &&
				(CRTC_InternalState.HCount<=(CRTCRegisters[0]<<1)))
            {
                    CRTC_SetFlag(CRTC_MR_FLAG);
            }
            else
            {
                    CRTC_ClearFlag(CRTC_MR_FLAG);

            }
			break;

	  default:
		  break;
  }

}

static void CRTC0_RestartFrame(void)
{

        CRTC_InternalState.MA = GET_MA;
        CRTC_InternalState.MALine = CRTC_InternalState.MA;
        /*MAColumn = 0; */
        CRTC_InternalState.RasterCounter = 0;
        CRTC_InternalState.LineCounter = 0;

        CRTC_InternalState.MAStore = GET_MA;
		CRTC_InternalState.HDispAdd = 0;

        if (CRTC_InternalState.InterlaceAndVideoMode == 3)
        {
                CRTC_InternalState.RasterCounter = CRTC_InternalState.Frame;
        }
        else
        {
                CRTC_InternalState.RasterCounter = 0;
        }

        ASICCRTC_VTot();

        CRTC_SetFlag(CRTC_VDISP_FLAG);

        CRTC_SetRenderingFunction();

        CRTC_DoLineChecks();


		/* on type 0, the first line is always visible */

#ifdef HD6845S
		/* if type 0 is a HD6845S */
		CRTC_SetFlag(CRTC_VDISP_FLAG);
#endif

}

void    CRTC0_MaxRasterMatch(void)
{
		CRTC_ClearFlag(CRTC_MR_FLAG);

        if (CRTC_InternalState.InterlaceAndVideoMode==3)
        {
                if (CRTC_InternalState.RasterCounter == ((CRTCRegisters[9]>>1) +
                        ((CRTC_InternalState.LineCounter^CRTC_InternalState.Frame) & CRTCRegisters[9] & 0x01)))
                {

                        CRTC_SetFlag(CRTC_MR_FLAG);
                }
        }
        else
        {

              if (CRTC_InternalState.RasterCounter == CRTCRegisters[9])
              {
                      CRTC_SetFlag(CRTC_MR_FLAG);
              }
        }
}


/* executed for each complete line done by the CRTC */
void    CRTC0_DoLine(void)
{
	/* on type 0 do line 6 check each line */
        ASICCRTC_RasterLine = (unsigned char)((ASICCRTC_RasterLine + 1) & 0x07);

/*      if (InterlaceAndVideoMode!=3)
      {
*/
		/* increment raster counter */
                CRTC_InternalState.RasterCounter = (unsigned char)((CRTC_InternalState.RasterCounter + 1) & 0x01f);
/*      }
      else
      {
              CRTC_InternalState.RasterCounter = (CRTC_InternalState.RasterCounter + 2) & 0x01f;
      }
*/



		CRTC3_DoVerticalSyncCounter();

        if (ASIC_RasterLineMatch == TRUE)
        {
                ASIC_RasterLineMatch = FALSE;

                ASICCRTC_MALine +=CRTC_InternalState.HDispAdd;
        }


        if (CRTC_InternalState.CRTC_Flags & CRTC_MR_FLAG)
        {
/*			CRTC_ClearFlag(CRTC_VSYNC_TRIGGERED_FLAG); */

				CRTC_ClearFlag(CRTC_MR_FLAG);

                /*if (InterlaceAndVideoMode!=3) */
                {
                        CRTC_InternalState.RasterCounter = 0;
                }
        /*else
              {
                      if (CRTCRegisters[9] & 1)
                      {
                               MaxRaster is odd,  start raster count on zero.
                              CRTC_InternalState.RasterCounter = (CRTC_InternalState.RasterCounter & 0x01)^ 0x01;
                      }
                      else
                     {
                               MaxRaster is even, start it at zero
                              CRTC_InternalState.RasterCounter = CRTC_InternalState.RasterCounter & 0x01;
                      }
              }
*/

                CRTC_InternalState.MALine +=CRTC_InternalState.HDispAdd;

                if (ASICCRTC_VerticalSoftScroll==0)
                {
                        ASICCRTC_MALine += CRTC_InternalState.HDispAdd;
                }

                if (!(CRTC_InternalState.CRTC_Flags & CRTC_VADJ_FLAG))
                {
                        if (!(CRTC_InternalState.CRTC_Flags & CRTC_VTOT_FLAG))
                        {
                                CRTC_InternalState.LineCounter = (unsigned char)((CRTC_InternalState.LineCounter+1) & 0x07f);
                        }
                }

                /* following assumes that the raster counter */
                /* is reloaded at VTot and that scroll cannot be changed */
                /* until next frame */
                if (CPCHardware==CPC_HW_CPCPLUS)
                {
                        ASICCRTC_RasterLine = ASICCRTC_VerticalSoftScroll;
                }

        }





        /* are we in vertical adjust ? */
        if (CRTC_InternalState.CRTC_Flags & CRTC_VADJ_FLAG)
        {
                CRTC_InternalState.VertAdjustCount = (unsigned char)((CRTC_InternalState.VertAdjustCount+1) & 0x01f);

                /* vertical adjust matches counter? */
                if (CRTC_InternalState.VertAdjustCount==CRTCRegisters[5])
                {
                        CRTC_ClearFlag(CRTC_VADJ_FLAG);

                        CRTC0_RestartFrame();
                }
        }

        if (CRTC_InternalState.CRTC_Flags & CRTC_VTOT_FLAG)
        {
				CRTC_ClearFlag(CRTC_VTOT_FLAG);

                /* toggle frame */
                CRTC_InternalState.Frame ^= 0x01;

                /* is it active? i.e. VertAdjust!=0 */
                if (CRTCRegisters[5]!=0)
                {
                        /* yes */
                        CRTC_InternalState.VertAdjustCount = 0;
                        CRTC_SetFlag(CRTC_VADJ_FLAG);

                        CRTC_InternalState.LineCounter = (unsigned char)((CRTC_InternalState.LineCounter + 1) & 0x07f);
                }
                else
                {
                        /* restart frame */

                        CRTC0_RestartFrame();
                }
        }

        CRTC0_MaxRasterMatch();

        if (CRTC_InternalState.CRTC_Flags & CRTC_MR_FLAG)
        {
                if (CRTC_InternalState.LineCounter == CRTCRegisters[4])
                {
                        CRTC_SetFlag(CRTC_VTOT_FLAG);
                }
        }


		CRTC_DoLineChecks();


        if ((ASICCRTC_VerticalSoftScroll!=0) && (!(CRTC_InternalState.CRTC_Flags & CRTC_VADJ_FLAG)))
        {
                if ((ASICCRTC_RasterLine>=CRTCRegisters[9]) || (ASICCRTC_RasterLine==7))
                {
                        ASIC_RasterLineMatch = TRUE;
                }
        }

        ASICCRTC_ScreenSplit();


        if (CRTC_InternalState.InterlaceAndVideoMode == 3)
        {
                /* if R9 is odd, this will change between 0 or 1 */
                int Bit0 =      ((CRTC_InternalState.LineCounter^CRTC_InternalState.Frame) & CRTCRegisters[9] & 0x01) |
                /* if R9 is even, we want Frame */
                (((CRTCRegisters[9] & 0x01)^0x01) & CRTC_InternalState.Frame);

                CRTC_InternalState.RasterMA = (CRTC_InternalState.RasterCounter<<1) | Bit0;
        }
        else
        {
                CRTC_InternalState.RasterMA = CRTC_InternalState.RasterCounter;
        }

        CRTC_InternalState.RasterMA = (CRTC_InternalState.RasterMA & 0x07)<<11;
		ASIC_RasterMA = ((ASICCRTC_RasterLine & 0x07)<<11);

        ASICCRTC_GenerateLineNumber();
}

/*************************/
/* CRTC type 1 - UM6845R */
/*************************/

static BOOL	CRTC1_HTotMatch(void)
{
	if (
		(CRTC_InternalState.HCount == CRTCRegisters[0]) ||
		(CRTC_InternalState.HCount == (CRTCRegisters[0] & 0x0fe))
		)
	{
		return TRUE;
	}

	return FALSE;
}



static void CRTC1_UpdateState(int RegIndex)
{
  switch (RegIndex)
  {
		case 0:
		{
			if (!CRTC1_HTotMatch())
            {
                    CRTC_SetFlag(CRTC_HTOT_FLAG);
            }
            else
            {
                    CRTC_ClearFlag(CRTC_HTOT_FLAG);
            }
		}
		break;

		case 7:
		{
         CRTC_DoLineChecks();
		}
		break;

        /* horizontal and vertical sync width registers */
        case 3:
        {
                CRTC_DoReg3();
        }
        break;

        case 8:
        {
                CRTC_DoReg8();
        }
        break;

        case 1:
        {
                CRTC_DoReg1();
        }
        break;

		case 6:
		{
			/* if type 1, we can change reg 6 at any time,and
			it takes effect*/
			CRTC_DoLineChecks();
		}
		break;

		case 9:
		{

		    if (!CRTC1_HTotMatch())
            {
                    if (CRTC_InternalState.RasterCounter==CRTCRegisters[9])
                    {
                            CRTC_SetFlag(CRTC_MR_FLAG);

                            if (CRTC_InternalState.LineCounter == CRTCRegisters[4])
                            {
                                    CRTC_SetFlag(CRTC_VTOT_FLAG);
                            }
                    }
                    else
                    {
                            if (CRTC_InternalState.CRTC_Flags & CRTC_MR_FLAG)
                            {
								if (!CRTC1_HTotMatch())
							   {
                                            if (CRTC_InternalState.LineCounter == CRTCRegisters[4])
                                            {
												CRTC_ClearFlag(CRTC_VTOT_FLAG);
                                            }
                                    }
                            }

                            CRTC_ClearFlag(CRTC_MR_FLAG);
					}
            }
		}
		break;

		case 4:
		{
            if (CRTC_InternalState.CRTC_Flags & CRTC_MR_FLAG)
            {
				    if (!CRTC1_HTotMatch())
	                {
                            if (CRTC_InternalState.LineCounter == CRTCRegisters[4])
                            {
                                    CRTC_SetFlag(CRTC_VTOT_FLAG);
                            }
                            else
                            {
								CRTC_ClearFlag(CRTC_VTOT_FLAG);
                            }
                    }
            }
		}
		break;

		default:
			break;
	}
}

static void CRTC1_RestartFrame(void)
{

        CRTC_InternalState.MA = GET_MA;
        CRTC_InternalState.MALine = CRTC_InternalState.MA;
        CRTC_InternalState.MAStore = GET_MA;
		CRTC_InternalState.HDispAdd = 0;

        CRTC_InternalState.LineCounter = 0;
	    CRTC_InternalState.RasterCounter = 0;

        ASICCRTC_VTot();

        CRTC_SetFlag(CRTC_VDISP_FLAG);

        CRTC_SetRenderingFunction();

        CRTC_DoLineChecks();



/*        VertAdjustPush = CRTCRegisters[5]; */

        CRTC_InternalState.MA = GET_MA;
        CRTC_InternalState.MALine = CRTC_InternalState.MA;
        CRTC_InternalState.RasterCounter = 0;
        CRTC_InternalState.LineCounter = 0;


        if (CRTC_InternalState.InterlaceAndVideoMode == 3)
        {
                CRTC_InternalState.RasterCounter = CRTC_InternalState.Frame;
        }
        else
        {
                CRTC_InternalState.RasterCounter = 0;
        }

        ASICCRTC_VTot();

        CRTC_SetFlag(CRTC_VDISP_FLAG);

        CRTC_SetRenderingFunction();

        CRTC_DoLineChecks();

}


void	CRTC1_MaxRasterMatch(void)
{
        CRTC_ClearFlag(CRTC_MR_FLAG);

		if (CRTC_InternalState.InterlaceAndVideoMode == 3)
        {
                /* in interlace sync and video mode, the raster counter increments by 1,
                and the RA output is adjusted. This compares 1/2R9 to CRTC_InternalState.RasterCounter. */
                if (CRTC_InternalState.RasterCounter==(CRTCRegisters[9]>>1))
                {
                        CRTC_SetFlag(CRTC_MR_FLAG);
                }
        }
        else
        {
                if (CRTC_InternalState.RasterCounter==CRTCRegisters[9])
                {

                        CRTC_SetFlag(CRTC_MR_FLAG);
                }
        }

}





/* on CRTC1, vsync of 40 only accepted if bc00,5 is 9!! */
/* check that if 39 is programmed with bc00,5==0 no vsync is generated */

/* executed for each complete line done by the CRTC */
void    CRTC1_DoLine(void)
{
        ASICCRTC_RasterLine = (unsigned char)((ASICCRTC_RasterLine + 1) & 0x07);

        /* increment raster counter */
        CRTC_InternalState.RasterCounter = (unsigned char)((CRTC_InternalState.RasterCounter + 1) & 0x01f);


        /* update lines after VSYNC count - used to handle multiple VSYNCs
        per frame*/
/*        CRTC_InternalState.Monitor_State.LinesAfterVsync++; */

		CRTC3_DoVerticalSyncCounter();

        if (ASIC_RasterLineMatch == TRUE)
        {
                ASIC_RasterLineMatch = FALSE;

                ASICCRTC_MALine = CRTC_InternalState.MAStore;
        }


        if (CRTC_InternalState.CRTC_Flags & CRTC_MR_FLAG)
        {

				CRTC_ClearFlag(CRTC_MR_FLAG);

        /*      if (InterlaceAndVideoMode!=3)
              {
        */
                CRTC_InternalState.RasterCounter = 0;
        /*      }
              else
              {
                      CRTC_InternalState.RasterCounter = CRTC_InternalState.RasterCounter & 0x01;
              }
*/
                CRTC_InternalState.MALine = CRTC_InternalState.MAStore;


                if (ASICCRTC_VerticalSoftScroll==0)
                {
                        ASICCRTC_MALine+=CRTC_InternalState.HDispAdd;
                }

                if (!(CRTC_InternalState.CRTC_Flags & CRTC_VTOT_FLAG))
                {
                        CRTC_InternalState.LineCounter = (unsigned char)((CRTC_InternalState.LineCounter+1) & 0x07f);
                }

                /* following assumes that the raster counter */
                /* is reloaded at VTot and that scroll cannot be changed */
                /* until next frame */
                if (CPCHardware==CPC_HW_CPCPLUS)
                {
                        ASICCRTC_RasterLine = ASICCRTC_VerticalSoftScroll;
                }

        }





        /* are we in vertical adjust ? */
        if (CRTC_InternalState.CRTC_Flags & CRTC_VADJ_FLAG)
        {
                CRTC_InternalState.VertAdjustCount = (unsigned char)((CRTC_InternalState.VertAdjustCount+1) & 0x01f);

                /* vertical adjust matches counter? */
                if (CRTC_InternalState.VertAdjustCount==CRTCRegisters[5])
                {
                        CRTC_ClearFlag(CRTC_VADJ_FLAG);
                        CRTC1_RestartFrame();

                }
        }


        /* on CRTC type 1, MA can be changed through char line 0 of frame,
        the address generated depends on MA and what the current RA is, this
        is done in the CRTC_WriteReg */
        if (CRTC_InternalState.LineCounter == 0)
        {
                CRTC_InternalState.MA = GET_MA;
                CRTC_InternalState.MALine = CRTC_InternalState.MA;
        }

        if (CRTC_InternalState.CRTC_Flags & CRTC_VTOT_FLAG)
        {
                CRTC_ClearFlag(CRTC_VTOT_FLAG);

                /* load vertical adjust */
                /*VertAdjust = CRTCRegisters[5]; */

                CRTC_InternalState.Frame ^= 0x01;

                /* is it active? i.e. VertAdjust!=0 */
                if (CRTCRegisters[5]!=0)
                {

                        /* yes */
                        CRTC_InternalState.VertAdjustCount = 0;
                        CRTC_SetFlag(CRTC_VADJ_FLAG);

                        CRTC_InternalState.LineCounter = (unsigned char)((CRTC_InternalState.LineCounter+1) & 0x07f);

                        /*CRTC_DoLineChecks(); */

                }
                else
                {
                        CRTC1_RestartFrame();
                }
        }

        /*
        if (CRTC_InternalState.RasterCounter > CRTCRegisters[9])
        {
                MaxRasterMatch = TRUE;

                if (CRTC_InternalState.LineCounter == CRTCRegisters[4])
                {
                        RestartFrame = TRUE;
                }
        }


        if (CRTC_InternalState.LineCounter > CRTCRegisters[4])
        {
                RestartFrame = TRUE;
        }
*/

        CRTC1_MaxRasterMatch();

        if (CRTC_InternalState.CRTC_Flags & CRTC_MR_FLAG)
        {
               if (!(CRTC_InternalState.CRTC_Flags & CRTC_VADJ_FLAG))
                {
                        if (CRTC_InternalState.LineCounter == CRTCRegisters[4])
                        {
                                /* Vtot match too */

                                CRTC_SetFlag(CRTC_VTOT_FLAG);
                        }
                }
        }

		CRTC_DoLineChecks();


        if ((ASICCRTC_VerticalSoftScroll!=0) && (!(CRTC_InternalState.CRTC_Flags & CRTC_VADJ_FLAG)))
        {
                if ((ASICCRTC_RasterLine>=CRTCRegisters[9]) || (ASICCRTC_RasterLine==7))
                {
                        ASIC_RasterLineMatch = TRUE;
                }
        }

        ASICCRTC_ScreenSplit();

        if (CRTC_InternalState.InterlaceAndVideoMode == 3)
        {
                CRTC_InternalState.RasterMA = ((CRTC_InternalState.RasterCounter<<1) | CRTC_InternalState.Frame);
        }
        else
        {
                CRTC_InternalState.RasterMA = CRTC_InternalState.RasterCounter;
        }

        CRTC_InternalState.RasterMA = (CRTC_InternalState.RasterMA & 0x07)<<11;
		ASIC_RasterMA = ((ASICCRTC_RasterLine & 0x07)<<11);

        ASICCRTC_GenerateLineNumber();
}

/*************************/
/* CRTC type 2 - MC6845P */
/*************************/

static void CRTC2_UpdateState(int RegIndex)
{
    /* registers that can be changing immediatly */
    switch (RegIndex)
    {

			case 7:
			{
             CRTC_DoLineChecks();
			}
			break;
            /* horizontal and vertical sync width registers */
            case 3:
            {
                    CRTC_DoReg3();
            }
            break;


            case 8:
            {
                    CRTC_DoReg8();
            }
            break;

            case 1:
            {
                    CRTC_DoReg1();
            }
            break;

            default:
                    break;
    }




}

static void CRTC2_RestartFrame(void)
{
	CRTC_InternalState.HDispAdd = 0;
	CRTC_InternalState.RasterCounter = 0;

        CRTC_InternalState.MA = GET_MA;
        CRTC_InternalState.MALine = CRTC_InternalState.MA;

        CRTC_InternalState.LineCounter = 0;

        ASICCRTC_VTot();

        CRTC_SetFlag(CRTC_VDISP_FLAG);

        CRTC_SetRenderingFunction();

        CRTC_DoLineChecksCRTC2();
}


void	CRTC2_MaxRasterMatch(void)
{
        CRTC_ClearFlag(CRTC_MR_FLAG);

		if (CRTC_InternalState.InterlaceAndVideoMode == 3)
        {
                /* in interlace sync and video mode, the raster counter increments by 1,
                and the RA output is adjusted. This compares 1/2R9 to CRTC_InternalState.RasterCounter. */
                if (CRTC_InternalState.RasterCounter==(CRTCRegisters[9]>>1))
                {
                        CRTC_SetFlag(CRTC_MR_FLAG);
                }
        }
        else
        {
                if (CRTC_InternalState.RasterCounter==CRTCRegisters[9])
                {
                        CRTC_SetFlag(CRTC_MR_FLAG);
                }
        }

}





/* on CRTC1, vsync of 40 only accepted if bc00,5 is 9!! */
/* check that if 39 is programmed with bc00,5==0 no vsync is generated */

/* executed for each complete line done by the CRTC */
void    CRTC2_DoLine(void)
{
        ASICCRTC_RasterLine = (unsigned char)((ASICCRTC_RasterLine + 1) & 0x07);

        /* increment raster counter */
        CRTC_InternalState.RasterCounter = (unsigned char)((CRTC_InternalState.RasterCounter + 1) & 0x01f);

        /* update lines after VSYNC count - used to handle multiple VSYNCs
        per frame*/
/*        CRTC_InternalState.Monitor_State.LinesAfterVsync++; */

		CRTC_DoVerticalSyncCounter();

        if (ASIC_RasterLineMatch == TRUE)
        {
                ASIC_RasterLineMatch = FALSE;

              ASICCRTC_MALine += CRTC_InternalState.HDispAdd;
/*                ASICCRTC_MALine = CRTC_InternalState.MAStore; */
        }


        if (CRTC_InternalState.CRTC_Flags & CRTC_MR_FLAG)
        {
/*			CRTC_ClearFlag(CRTC_VSYNC_TRIGGERED_FLAG); */

			CRTC_ClearFlag(CRTC_MR_FLAG);

            CRTC_InternalState.RasterCounter = 0;

                CRTC_InternalState.MALine += CRTC_InternalState.HDispAdd;

                if (ASICCRTC_VerticalSoftScroll==0)
                {
                        ASICCRTC_MALine+=CRTC_InternalState.HDispAdd;
                }

                if (!(CRTC_InternalState.CRTC_Flags & CRTC_VTOT_FLAG))
                {
                        CRTC_InternalState.LineCounter = (unsigned char)((CRTC_InternalState.LineCounter+1) & 0x07f);

                        CRTC_DoLineChecksCRTC2();
                }

                /* following assumes that the raster counter */
                /* is reloaded at VTot and that scroll cannot be changed */
                /* until next frame */
                if (CPCHardware==CPC_HW_CPCPLUS)
                {
                        ASICCRTC_RasterLine = ASICCRTC_VerticalSoftScroll;
                }

        }





        /* are we in vertical adjust ? */
        if (CRTC_InternalState.CRTC_Flags & CRTC_VADJ_FLAG)
        {
                CRTC_InternalState.VertAdjustCount = (unsigned char)((CRTC_InternalState.VertAdjustCount+1) & 0x01f);

                /* vertical adjust matches counter? */
                if (CRTC_InternalState.VertAdjustCount==CRTCRegisters[5])
                {
                        CRTC_ClearFlag(CRTC_VADJ_FLAG);
                        CRTC2_RestartFrame();

                }
        }

        if (CRTC_InternalState.CRTC_Flags & CRTC_VTOT_FLAG)
        {
			CRTC_ClearFlag(CRTC_VTOT_FLAG);

                CRTC_InternalState.Frame ^= 0x01;

                /* is it active? i.e. VertAdjust!=0 */
                if (CRTCRegisters[5]!=0)
                {

                        /* yes */
                        CRTC_InternalState.VertAdjustCount = 0;
                        CRTC_SetFlag(CRTC_VADJ_FLAG);

                        CRTC_InternalState.LineCounter = (unsigned char)((CRTC_InternalState.LineCounter+1) & 0x07f);

                        CRTC_DoLineChecksCRTC2();

                }
                else
                {
                        CRTC2_RestartFrame();
                }
        }

        CRTC2_MaxRasterMatch();

        if (CRTC_InternalState.CRTC_Flags & CRTC_MR_FLAG)
        {
               if (!(CRTC_InternalState.CRTC_Flags & CRTC_VADJ_FLAG))
                {
					if (CRTC_InternalState.InterlaceAndVideoMode == 3)
					{
						if ((CRTC_InternalState.LineCounter>>1) == CRTCRegisters[4])
						{
							CRTC_SetFlag(CRTC_VTOT_FLAG);
						}
					}
					else
					{

                        	if (CRTC_InternalState.LineCounter == CRTCRegisters[4])
                        	{
                                	/* Vtot match too */

                                	CRTC_SetFlag(CRTC_VTOT_FLAG);
                        	}
					}
                }
		}


        if ((ASICCRTC_VerticalSoftScroll!=0) && (!(CRTC_InternalState.CRTC_Flags & CRTC_VADJ_FLAG)))
        {
                if ((ASICCRTC_RasterLine>=CRTCRegisters[9]) || (ASICCRTC_RasterLine==7))
                {
                        ASIC_RasterLineMatch = TRUE;
                }
        }

        ASICCRTC_ScreenSplit();

        if (CRTC_InternalState.InterlaceAndVideoMode == 3)
        {
                CRTC_InternalState.RasterMA = ((CRTC_InternalState.RasterCounter<<1) | CRTC_InternalState.Frame);
        }
        else
        {
                CRTC_InternalState.RasterMA = CRTC_InternalState.RasterCounter;
        }

        CRTC_InternalState.RasterMA = (CRTC_InternalState.RasterMA & 0x07)<<11;
		ASIC_RasterMA = ((ASICCRTC_RasterLine & 0x07)<<11);


        ASICCRTC_GenerateLineNumber();
}





/* code that counts monitor position. If HSYNC occurs at right time,
goes onto next line ok */

void	CRTC_Monitor_NewFrame(void);

#ifdef SIMPLE_MONITOR_EMULATION
void    CRTC_NextMonitorLine()
{
    /* for monitor sync */
    CRTC_InternalState.Monitor_State.MonitorHorizontalCount = 0;

	CRTC_InternalState.Monitor_State.MonitorScanLineCount++;

#ifdef LESS_MULTS
	Render_NextLine();
#endif

    if (CRTC_InternalState.Monitor_State.MonitorScanLineCount>=(39*8))
    {
		CRTC_Monitor_NewFrame();
	}
}
#else

void    CRTC_NextMonitorLine(void)
{

	unsigned long ScanLineCount;

	/* update vsync retrace */
	if (CRTC_InternalState.Monitor_State.VSyncRetraceCount>0)
	{
		CRTC_InternalState.Monitor_State.VSyncRetraceCount--;
		return;
	}


	ScanLineCount = CRTC_InternalState.Monitor_State.MonitorScanLineCount;

	ScanLineCount++;

#ifdef LESS_MULTS
	Render_NextLine();
#endif

    if (ScanLineCount>=(39*8))
    {
			ScanLineCount = 0;

#ifdef LESS_MULTS
			Render_FirstLine();
#endif

	}


	CRTC_InternalState.Monitor_State.MonitorScanLineCount = ScanLineCount;
}
#endif


void	CRTC_MonitorReset(void)
{
	CRTC_InternalState.Monitor_State.CharsAfterHsync = 0;
	CRTC_InternalState.Monitor_State.HSyncRetraceCount = 0;
	CRTC_InternalState.Monitor_State.LinesAfterVsync = 0;
	CRTC_InternalState.Monitor_State.MonitorFlags = 0;
	CRTC_InternalState.Monitor_State.MonitorHorizontalCount = 0;
	CRTC_InternalState.Monitor_State.MonitorScanLineCount = 0;
	CRTC_InternalState.Monitor_State.MonitorVsyncCount = 0;
	CRTC_InternalState.Monitor_State.VSyncRetraceCount = 0;
}


void	CRTC_DoMonitorHsync(void)
{
/*	CRTC_InternalState.Monitor_State.MonitorFlags&=~MONITOR_IN_HSYNC; */

	CRTC_InternalState.Monitor_State.CharsAfterHsync=0;

	CRTC_NextMonitorLine();
}

void	CRTC_Monitor_NewFrame(void)
{
		CRTC_InternalState.Monitor_State.MonitorScanLineCount = 0;

#ifdef LESS_MULTS
        Render_FirstLine();
#endif

#ifndef SIMPLE_MONITOR_EMULATION
	/* set vsync retrace */
	CRTC_InternalState.Monitor_State.VSyncRetraceCount = 312-Y_CRTC_LINE_HEIGHT;
#endif
}

/*
static int AccumulatedCycles;

void	CRTC_RenderCycles(void)
{
	int i;

    if (!CRTC_InternalState.DontRender)
    {
		if (AccumulatedCycles!=0)
		{
			for (i=0; i<AccumulatedCycles; i++)
			{
		        CRTC_InternalState.pCRTC_RenderFunction();
			}
		}
	}

	AccumulatedCycles = 0;
}
*/

/* 46 = hsync pos
62 long

 46, 6 chars hsync (52),10	 62 chars not ok
 46, 6 chars hsync (52),11	 63 chars ok
 46, 6 chars hsync (52),12	 64 chars ok
 46, 6 chars hsync (52),13	 65 chars ok
 46, 6 chars hsync (52),14	 66 chars ok
 46, 6 chars hsync (52),15	 67 chars not ok
*/
void	CRTC_StartMonitorHsync(void)
{
	/* start horizontal sync */
	CRTC_InternalState.Monitor_State.HSyncRetraceCount = 8;


/*	if (CRTC_InternalState.CRTC_Flags & CRTC_HS_Monitor_State.MonitorFlags & MONITOR_IN_HSYNC)
	{
		if (CRTC_InternalState.Monitor_State.HSyncRetraceCount>=CRTC_InternalState.GA_State.GA_HSyncWidth)
		{

			CRTC_InternalState.Monitor_State.HSyncRetraceCount = CRTC_InternalState.GA_State.GA_HSyncWidth;
		}
	}
*/

	/* zeroise counters */
	CRTC_InternalState.Monitor_State.MonitorHorizontalCount = 0;
	CRTC_InternalState.Monitor_State.CharsAfterHsync = 0;
	CRTC_InternalState.Monitor_State.MonitorFlags|=MONITOR_IN_HSYNC;

	CRTC_NextMonitorLine();
}

static void CRTC_InterlaceControl_SetupDelayedVsync(void)
{
	/* don't set VSYNC */

	/* set VSYNC at next HTOT/2 */
	CRTC_InternalState.CRTC_HalfHtotFlags = CRTC_VS_FLAG;
}

static void CRTC_InterlaceControl_FinishDelayedVsync(void)
{
	/* don't clear VSYNC now, clear VSYNC at next HTOT/2 */
	CRTC_InternalState.CRTC_HalfHtotFlags = 0;
}

/* setup a VSYNC to start at the beginning of the line */
static void CRTC_InterlaceControl_SetupStandardVsync(void)
{
	/* set VSYNC immediatly */
	CRTC_SetFlag(CRTC_VS_FLAG);

	/* keep VSYNC set at HTOT/2 */
	CRTC_InternalState.CRTC_HalfHtotFlags = CRTC_VS_FLAG;
}

static void CRTC_InterlaceControl_FinishStandardVsync(void)
{
	/* clear vsync */
	CRTC_ClearFlag(CRTC_VS_FLAG);
	/* no VSYNC on next HTOT/2 */
	CRTC_InternalState.CRTC_HalfHtotFlags = 0;
}

/* call when VSYNC has begun */
static void CRTC_InterlaceControl_VsyncStart(void)
{
	if ((CRTC_InternalState.InterlaceAndVideoMode & 1)==0)
	{
		/* no interlace */
		CRTC_InterlaceControl_SetupStandardVsync();
	}
	else
	{
		/* interlace */

		if (CRTC_InternalState.Frame!=0)
		{
			CRTC_InterlaceControl_SetupStandardVsync();
		}
		else
		{
			CRTC_InterlaceControl_SetupDelayedVsync();
		}
	}

}

static void CRTC_InterlaceControl_VsyncEnd(void)
{
	if ((CRTC_InternalState.InterlaceAndVideoMode & 1)==0)
	{
		/* no interlace */
		CRTC_InterlaceControl_FinishStandardVsync();
	}
	else
	{
		/* interlace */

		if (CRTC_InternalState.Frame!=0)
		{
			CRTC_InterlaceControl_FinishStandardVsync();
		}
		else
		{
			CRTC_InterlaceControl_FinishDelayedVsync();
		}
	}
}

void	Graphics_Update(void)
{
        static unsigned long s_lastRenderState = 0x0ffffffffUL;
        unsigned long renderState;

	/* frame-skip render override */
	if (CRTC_InternalState.DontRender)
		return;

	/* in vertical retrace? don't render */
	if (CRTC_InternalState.Monitor_State.VSyncRetraceCount!=0)
		return;

	if (CRTC_InternalState.Monitor_State.HSyncRetraceCount!=0)
		return;

        /* Recompute render mode only when relevant state actually changes. */
        renderState = (CRTC_InternalState.CRTC_Flags &
                (CRTC_HDISP_FLAG | CRTC_VDISP_FLAG | CRTC_R8DT_FLAG | CRTC_HS_FLAG));
        if (CRTC_InternalState.Monitor_State.MonitorFlags & MONITOR_IN_VSYNC)
                renderState |= 0x010000UL;
        if (ASICCRTC_BlackDisplay)
                renderState |= 0x020000UL;

        if (renderState != s_lastRenderState)
        {
                s_lastRenderState = renderState;
                CRTC_SetRenderingFunction();
        }

	switch (CRTC_InternalState.CRTC_RenderType)
	{
		case CRTC_RENDER_GRAPHICS:
		{
			CRTC_InternalState.pCRTC_RenderGraphicsFunction();
		}
		break;

		case CRTC_RENDER_BORDER:
		{
			CRTC_InternalState.pCRTC_RenderBorder();
		}
		break;

		case CRTC_RENDER_SYNC:
		{
			CRTC_InternalState.pCRTC_RenderSync();
		}
		break;
	}
}

/*HD6845S_MONITOR_HSYNC_LEN       8 */

unsigned long PreviousHsyncFlags = 0;

void	Monitor_UpdateHorizontal(void)
{


	if (CRTC_InternalState.Monitor_State.HSyncRetraceCount>0)
	{

	/* changed state? */
	if (((CRTC_InternalState.CRTC_Flags^PreviousHsyncFlags) & CRTC_HS_FLAG)!=0)
	{
		/* high-low */
		if ((CRTC_InternalState.CRTC_Flags & CRTC_HS_FLAG)==0)
		{
			CRTC_InternalState.Monitor_State.HSyncRetraceCount = 0;
			return;
		}
	}
#ifndef SIMPLE_MONITOR_EMULATION
/*		if (CRTC_InternalState.CRTC_Flags & CRTC_HS_FLAG)
		{
			int GA_HsyncEnd = CRTC_InternalState.GA_State.GA_HSyncWidth;

			if (CRTC_InternalState.HorizontalSyncCount<=GA_HsyncEnd)
			{
				CRTC_InternalState.Monitor_State.HSyncRetraceCount = CRTC_InternalState.HorizontalSyncCount - GA_HsyncEnd;
			}
		}
		else
		{
*/
	CRTC_InternalState.Monitor_State.HSyncRetraceCount--;
/*	} */
#endif
		return;
	}


	CRTC_InternalState.Monitor_State.MonitorFlags&=~MONITOR_IN_HSYNC;

	/* update horizontal count */
	CRTC_InternalState.Monitor_State.MonitorHorizontalCount++;
	CRTC_InternalState.Monitor_State.CharsAfterHsync++;

#if 0
	/* changed state? */
	if (((CRTC_InternalState.CRTC_Flags^PreviousHsyncFlags) & CRTC_HS_FLAG)!=0)
	{
		/* low-high */
		if ((CRTC_InternalState.CRTC_Flags & CRTC_HS_FLAG)!=0)
		{

		/*	if ((CRTC_InternalState.Monitor_State.CharsAfterHsync>49) &&
				(CRTC_InternalState.Monitor_State.CharsAfterHsync<64))
			{
		*/
			if (CRTC_InternalState.Monitor_State.CharsAfterHsync>49)
			{
				CRTC_StartMonitorHsync();
				return;
			}
		}

	}
#endif

	PreviousHsyncFlags = CRTC_InternalState.CRTC_Flags;

	if (CRTC_InternalState.Monitor_State.CharsAfterHsync==(64-8))
	{
		CRTC_StartMonitorHsync();
	}
}


/* executed each NOP cycle performed by the Z80 */
void CRTC_DoCycles(int Cycles)
{
	/* update light pen */
//	CRTC_LightPen_Update(Cycles);

        while (Cycles-- > 0)
    {
            /* increment horizontal count */
            CRTC_InternalState.HCount=(unsigned char)((CRTC_InternalState.HCount+1) & 0x0ff);

#ifdef SIMPLE_MONITOR_EMULATION
            CRTC_InternalState.Monitor_State.MonitorHorizontalCount = (unsigned char)((CRTC_InternalState.Monitor_State.MonitorHorizontalCount+1) & 0x03f);

                CRTC_InternalState.Monitor_State.CharsAfterHsync++;


				if (CRTC_InternalState.Monitor_State.CharsAfterHsync>=64)
				{
					if (CRTC_InternalState.HorizontalSyncWidth==0)
					{
						CRTC_DoMonitorHsync();
					}
				}

#else

			Monitor_UpdateHorizontal();
#endif
            if (CRTC_InternalState.CRTC_Flags & CRTC_HTOT_FLAG)
            {
				unsigned long PreviousFlags = CRTC_InternalState.CRTC_Flags;
				CRTC_ClearFlag(CRTC_HTOT_FLAG);

		       /* update lines after VSYNC count - used to handle multiple VSYNCs
				per frame*/
					CRTC_InternalState.Monitor_State.LinesAfterVsync++;

                    /* zero count */
                    CRTC_InternalState.HCount = 0;

					switch (CRTC_InternalState.CRTC_Type)
					{
						case 0:
						{
							CRTC0_DoLine();
						}
						break;

						case 1:
						{
							CRTC1_DoLine();
						}
						break;

						case 2:
						{
							CRTC2_DoLine();
						}
						break;

						case 3:
						{
							ASICCRTC_DoLine();
						}
						break;

						case 4:
						{
							CRTC1_DoLine();
						}
						break;
					}

                    if ((ASICCRTC_SoftScroll & 0x080)!=0)
                    {
                            /* doesn't work if the extend border is not set */


                            /* initialise graphics long data with data hidden by soft scroll
                            extended border */
                            {
                                    unsigned short Addr;
                                    unsigned short LocalMA;

                                    LocalMA = ASICCRTC_MALine + CRTC_InternalState.HCount;

                                    /* get screen scrolling */
                                    Addr = ((LocalMA & 0x03000)<<2) | ((LocalMA & 0x03ff)<<1);

                                    /* take address, and put in vertical line count in place of these 3 bits. */
                                    Addr |= (Addr & (0x0ffff^0x03800)) | ((ASICCRTC_RasterLine & 0x07)<<11);

                                    CRTC_InternalState.GraphicsLong = ((Z80MemoryBase[(unsigned long)(Addr)])<<8)
                                                                    | (Z80MemoryBase[(unsigned long)(Addr+1)]);
                            }
                    }


					if (((PreviousFlags^CRTC_InternalState.CRTC_Flags) & CRTC_VSCNT_FLAG)!=0)
					{
						/* vsync counter bit has changed state */
						if (CRTC_InternalState.CRTC_Flags & CRTC_VSCNT_FLAG)
						{
							/* change from vsync counter inactive to active */
							CRTC_InterlaceControl_VsyncStart();
						}
						else
						{
							/* change from counter active to inactive */
							CRTC_InterlaceControl_VsyncEnd();
						}
					}


					{
						{
							/* monitor vsync */

							/* if we want to perform a monitor vsync, do this */
							if (CRTC_InternalState.Monitor_State.MonitorFlags & MONITOR_VSYNC_SYNC)
							{
									CRTC_InternalState.Monitor_State.MonitorFlags &=~MONITOR_VSYNC_SYNC;

									if ((CRTC_InternalState.Monitor_State.LinesAfterVsync>=272))
									{
										CRTC_InternalState.Monitor_State.LinesAfterVsync = 0;

										CRTC_Monitor_NewFrame();
									}
							}
						}

					}

					CRTC_InternalState.CRTC_FlagsAtLastHtot = CRTC_InternalState.CRTC_Flags;

                    /* must be called each line to turn on the correct sprites! */
                    ASIC_GenerateSpriteActiveMaskForLine(ASICCRTC_Line);

			 }


			/* does horizontal equal Htot? */
			if (CRTC_InternalState.HCount == CRTCRegisters[0])
			{
				CRTC_SetFlag(CRTC_HTOT_FLAG);
			}

			if (CRTC_InternalState.HCount == (CRTCRegisters[0]>>1))
			{
					unsigned long Flags;

					/* get flags */
					Flags = CRTC_InternalState.CRTC_Flags;
					/* clear VSYNC flag */
					Flags &= ~CRTC_VS_FLAG;
					/* set/clear VSYNC flag */
					Flags |= CRTC_InternalState.CRTC_HalfHtotFlags;
					/* store new flags */
					CRTC_InternalState.CRTC_Flags = Flags;
			}

            /* Horizontal Sync Width Counter */
            /* are we counting horizontal syncs? */
            if (CRTC_InternalState.CRTC_Flags & CRTC_HS_FLAG)
            {
                    CRTC_InternalState.HorizontalSyncCount++;

#ifdef SIMPLE_MONITOR_EMULATION
				   /* has count just been cut? or has it run it's natural course? */
                        if ((CRTC_InternalState.HorizontalSyncCount==CRTC_InternalState.GA_State.GA_HSyncWidth))			/*|| (CRTC_InternalState.GA_State.GA_HSyncWidth==0))*/
                        {
							if (CRTC_InternalState.Monitor_State.CharsAfterHsync >= 63)
							{
								CRTC_DoMonitorHsync();
							}
                        }
#endif
                    /* if horizontal sync count = Horizontal Sync Width then
                     stop horizontal sync */
                    if (CRTC_InternalState.HorizontalSyncCount==CRTC_InternalState.HorizontalSyncWidth)
                    {
							CRTC_InternalState.HorizontalSyncCount=0;

                            /* stop horizontal sync counter */
                            CRTC_ClearFlag(CRTC_HS_FLAG);


                            /* call functions that would happen on a HSYNC */
                            CRTC_DoHsync();
                    }
            }

            /* does current horizontal count equal position to start horizontal sync? */
            if (CRTC_InternalState.HCount == CRTCRegisters[2])
            {
                    /* if horizontal sync = 0, in the HD6845S no horizontal
                    sync is generated. The input to the flip-flop is 1 from
                    both Horizontal Sync Position and HorizontalSyncWidth, and
                    the HSYNC is not even started */
                    if (CRTC_InternalState.HorizontalSyncWidth!=0)
                    {
							/* are we already in a HSYNC? */
                            if (!(CRTC_InternalState.CRTC_Flags & CRTC_HS_FLAG))
                            {
                                    /* no.. */

                                    /* enable horizontal sync counter */
                                    CRTC_SetFlag(CRTC_HS_FLAG);

                                    /* initialise counter */
                                    CRTC_InternalState.HorizontalSyncCount = 0;

                            }
                    }
            }


            if (CRTC_InternalState.HCount == CRTC_InternalState.HStart)
            {
                    /* enable horizontal display */
                    CRTC_SetFlag(CRTC_HDISP_FLAG);

            }

            /* register 8 only defines delay to add to HDisp */
            if (CRTC_InternalState.HCount == CRTC_InternalState.HEnd)
            {
                    CRTC_ClearFlag(CRTC_HDISP_FLAG);

                    CRTC_InternalState.HDispAdd = CRTCRegisters[1];

                    /* if max raster matches, store current MA */
                    if (CRTC_InternalState.CRTC_Flags & CRTC_MR_FLAG)
                    {
                            CRTC_InternalState.MAStore = CRTC_InternalState.MALine + CRTC_InternalState.HCount;
                    }
           }
#if 0
#ifdef WIP
				if (CRTC_InternalState.Monitor_State.CharsAfterHsync==50)
				{
					CRTC_InternalState.Monitor_State.MonitorFlags|=MONITOR_IN_HSYNC;
				}

				if (CRTC_InternalState.Monitor_State.CharsAfterHsync==66)
				{
					CRTC_DoMonitorHsync();
				}
#endif
#endif
		Graphics_Update();

	}
}

#if 0

/* HStage_Types */
#define		CRTC_HSTAGE_CHECK_HTOT			0x0001
#define		CRTC_HSTAGE_CHECK_HEND			0x0002
#define		CRTC_HSTAGE_CHECK_HSYNC_START	0x0004
#define		CRTC_HSTAGE_CHECK_HSYNC_END		0x0008
#define		CRTC_HSTAGE_CHECK_HSTART		0x0010

/* HCount of next stage */
unsigned char HStage_Next;
/* describes what to check at this h stage */
unsigned char HStage_Type;

unsigned char HStageCheck;

static void	CRTC_HStage_UpdateClosestStage(void)
{
	HStage_Type = 0;
	HStage_Next = 0x0ff;

	/* check HTOT */
	if (HStageCheck & CRTC_HSTAGE_CHECK_HTOT)
	{
		/* is HTOT closer than current H Stage? */
		if (CRTCRegisters[0]<=HStage_Next)
		{
			HStage_Type|=CRTC_HSTAGE_CHECK_HTOT;

			if (CRTCRegisters[0]<HStage_Next)
			{
				/* if less, then we do not want to check any other bits, but this one.
				if equal, we check this too as well as previously set flags */
				HStage_Type&=CRTC_HSTAGE_CHECK_HTOT;
			}

			HStage_Next = CRTCRegisters[0];
		}
	}

	/* check HEND */
	if (HStageCheck & CRTC_HSTAGE_CHECK_HEND)
	{
		/* is HEND closer than current H Stage? */
		if (CRTC_InternalState.HEnd<=HStage_Next)
		{
			HStage_Type|=CRTC_HSTAGE_CHECK_HEND;

			if (CRTC_InternalState.HEnd<HStage_Next)
			{
				/* if less, then we do not want to check any other bits, but this one.
				if equal, we check this too as well as previously set flags */
				HStage_Type&=CRTC_HSTAGE_CHECK_HEND;
			}

			HStage_Next = CRTC_InternalState.HEnd;
		}
	}


	if (HStageCheck & CRTC_HSTAGE_CHECK_HSTART)
	{
		/* is HSTART closer than current H Stage? */
		if (CRTC_InternalState.HStart<=HStage_Next)
		{
			HStage_Type |= CRTC_HSTAGE_CHECK_HSTART;

			if (CRTC_InternalState.HStart<HStage_Next)
			{
				/* if less, then we do not want to check any other bits, but this one.
				if equal, we check this too as well as previously set flags */
				HStage_Type &= CRTC_HSTAGE_CHECK_HSTART;
			}

			HStage_Next = CRTC_InternalState.HStart;
		}
	}
}



#endif

#if 0
static int AccumulatedCycles;
static int RenderStageCount;
static CRTC_RENDER_STAGE	*pCurrentRenderStage;
static CRTC_RENDER_STAGE	RenderStages[64];


static void CRTC_EndRenderStage(void)
{
	/* set number of cycles in this stage */
	pCurrentRenderStage->Count = AccumulatedCycles;
	AccumulatedCycles = 0;
}

static void	CRTC_NextRenderStage(unsigned long Flags)
{
	CRTC_EndRenderStage();

	/* next render stage */
	pCurrentRenderStage++;
	/* update number of render stages */
	RenderStageCount++;

	/* store current MA */
	pCurrentRenderStage->MA = CRTC_InternalState.MALine + CRTC_InternalState.HCount;
	/* store rendering method to use */
	pCurrentRenderStage->RenderType = CRTC_InternalState.CRTC_RenderType;
	pCurrentRenderStage->Flags = Flags;
}

static void	CRTC_FirstRenderStage(void)
{
	RenderStageCount=1;
	pCurrentRenderStage = &RenderStages[0];
	pCurrentRenderStage->MA = CRTC_InternalState.MALine + CRTC_InternalState.HCount;
	pCurrentRenderStage->RenderType = CRTC_InternalState.CRTC_RenderType;
	pCurrentRenderStage->Flags = 0;
}
#endif

void    CRTC_Reset(void)
{
        int i;


		CRTC_InternalState.DontRender = TRUE;

		/* vsync counter not active */
        CRTC_ClearFlag(CRTC_VSCNT_FLAG);
		/* not in hsync */
        CRTC_ClearFlag(CRTC_HS_FLAG);
		/* not in a vsync */
        CRTC_ClearFlag(CRTC_VS_FLAG);
		/* not reached end of line */
		CRTC_ClearFlag(CRTC_HTOT_FLAG);
		/* not reached end of frame */
		CRTC_ClearFlag(CRTC_VTOT_FLAG);

		CRTC_InternalState.GA_State.GA_Flags &=~GA_HSYNC_FLAG;
		CRTC_InternalState.GA_State.GA_Flags &=~GA_VSYNC_FLAG;

		/* not reached last raster in char */
		CRTC_ClearFlag(CRTC_MR_FLAG);
		/* not in vertical adjust */
        CRTC_ClearFlag(CRTC_VADJ_FLAG);
		/* do not display graphics */
		CRTC_ClearFlag(CRTC_VDISP_FLAG);
		CRTC_ClearFlag(CRTC_HDISP_FLAG);

		CRTC_ClearFlag(CRTC_R8DT_FLAG);

        /* reset all registers */
        for (i=0; i<16; i++)
        {
                /* select register */
                CRTC_RegisterSelect(i);

                /* write data */
                CRTC_WriteData(0);
        }

        /* reset CRTC internal registers */

        /* reset horizontal count */
        CRTC_InternalState.HCount = 0;
        /* reset line counter (vertical count) */
        CRTC_InternalState.LineCounter = 0;
        /* reset raster count */
        CRTC_InternalState.RasterCounter = 0;
        /* reset MA */
        CRTC_InternalState.MA = 0;
        CRTC_InternalState.MALine = CRTC_InternalState.MA;
        CRTC_InternalState.Frame = 0;

		CRTC_MonitorReset();

#ifdef LESS_MULTS
        Render_FirstLine();
#endif
}

void	CRTC_SetRenderState(BOOL State)
{
	CRTC_InternalState.DontRender = State;
}

static unsigned long ClocksToLightPenTrigger;
static unsigned long LightPen_Strobe = 0;
#if 0
/* x pos in pixel coords, y pos in pixel coords */
void	CRTC_LightPen_Trigger(int XPos, int YPos)
{
	unsigned long NopsInX, NopsInY;
	/* x pos is in mode 2 pixels
	 y pos is in mode 2 pixels

	 x,y pos are in visible display area

	 in mode 2, there are 16 pixels per nop.
	 each nop is 2 bytes

	 not correct.
*/
	/* us per x position */
	NopsInX = (XPos>>4);
	/* 64 us per line */
	NopsInY = (YPos<<6);

	ClocksToLightPenTrigger = NopsInY + NopsInX;


	LightPen_Strobe = 1;

}

void	CRTC_LightPen_Trigger(int X, int Y)
{
	unsigned long Nops;
	unsigned long MonitorLine;
	unsigned long LinesToTrigger;
	unsigned long MonitorX;
	unsigned long CharsToTrigger;

	/* get current monitor line we are on. */
	MonitorLine = CRTC_InternalState.Monitor_State.MonitorScanLineCount;

	/* work out number of complete lines until the trigger should occur */
	if (Y>MonitorLine)
	{
		LinesToTrigger = Y - MonitorLine;
	}
	else
	{
		LinesToTrigger = (312 - MonitorLine) + Y;
	}

	Nops = (LinesToTrigger<<6);

	MonitorX = CRTC_InternalState.Monitor_State.MonitorHorizontalCount;

	X = (X>>(4-1)) + X_CRTC_CHAR_OFFSET;

	if (X>MonitorX)
	{
		CharsToTrigger = X - MonitorX;
	}
	else
	{
		CharsToTrigger = (64 - MonitorX) + X;
	}

	Nops += (CharsToTrigger>>1);


/*		unsigned long Nops;

		Nops = (YPos<<6) + (XPos>>5) + ((8*10)*64);
*/
//		CRTC_LightPen_Trigger(Nops);



	LightPen_Strobe = 1;

	ClocksToLightPenTrigger = Nops;
}

void	CRTC_LightPen_Update(unsigned long NopsPassed)
{
	if (LightPen_Strobe)
	{
		if (NopsPassed>=ClocksToLightPenTrigger)
		{
			/* signal that light pen register is full */
			UM6845R_StatusRegister |=0x01;

			/* grab current MA */
			CRTC_LightPenMA = CRTC_InternalState.MALine + CRTC_InternalState.HCount;

			CRTC_LightPenMA &= ~0x0c000;

			/* re-write Light Pen data (read-only!) */
			CRTCRegisters[16] = (CRTC_LightPenMA>>8) & 0x0ff;
			CRTCRegisters[17] = CRTC_LightPenMA & 0x0ff;


			LightPen_Strobe = 0;
		}

		ClocksToLightPenTrigger -= NopsPassed;

	}
}
#endif
