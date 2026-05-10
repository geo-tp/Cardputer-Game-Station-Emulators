/*
$Date: 2009-10-30 05:26:46 +0100 (ven., 30 oct. 2009) $
$Rev: 71 $
*/
#include <stdio.h>
#include <string.h>
#include <time.h>

//#include "entry.h"
#include "WSRender.h"
#include "WS.h"
#include "WSApu.h"
#include "WSInput.h"
#include "WSPdata.h"
#include "WSBandai.h"
#include "cpu/necintrf.h"

extern void ws_graphics_paint(void); // SDL drawing of screen
extern unsigned long SDL_UXTimerRead(void);
extern void init_ModRM_tables(void);

#define IPeriod 32          // HBlank/8 (256/8)
#define MAX_SRAM_ALLOCATED 0x8000  // 32KB

int Run;
BYTE *Page[16];             // �o���N���蓖��
BYTE* IRAM = NULL;          // ����RAM 64kB = Page[0]
BYTE *IO;                   // IO
BYTE gDummyByte = 0xA0;     // valeur open-bus par défaut
BYTE *MemDummy = &gDummyByte;
BYTE** ROMMap;              // C-ROM�o���N�}�b�v
int ROMBanks;               // C-ROM�o���N��
BYTE **RAMMap;              // C-RAM�o���N�}�b�v
int RAMBanks;               // C-RAM�o���N��
int RAMSize;                // C-RAM���e��
WORD IEep[64];              // ����EEPROM
struct EEPROM sIEep;        // EEPROM�ǂݏ����p�\���́i�����j
struct EEPROM sCEep;        // EEPROM�ǂݏ����p�\���́i�J�[�g���b�W�j
int CartKind;               // �Z�[�u�������̎�ށiCK_EEP = EEPROM�j

static int ButtonState = 0x0000;    // Button state: B.A.START.OPTION.X4.X3.X2.X1.Y4.Y3.Y2.Y1
static int HVMode;
static WORD HTimer;
static WORD VTimer;
static int RtcCount;
static int RAMEnable;
int FrameSkip = 1;
static int GDmaExtraCycles;
static int SkipCnt = 0;
static int TblSkip[5][5] = {
    {1,1,1,1,1},
    {0,1,1,1,1},
    {0,1,0,1,1},
    {0,0,1,0,1},
    {0,0,0,0,1},
};
#ifdef BENCHMARK_LOGS
static WsCoreStats s_coreStats;
#define WS_BENCH_INC(field)       (s_coreStats.field++)
#define WS_BENCH_ADD(field, val)  (s_coreStats.field += (unsigned int)(val))
#else
#define WS_BENCH_INC(field)       ((void)0)
#define WS_BENCH_ADD(field, val)  ((void)0)
#endif
#define MONO(C) (C)<<12 | (C)<<7 | (C)<<1
static WORD DefColor[] = {
    MONO(0xF), MONO(0xE), MONO(0xD), MONO(0xC), MONO(0xB), MONO(0xA), MONO(0x9), MONO(0x8),
    MONO(0x7), MONO(0x6), MONO(0x5), MONO(0x4), MONO(0x3), MONO(0x2), MONO(0x1), MONO(0x0)
};

static int WsGdmaSourceIsValid(DWORD source)
{
    const int page = (int)((source >> 16) & 0x0F);

    if (page == 0x01 && RAMEnable) return 0;
    if (page >= 0x02 && (HWARCH & 0x08)) return 0;
    return 1;
}

static void WsRunGdma(void)
{
    DWORD source = DMASRC & 0x0FFFFE;
    WORD dest = DMADST & 0xFFFE;
    WORD length = DMACNT & 0xFFFE;
    const int step = (IO[0x48] & 0x40) ? -2 : 2;
    WORD remaining = length;

    if (!remaining || !WsGdmaSourceIsValid(source))
    {
        IO[0x48] &= 0x7F;
        return;
    }

    while(remaining)
    {
        if (!WsGdmaSourceIsValid(source)) break;
        BYTE lo = ReadMem(source);
        BYTE hi = ReadMem(source + 1);
        WriteMem(dest, lo);
        WriteMem((WORD)(dest + 1), hi);
        source = (DWORD)((source + step) & 0x0FFFFE);
        dest = (WORD)(dest + step);
        remaining -= 2;
    }

    WORD transferred = (WORD)(length - remaining);
    if(transferred)
    {
        GDmaExtraCycles += 5 + (int)transferred;
        WS_BENCH_INC(gdmaTransfers);
        WS_BENCH_ADD(gdmaBytes, transferred);
    }
    DMACNT = remaining;
    DMASRC = source;
    DMADST = dest;
    IO[0x48] &= 0x7F;
}

void WsAllocateBuffers(void)
{
    // ROMMap RAMMap IO - 256 BYTES
    ROMMap = (BYTE**)malloc(0x100u * sizeof(BYTE*));
    RAMMap = (BYTE**)malloc(0x100u * sizeof(BYTE*));
    IO = (BYTE*)malloc(0x100u);
    memset(ROMMap, 0, 0x100u * sizeof(BYTE*));
    memset(RAMMap, 0, 0x100u * sizeof(BYTE*));
    memset(IO, 0, 0x100u);
    
    // 64 KB IRAM
    IRAM = (BYTE*)malloc(0x10000u);
    memset(IRAM, 0, 0x10000u);
}

void  ComEEP(struct EEPROM *eeprom, WORD *cmd, WORD *data)
{
    int i, j, op, addr;
    const int tblmask[16][5]=
    {
        {0x0000, 0, 0x0000, 0, 0x0000}, // dummy
        {0x0000, 0, 0x0000, 0, 0x0000},
        {0x0000, 0, 0x0000, 0, 0x0000},
        {0x0000, 0, 0x0000, 0, 0x0000},
        {0x000C, 2, 0x0003, 0, 0x0003},
        {0x0018, 3, 0x0006, 1, 0x0007},
        {0x0030, 4, 0x000C, 2, 0x000F},
        {0x0060, 5, 0x0018, 3, 0x001F},
        {0x00C0, 6, 0x0030, 4, 0x003F}, // 1Kbits IEEPROM
        {0x0180, 7, 0x0060, 5, 0x007F},
        {0x0300, 8, 0x00C0, 6, 0x00FF},
        {0x0600, 9, 0x0180, 7, 0x01FF},
        {0x0C00, 10, 0x0300, 8, 0x03FF}, // 16Kbits
        {0x1800, 11, 0x0600, 9, 0x07FF},
        {0x3000, 12, 0x0C00, 10, 0x0FFF},
        {0x6000, 13, 0x1800, 11, 0x1FFF},
    };
    if(eeprom->data == NULL)
    {
        return;
    }
    for(i = 15, j = 0x8000; i >= 0; i--, j >>= 1)
    {
        if(*cmd & j)
        {
            break;
        }
    }
    op = (*cmd & tblmask[i][0]) >> tblmask[i][1];
    switch(op)
    {
    case 0:
        addr = (*cmd & tblmask[i][2]) >> tblmask[i][3];
        switch(addr)
        {
        case 0: // �����݋֎~
            eeprom->we = 0;
            break;
        case 1: // �S�A�h���X��������
            for(j = tblmask[i][4]; j >= 0; j--)
            {
                eeprom->data[j] = *data;
            }
            break;
        case 2: // �`�b�v����
            if(eeprom->we)
            {
                memset(eeprom->data, 0xFF, sizeof(eeprom->data)*2);
            }
            break;
        case 3: // �������݋���
            eeprom->we = 1;
            break;
        }
        *data = 0;
        break;
    case 1: // ��������
        if(eeprom->we)
        {
            addr = *cmd & tblmask[i][4];
            eeprom->data[addr] = *data;
            if (ROMBanks == 1 && addr == 0x3A) // �A�i�U�w�u������������ł�
            {
                WsSplash();
                Run = 0; // �p�[�\�i���f�[�^�Ō�̏������݂Ȃ̂ŏI��
            }
        }
        *data = 0;
        break;
    case 2: // �ǂݏo��
        addr = *cmd & tblmask[i][4];
        *data = eeprom->data[addr];
        break;
    case 3: // ����
        if(eeprom->we)
        {
            addr = *cmd & tblmask[i][4];
            eeprom->data[addr] = 0xFFFF;
        }
        *data = 0;
        break;
    default: break;
    }
}

inline BYTE ReadMem(DWORD A)
{
    return Page[(A >> 16) & 0xF][A & 0xFFFF];
}

void WriteMem(DWORD A, BYTE V)
{
    (*WriteMemFnTable[(A >> 16) & 0x0F])(A, V);
}

static void WriteRom(DWORD A, BYTE V)
{
	//ErrorMsg(ERR_WRITE_ROM);
}

static void WriteIRam(DWORD A, BYTE V)
{
    IRAM[A & 0xFFFF] = V;
    if((A & 0xFE00) == 0xFE00)
    {
        SetPalette(A);
    }
    if(!((A - WaveMap) & 0xFFC0))
    {
        apuSetPData(A & 0x003F, V);
    }
}

#define	FLASH_CMD_ADDR1			0x0AAA
#define	FLASH_CMD_ADDR2			0x0555
#define	FLASH_CMD_DATA1			0xAA
#define	FLASH_CMD_DATA2			0x55
#define	FLASH_CMD_RESET			0xF0
#define	FLASH_CMD_ERASE			0x80
#define	FLASH_CMD_ERASE_CHIP	0x10
#define	FLASH_CMD_ERASE_SECT	0x30
#define	FLASH_CMD_CONTINUE_SET	0x20
#define	FLASH_CMD_CONTINUE_RES1	0x90
#define	FLASH_CMD_CONTINUE_RES2	0xF0
#define	FLASH_CMD_CONTINUE_RES3	0x00
#define	FLASH_CMD_WRITE			0xA0
static void  WriteCRam(DWORD A, BYTE V)
{   
    if (RAMBanks <= 0 || RAMSize <= 0) return;

    // IMPORTANT, this will prevent writes to SRAM when not mapped
    // Can't handle sram > 32KB, prevent writes
    // We cant allocate more than 32 KB for SRAM, no more mem
    if (Page[1] != RAMMap[0]) return;
	int offset = A & 0xFFFF;
    if (offset >= MAX_SRAM_ALLOCATED) return;

    static int flashCommand1 = 0;
	static int flashCommand2 = 0;
	static int flashWriteSet = 0;
	static int flashWriteOne = 0;
	static int flashWriteReset = 0;
	static int flashWriteEnable = 0;


    if (offset >= RAMSize)
    {
        //ErrorMsg(ERR_OVER_RAMSIZE);
    }
	// WonderWitch
	// FLASH ROM command sequence
	if (flashCommand2)
	{
		if (offset == FLASH_CMD_ADDR1)
		{
			switch (V) {
			case FLASH_CMD_CONTINUE_SET:
				flashWriteSet   = 1;
				flashWriteReset = 0;
				break;
			case FLASH_CMD_WRITE:
				flashWriteOne = 1;
				break;
			case FLASH_CMD_RESET:
				break;
			case FLASH_CMD_ERASE:
				break;
			case FLASH_CMD_ERASE_CHIP:
				break;
			case FLASH_CMD_ERASE_SECT:
				break;
			}
		}
		flashCommand2 = 0;
	}
	else if (flashCommand1)
	{
		if (offset == FLASH_CMD_ADDR2 && V == FLASH_CMD_DATA2)
		{
			flashCommand2 = 1;
		}
		flashCommand1 = 0;
	}
	else if (offset == FLASH_CMD_ADDR1 && V == FLASH_CMD_DATA1)
	{
		flashCommand1 = 1;
	}
	if (RAMSize != 0x40000 || BNK1SEL < 8)
	{
		// normal sram
		Page[1][offset] = V;
	}
	else if (BNK1SEL >= 8 && BNK1SEL < 15)
	{
		// FLASH ROM use SRAM bank(port 0xC1:8-14)(0xC1:15 0xF0000-0xFFFFF are write protected)
		if (flashWriteEnable || flashWriteOne)
		{
			Page[BNK1SEL][offset] = V;
			flashWriteEnable = 0;
			flashWriteOne = 0;
		}
		else if (flashWriteSet)
		{
			switch (V)
			{
			case FLASH_CMD_WRITE:
				flashWriteEnable = 1;
				flashWriteReset = 0;
				break;
			case FLASH_CMD_CONTINUE_RES1:
				flashWriteReset = 1;
				break;
			case FLASH_CMD_CONTINUE_RES2:
			case FLASH_CMD_CONTINUE_RES3:
				if (flashWriteReset)
				{
					flashWriteSet = 0;
					flashWriteReset = 0;
				}
				break;
			default:
				flashWriteReset = 0;
			}
		}
	}
}

void  WriteIO(DWORD A, BYTE V)
{
    int i, j, k;

    if(A >= 0x100)
    {
        return;
    }
    switch(A)
    {
    case 0x07:
        Scr1TMap = IRAM + ((V & 0x0F) << 11);
        Scr2TMap = IRAM + ((V & 0xF0) << 7);
        break;
    case 0x15:
        if (V & 0x01)
        {
            Segment[8] = 1;
			RenderSleep();
        }
		else
		{
            Segment[8] = 0;
		}
        if (V & 0x02)
        {
            SetHVMode(1);
            Segment[4] = 1;
        }
        else
        {
            Segment[4] = 0;
        }
        if (V & 0x04)
        {
            SetHVMode(0);
            Segment[3] = 1;
        }
        else
        {
            Segment[3] = 0;
        }
        if (V & 0x08)
        {
            Segment[2] = 1;
        }
		else
		{
            Segment[2] = 0;
		}
        if (V & 0x10)
        {
            Segment[1] = 1;
        }
		else
		{
            Segment[1] = 0;
		}
        if (V & 0x20)
        {
            Segment[0] = 1;
        }
		else
		{
            Segment[0] = 0;
		}
        break;
    case 0x1C:
    case 0x1D:
    case 0x1E:
    case 0x1F:
        if(COLCTL & 0x80) break;
        i = (A - 0x1C) << 1;
        MonoColor[i] = DefColor[V & 0x0F];
        MonoColor[i + 1] = DefColor[(V & 0xF0) >> 4];
        for(k = 0x20; k < 0x40; k++)
        {
            i = (k & 0x1E) >> 1;
            j = 0;
            if(k & 0x01) j = 2;
            Palette[i][j] = MonoColor[IO[k] & 0x07];
            Palette[i][j + 1] = MonoColor[(IO[k] >> 4) & 0x07];
        }
        break;
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2A:
    case 0x2B:
    case 0x2C:
    case 0x2D:
    case 0x2E:
    case 0x2F:
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
    case 0x3A:
    case 0x3B:
    case 0x3C:
    case 0x3D:
    case 0x3E:
    case 0x3F:
        if (COLCTL & 0x80) break;
        i = (A & 0x1E) >> 1;
        j = 0;
        if (A & 0x01) j = 2;
        Palette[i][j] = MonoColor[V & 0x07];
        Palette[i][j + 1] = MonoColor[(V >> 4) & 0x07];
        break;
    case 0x48:
        if(V & 0x80)
        {
            IO[A] = V;
            WsRunGdma();
            return;
        }
        break;
    case 0x80:
    case 0x81:
        IO[A] = V;
        Ch[0].freq = *(unsigned short*)(IO + 0x80);
        return;
    case 0x82:
    case 0x83:
        IO[A] = V;
        Ch[1].freq = *(unsigned short*)(IO + 0x82);
        return;
    case 0x84:
    case 0x85:
        IO[A] = V;
        Ch[2].freq = *(unsigned short*)(IO + 0x84);
        return;
    case 0x86:
    case 0x87:
        IO[A] = V;
        Ch[3].freq = *(unsigned short*)(IO + 0x86);
        return;
    case 0x88:
        Ch[0].volL = (V >> 4) & 0x0F;
        Ch[0].volR = V & 0x0F;
        break;
    case 0x89:
        Ch[1].volL = (V >> 4) & 0x0F;
        Ch[1].volR = V & 0x0F;
        break;
    case 0x8A:
        Ch[2].volL = (V >> 4) & 0x0F;
        Ch[2].volR = V & 0x0F;
        break;
    case 0x8B:
        Ch[3].volL = (V >> 4) & 0x0F;
        Ch[3].volR = V & 0x0F;
        break;
    case 0x8C:
        Swp.step = (signed char)V;
        break;
    case 0x8D:
        Swp.time = (V + 1) << 5;
        break;
    case 0x8E:
        Noise.pattern = V & 0x07;
        break;
    case 0x8F:
        WaveMap = V << 6;
        for (i = 0; i < 64; i++) {
            apuSetPData(WaveMap + i, IRAM[WaveMap + i]);
        }
        break;
    case 0x90:
        Ch[0].on = V & 0x01;
        Ch[1].on = V & 0x02;
        Ch[2].on = V & 0x04;
        Ch[3].on = V & 0x08;
        VoiceOn   = V & 0x20;
        Swp.on    = V & 0x40;
        Noise.on  = V & 0x80;
        break;
    case 0x91:
        V |= 0x80; // �w�b�h�z���͏�ɃI��
        break;
    case 0xA0:
        V=0x02;
        break;
    case 0xA2:
        if(V & 0x01)
        {
            HTimer = HPRE;
        }
        else
        {
            HTimer = 0;
        }
        if(V & 0x04)
        {
            VTimer = VPRE;
        }
        else
        {
            VTimer = 0;
        }
        break;
    case 0xA4:
    case 0xA5:
        IO[A] = V;
        IO[A + 4] = V;
        HTimer = HPRE;
        return;
    case 0xA6:
    case 0xA7:
        IO[A] = V;
        IO[A + 4] = V;
        VTimer = VPRE;
        return;
    case 0xB3:
        if(V & 0x20)
        {
            V &= 0xDF;
        }
        V |= 0x04;
        break;
    case 0xB5:
        KEYCTL = (BYTE)(V & 0xF0);
        if(KEYCTL & 0x40) KEYCTL |= (BYTE)((ButtonState >> 8) & 0x0F);
        if(KEYCTL & 0x20) KEYCTL |= (BYTE)((ButtonState >> 4) & 0x0F);
        if(KEYCTL & 0x10) KEYCTL |= (BYTE)(ButtonState & 0x0F);
        return;
    case 0xB6:
        IRQACK &= (BYTE)~V;
        return;
    case 0xBE:
        ComEEP(&sIEep, (WORD*)(IO + 0xBC), (WORD*)(IO + 0xBA));
        V >>= 4;
        break;
    case 0xC0:
        if(nec_get_reg(NEC_CS) >= 0x4000)
        {
            nec_execute(1);
        }
        j = (V << 4) & 0xF0;
        Page[0x4] = ROMMap[0x4 | j];
        Page[0x5] = ROMMap[0x5 | j];
        Page[0x6] = ROMMap[0x6 | j];
        Page[0x7] = ROMMap[0x7 | j];
        Page[0x8] = ROMMap[0x8 | j];
        Page[0x9] = ROMMap[0x9 | j];
        Page[0xA] = ROMMap[0xA | j];
        Page[0xB] = ROMMap[0xB | j];
        Page[0xC] = ROMMap[0xC | j];
        Page[0xD] = ROMMap[0xD | j];
        Page[0xE] = ROMMap[0xE | j];
        Page[0xF] = ROMMap[0xF | j];
        break;
    case 0xC1:
        if (CartKind & CK_EEP) { Page[1] = MemDummy; RAMEnable = 0; IO[A]=V; return; }
        RAMEnable = 0;

        // IMPORTANT; only Bank 0 can be mapped and allocated; no heap space for more
        if (V != 0) {
            Page[1] = MemDummy;       // banques != 0 ignored
        } else {
            // Bank 0 only
            if (RAMBanks >= 1 && RAMMap[0]) {
                Page[1] = RAMMap[0];
                RAMEnable = 1;
            } else {
                Page[1] = MemDummy;
            }
        }
    break;
    case 0xC2:
        Page[2] = ROMMap[V];
        break;
    case 0xC3:
        Page[3] = ROMMap[V];
        break;
    case 0xC8:
        ComEEP(&sCEep, (WORD*)(IO + 0xC6), (WORD*)(IO + 0xC4));
        if(V & 0x10)
        {
            V >>= 4;
        }
        if(V & 0x20)
        {
            V >>= 4;
        }
        if(V & 0x40)
        {
            V >>= 5;
        }
        break;
    case 0xCA: // RTC Command
        if (V == 0x15)
        {
            RtcCount = 0;
        }
        break;
    case 0xCB: //RTC DATA
        break;
    default:
        break;
    }
    IO[A] = V;
}

#define  BCD(value) ((value / 10) << 4) | (value % 10)
BYTE ReadIO(DWORD A)
{
    switch(A)
    {
    case 0xCA:
        return IO[0xCA] | 0x80;
    case 0xCB:
        if (IO[0xCA] == 0x15)  // get time command
        {
            BYTE year, mon, mday, wday, hour, min, sec, j;
            struct tm *newtime;
            time_t long_time;

			long_time = time(NULL);
            //time(&long_time);
            newtime = localtime(&long_time);
            switch(RtcCount)
            {
            case 0:
                RtcCount++;
                year = newtime->tm_year;
                year %= 100;
                return BCD(year);
            case 1:
                RtcCount++;
                mon = newtime->tm_mon;
                mon++;
                return BCD(mon);
            case 2:
                RtcCount++;
                mday = newtime->tm_mday;
                return BCD(mday);
            case 3:
                RtcCount++;
                wday = newtime->tm_wday;
                return BCD(wday);
            case 4:
                RtcCount++;
                hour = newtime->tm_hour;
                j = BCD(hour);
                if (hour > 11)
                    j |= 0x80;
                return j;
            case 5:
                RtcCount++;
                min = newtime->tm_min;
                return BCD(min);
            case 6:
                RtcCount = 0;
                sec = newtime->tm_sec;
                return BCD(sec);
            }
            return 0;
        }
        else {
            // set ack
            return (IO[0xCB] | 0x80);
        }
    }
    return IO[A];
}

WriteMemFn WriteMemFnTable[16]= {
    WriteIRam,
    WriteCRam,
    WriteRom,
    WriteRom,
    WriteRom,
    WriteRom,
    WriteRom,
    WriteRom,
    WriteRom,
    WriteRom,
    WriteRom,
    WriteRom,
    WriteRom,
    WriteRom,
    WriteRom,
    WriteRom,
};

void WsReset (void)
{
    int i, j;

    Page[0x0] = IRAM;
    sIEep.data = IEep;
    sIEep.we = 0;
    if(CartKind & CK_EEP)
    {
        Page[0x1] = MemDummy;
        sCEep.data = (WORD*)(RAMMap[0x00]);
        sCEep.we = 0;
    }
    else
    {
        Page[0x1] = ROMMap[0x00];
        sCEep.data = NULL;
        sCEep.we = 0;
    }
    Page[0xF] = ROMMap[0xFF];
    i = (SPRTAB & 0x1F) << 9;
    i += SPRBGN << 2;
    j = SPRCNT << 2;
    memcpy(SprTMap, IRAM + i, j);
    SprTTMap = SprTMap;
    SprETMap = SprTMap + j - 4;
    WriteIO(0x07, 0x00);
    WriteIO(0x14, 0x01);
    WriteIO(0x1C, 0x99);
    WriteIO(0x1D, 0xFD);
    WriteIO(0x1E, 0xB7);
    WriteIO(0x1F, 0xDF);
    WriteIO(0x20, 0x30);
    WriteIO(0x21, 0x57);
    WriteIO(0x22, 0x75);
    WriteIO(0x23, 0x76);
    WriteIO(0x24, 0x15);
    WriteIO(0x25, 0x73);
    WriteIO(0x26, 0x77);
    WriteIO(0x27, 0x77);
    WriteIO(0x28, 0x20);
    WriteIO(0x29, 0x75);
    WriteIO(0x2A, 0x50);
    WriteIO(0x2B, 0x36);
    WriteIO(0x2C, 0x70);
    WriteIO(0x2D, 0x67);
    WriteIO(0x2E, 0x50);
    WriteIO(0x2F, 0x77);
    WriteIO(0x30, 0x57);
    WriteIO(0x31, 0x54);
    WriteIO(0x32, 0x75);
    WriteIO(0x33, 0x77);
    WriteIO(0x34, 0x75);
    WriteIO(0x35, 0x17);
    WriteIO(0x36, 0x37);
    WriteIO(0x37, 0x73);
    WriteIO(0x38, 0x50);
    WriteIO(0x39, 0x57);
    WriteIO(0x3A, 0x60);
    WriteIO(0x3B, 0x77);
    WriteIO(0x3C, 0x70);
    WriteIO(0x3D, 0x77);
    WriteIO(0x3E, 0x10);
    WriteIO(0x3F, 0x73);
    WriteIO(0x01, 0x00);
    WriteIO(0x8F, 0x03);
    WriteIO(0x91, 0x80);
    WriteIO(0xA0, 0x02);
    WriteIO(0xB3, 0x04);
    WriteIO(0xBA, 0x01);
    WriteIO(0xBB, 0x00);
    WriteIO(0xBC, 0x30); // ����EEPROM
    WriteIO(0xBD, 0x01); // �������݋���
    WriteIO(0xBE, 0x83);
    IO[0xC0] = 0x0F;
    j = 0xF0;
    Page[0x4] = ROMMap[0x4 | j];
    Page[0x5] = ROMMap[0x5 | j];
    Page[0x6] = ROMMap[0x6 | j];
    Page[0x7] = ROMMap[0x7 | j];
    Page[0x8] = ROMMap[0x8 | j];
    Page[0x9] = ROMMap[0x9 | j];
    Page[0xA] = ROMMap[0xA | j];
    Page[0xB] = ROMMap[0xB | j];
    Page[0xC] = ROMMap[0xC | j];
    Page[0xD] = ROMMap[0xD | j];
    Page[0xE] = ROMMap[0xE | j];
    Page[0xF] = ROMMap[0xF | j];
    WriteIO(0xC2, 0xFF);
    WriteIO(0xC3, 0xFF);
    IRAM[0x75AC]=0x41;
    IRAM[0x75AD]=0x5F;
    IRAM[0x75AE]=0x43;
    IRAM[0x75AF]=0x31;
    IRAM[0x75B0]=0x6E;
    IRAM[0x75B1]=0x5F;
    IRAM[0x75B2]=0x63;
    IRAM[0x75B3]=0x31;
    apuWaveClear();
    GDmaExtraCycles = 0;
    ButtonState = 0x0000;
	for (i = 0; i < 11; i++)
	{
		Segment[i] = 0;
	}
    nec_reset(NULL);
    nec_set_reg(NEC_SP, 0x2000);
}

void WsRomPatch(BYTE *buf)
{
    if((buf[0] == 0x01) && (buf[1] == 0x01) && (buf[2] == 0x16)) // SWJ-BANC16 STAR HEARTS
    {
        RAMBanks = 1;
        RAMSize = 0x8000;
        CartKind = 0;
    }
    if((buf[0] == 0x01) && (buf[1] == 0x00) && (buf[2] == 0x2C || buf[2] == 0x2F)) // SWJ-BAN02C,02F �f�W�^���p�[�g�i�[
    {
        RAMBanks = 1;
        RAMSize = 0x8000;
        CartKind = 0;
    }
    if((buf[0] == 0x01) && (buf[1] == 0x01) && (buf[2] == 0x38)) // SWJ-BANC38 NARUTO �؃m�t�E�@��
    {
        RAMBanks = 1;
        RAMSize = 0x10000;
        CartKind = 0;
    }
}

int Interrupt(void)
{
    static int LCount=0, Joyz=0x0000;
    int i, j;

    if(++LCount>=8) // 8���1Hblank����
    {
        LCount=0;
    }
    switch(LCount)
    {
        case 0:
            if(RSTRL == 144)
            {
                DWORD VCounter;

                ButtonState = WsInputGetState(HVMode);
                if((ButtonState ^ Joyz) & Joyz)
                {
                    if(IRQENA & KEY_IFLAG)
                    {
                        IRQACK |= KEY_IFLAG;
                        WS_BENCH_INC(keyIrqs);
                    }
                }
                Joyz = ButtonState;
                // Vblank�J�E���g�A�b�v
                VCounter = VCNTH << 16 | VCNTL;
                VCounter++;
                VCNTL = (WORD)VCounter;
                VCNTH = (WORD)(VCounter >> 16);
            }
            break;
        case 2:
            // Hblank����1�T���v���Z�b�g���邱�Ƃ�12KHz��wave�f�[�^���o����
			apuWaveSet();
            WS_BENCH_INC(apuTicks);
			//NCSR = apuShiftReg();
            break;
        case 4:
            if(RSTRL == 140)
            {
                i = (SPRTAB & 0x1F) << 9;
                i += SPRBGN << 2;
                j = SPRCNT << 2;
                memcpy(SprTMap, IRAM + i, j);
                SprTTMap = SprTMap;
                SprETMap= SprTMap + j - 4;
            }

            if(LCDSLP & 0x01)
            {
			    if(RSTRL == 0)
                {
                    SkipCnt--;
                    if(SkipCnt < 0)
                    {
                        SkipCnt = 4;
                    }
                }
                int frameSkip = FrameSkip;
                if(frameSkip < 0) frameSkip = 0;
                if(frameSkip > 4) frameSkip = 4;
				if(TblSkip[frameSkip][SkipCnt])
                {
                    if(RSTRL < 144)
                    {
                        RefreshLine(RSTRL);
                        WS_BENCH_INC(refreshLines);
                    }
                    if(RSTRL == 144)
                    {
                        ws_graphics_paint();
                        WS_BENCH_INC(paintRequests);
                    }
                }
            }
            break;
        case 6:
            if((TIMCTL & 0x01) && HTimer)
            {
                HTimer--;
                if(!HTimer)
                {
                    if(TIMCTL & 0x02)
                    {
                        HTimer = HPRE;
                    }
                    if(IRQENA & HTM_IFLAG)
                    {
                        IRQACK |= HTM_IFLAG;
                        WS_BENCH_INC(htimerIrqs);
                    }
                }
            }
            else if(HPRE == 1)
            {
                if(IRQENA & HTM_IFLAG)
                {
                    IRQACK |= HTM_IFLAG;
                    WS_BENCH_INC(htimerIrqs);
                }
            }
            if((IRQENA & VBB_IFLAG) && (RSTRL == 144))
            {
                IRQACK |= VBB_IFLAG;
                WS_BENCH_INC(vblankIrqs);
            }
            if((TIMCTL & 0x04) && (RSTRL == 144) && VTimer)
            {
                VTimer--;
                if(!VTimer)
                {
                    if(TIMCTL & 0x08)
                    {
                        VTimer = VPRE;
                    }
                    if(IRQENA & VTM_IFLAG)
                    {
                        IRQACK |= VTM_IFLAG;
                        WS_BENCH_INC(vtimerIrqs);
                    }
                }
            }
            if((IRQENA & RST_IFLAG) && (RSTRL == RSTRLC))
            {
                IRQACK |= RST_IFLAG;
                WS_BENCH_INC(lineIrqs);
            }
            break;
        case 7:
            RSTRL++;
            if(RSTRL >= 159)
            {
                RSTRL = 0;
            }
            // Hblank�J�E���g�A�b�v
            HCNT++;
            break;
        default:
            break;
    }
    return IRQACK;
}

int WsRun(void)
{
    static int period = IPeriod;
    int i, cycle, iack, inum;

    for(i = 0; i < 159 * 8; i++) // 1/75s
    {
        cycle = nec_execute(period);
        if(GDmaExtraCycles)
        {
            cycle += GDmaExtraCycles;
            GDmaExtraCycles = 0;
        }
        period += IPeriod - cycle;
        if(Interrupt())
        {
            iack = IRQACK;
            for(inum = 7; inum >= 0; inum--)
            {
                if(iack & 0x80)
                {
                    break;
                }
                iack <<= 1;
            }
            nec_int((inum + IRQBSE) << 2);
        }
    }
    WS_BENCH_INC(frames);
    WS_BENCH_ADD(cpuSteps, 159 * 8);
    return 0;
}

#ifdef BENCHMARK_LOGS
void WsGetAndResetStats(WsCoreStats* out)
{
    if(out)
    {
        *out = s_coreStats;
        out->frameSkip = FrameSkip;
    }
    memset(&s_coreStats, 0, sizeof(s_coreStats));
}
#endif

#define POS_X (88)
#define POS_Y (32)
#define NAME_Y (96)
void WsSplash(void)
{
    #ifndef WS_NO_SPLASH_SCREEN

    int x, y, i, len, pos, n;
    WORD* p;
    unsigned char* name = (unsigned char*)(IEep + 0x30);
	unsigned char* pdataFontArray = pdata + 169 * 16 + 13;
	unsigned char* pdataFont = pdata + 175 * 16 + 5;

    // �w�i�����ŃN���A
    p = FrameBuffer;
    for (y = 0; y < LCD_MAIN_H; y++)
    {
        for (x = 0; x < LCD_MAIN_W; x++)
        {
            *p++ = 0x0000;
        }
        p += SCREEN_WIDTH - LCD_MAIN_W;
    }
    ws_graphics_paint();
    Sleep(30);
    apuStartupSound();
    Sleep(25);
	Segment[3] = 1;
	Segment[5] = 1;
	Segment[9] = 1;
	Segment[10] = 1;
    for (i = 0; i < 6; i++)
    {
        WORD color[6] = {0xF800, 0xFC00, 0xFFE0, 0x03E0, 0x03FF, 0x001F};
        // B,A,N,D,A,I,��1�����\��
        for (y = bandaiRect[i].top; y <= bandaiRect[i].bottom; y++)
        {
            p = FrameBuffer + SCREEN_WIDTH * (POS_Y + y) + POS_X;
            for (x = bandaiRect[i].left; x <= bandaiRect[i].right; x++)
            {
                if (bandai[y][x] == 0xFFFF)
                {
                    p[x] = color[i];
                }
            }
        }
        ws_graphics_paint();
        Sleep(10);
        // ��������
        p = FrameBuffer;
        for (y = 0; y < LCD_MAIN_H; y++)
        {
            for (x = 0; x < LCD_MAIN_W; x++)
            {
                *p++ = 0x0000;
            }
            p += SCREEN_WIDTH - LCD_MAIN_W;
        }
    }
    ws_graphics_paint();
    Sleep(20);
    // �w�i�𔒂ŃN���A
    p = FrameBuffer;
    for (y = 0; y < LCD_MAIN_H; y++)
    {
        for (x = 0; x < LCD_MAIN_W; x++)
        {
            *p++ = 0xFFFF;
        }
        p += SCREEN_WIDTH - LCD_MAIN_W;
    }
    // BANDAI���S�\��
    p = FrameBuffer + SCREEN_WIDTH * POS_Y + POS_X;
    for (y = 0; y < BANDAI_Y; y++)
    {
        for (x = 0; x < BANDAI_X; x++)
        {
            *p++ = bandai[y][x];
        }
        p += (SCREEN_WIDTH - BANDAI_X);
    }
    ws_graphics_paint();
    // ���L�҂̖��O�\��
    for (len = 15; name[len] == 0 && len > 0; len--);
    pos = 112 - len * 4;
    for (n = 0; n <= len; n++)
    {
        for (i = 0; i <= len; i++)
        {
            BYTE ar = pdataFontArray[name[i]];
            BYTE* ch = pdataFont + ar * 8;
            for (y = 0; y < 8; y++)
            {
                BYTE font = *ch++;
                p = FrameBuffer + SCREEN_WIDTH * (NAME_Y + y) + pos + (i * 8);
                for (x = 0; x < 8; x++)
                {
                    if (name[i] && (font & 0x80)) // �X�y�[�X�t�H���g�́��ɂȂ��Ă�̂ŃX�L�b�v
                    {
                        if (n == i)
                        {
                            *p = 0xFFFF;
                        }
                        else
                        {
                            *p = 0x0000;
                        }
                    }
                    p++;
                    font <<= 1;
                }
            }
        }
        ws_graphics_paint();
        Sleep(3);
    }
    for (i = 0; i <= len; i++)
    {
        BYTE ar = pdataFontArray[name[i]];
        BYTE* ch = pdataFont + ar * 8;
        for (y = 0; y < 8; y++)
        {
            BYTE font = *ch++;
            p = FrameBuffer + SCREEN_WIDTH * (NAME_Y + y) + pos + (i * 8);
            for (x = 0; x < 8; x++)
            {
                if (name[i] && (font & 0x80))
                {
                    if (n == i)
                    {
                        *p = 0xFFFF;
                    }
                    else
                    {
                        *p = 0x0000;
                    }
                }
                p++;
                font <<= 1;
            }
        }
    }
	Segment[3] = 0;
	Segment[5] = 0;
	Segment[9] = 0;
    ws_graphics_paint();
	Sleep(100);
    #endif
}

void WsCpyPdata(BYTE* dst)
{
	memcpy(dst, pdata, size_pdata);
}

void Sleep(int ticks)
{
	unsigned long ini=SDL_UXTimerRead();
	while (SDL_UXTimerRead()-ini<(ticks*10000));
}

void SetHVMode(int Mode)
{
    HVMode = Mode;
}

void WsInit(void) {
    init_ModRM_tables();
    WsAllocateBuffers();
	apuInit();
	WsLoadIEep();
	WsSplash();
}

void WsDeInit(void) {
	WsSaveIEep();
	WsRelease();
	apuEnd();
}
