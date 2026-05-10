/*
$Date: 2009-10-30 05:26:46 +0100 (ven., 30 oct. 2009) $
$Rev: 71 $
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "WSHard.h"
#include "WS.h"
#include "WSFileio.h"
#include "WSRender.h"
#include "cpu/necintrf.h"

#define ERR_MALLOC					0
#define ERR_OVER_RAMSIZE			0
#define ERR_WRITE_ROM				0
#define ERR_WAVEOUT_OPEN			0
#define ERR_WAVEOUT_PREPAREHEADER	0
#define ERR_FOPEN					0
#define ERR_FREAD_ROMINFO			0
#define ERR_ILLEGAL_ROMSIZE			0
#define ERR_FREAD_ROM				0
#define ERR_CHECKSUM				0
#define ERR_FREAD_SAVE				0

static char *SaveDir = "RAM";
static char *StateDir = "STATE";
static char SaveName[1];   // unused
static char StateName[1];
static char* IEepPath = "./oswan-od.dat";
static BYTE g_dummy_ff = 0xFF;

#if 0
void WsSetDir(char* path)
{
	char temp[512];
	char file[512];
	char* p;

	SaveName[0] = 0;
	StateName[0] = 0;
	if (path == NULL)
	{
		return;
	}
	strcpy(temp, path);
	p = strrchr(temp, '\\');
	if (p == NULL)
	{
		return;
	}
	strcpy(file, p++);
	*p = 0;
	p = strrchr(file, '.');
	if (p == NULL)
	{
		return;
	}
	*p = 0;
	strcpy(SaveName, temp);
	strcat(SaveName, SaveDir);
	strcat(SaveName, file);
	strcat(SaveName, ".sav");
	strcpy(StateName, temp);
	strcat(StateName, StateDir);
	strcat(StateName, file);
}
#endif

int WsSetPdata(void)
{
    ROMBanks = 1;
    RAMBanks = 0;
    RAMSize = 0;
    CartKind = 0;
    if ((ROMMap[0xFF] = (BYTE*)malloc(0x10000)) == NULL)
    {
		fprintf(stderr,"WsSetPdata\n");

        //ErrorMsg(ERR_MALLOC);
        return 0;
    }
    WsCpyPdata(ROMMap[0xFF] + 0xF080);
    WsReset();
    SetHVMode(0);
    return 1;
}

int WsCreate(char *CartName)
{
    int Checksum, i, j;
    FILE* fp;
    BYTE buf[16];

    for (i = 0; i < 256; i++)
    {
        ROMMap[i] = MemDummy;
        RAMMap[i] = MemDummy;
    }
    memset(IRAM, 0, sizeof(IRAM));
    memset(MemDummy, 0xA0, sizeof(MemDummy));
    memset(IO, 0, sizeof(IO));
    if (CartName == NULL)
    {
        return WsSetPdata();
    }
    if ((fp = fopen(CartName, "rb")) == NULL)
    {
        //ErrorMsg(ERR_FOPEN);
		fprintf(stderr,"ERR_FOPEN");
        return ERR_FOPEN;
    }
    fseek(fp, -10, SEEK_END);
    if (fread(buf, 1, 10, fp) != 10)
    {
        //ErrorMsg(ERR_FREAD_ROMINFO);
		fprintf(stderr,"ERR_FREAD_ROMINFO");
        return ERR_FREAD_ROMINFO;
    }
    switch (buf[4])
    {
    case 1:
        ROMBanks = 4;
        break;
    case 2:
        ROMBanks = 8;
        break;
    case 3:
        ROMBanks = 16;
        break;
    case 4:
        ROMBanks = 32;
        break;
    case 5:
        ROMBanks = 48;
        break;
    case 6:
        ROMBanks = 64;
        break;
    case 7:
        ROMBanks = 96;
        break;
    case 8:
        ROMBanks = 128;
        break;
    case 9:
        ROMBanks = 256;
        break;
    default:
        ROMBanks = 0;
        break;
    }
    if (ROMBanks == 0)
    {
        //ErrorMsg(ERR_ILLEGAL_ROMSIZE);
		fprintf(stderr,"ERR_ILLEGAL_ROMSIZE\n");
        return ERR_ILLEGAL_ROMSIZE;
    }
    switch (buf[5])
    {
    case 0x01:
        RAMBanks = 1;
        RAMSize = 0x2000;
        CartKind = 0;
        break;
    case 0x02:
        RAMBanks = 1;
        RAMSize = 0x8000;
        CartKind = 0;
        break;
    case 0x03:
        RAMBanks = 2;
        RAMSize = 0x20000;
        CartKind = 0;
        break;
    case 0x04:
        RAMBanks = 4;
        RAMSize = 0x40000;
        CartKind = 0;
        break;
    case 0x10:
        RAMBanks = 1;
        RAMSize = 0x80;
        CartKind = CK_EEP;
        break;
    case 0x20:
        RAMBanks = 1;
        RAMSize = 0x800;
        CartKind = CK_EEP;
        break;
    case 0x50:
        RAMBanks = 1;
        RAMSize = 0x400;
        CartKind = CK_EEP;
        break;
    default:
        RAMBanks = 0;
        RAMSize = 0;
        CartKind = 0;
        break;
    }
    WsRomPatch(buf);
    Checksum = (int)((buf[9] << 8) + buf[8]);
    Checksum += (int)(buf[9] + buf[8]);
    for (i = ROMBanks - 1; i >= 0; i--)
    {
        fseek(fp, (ROMBanks - i) * -0x10000, 2);
        if ((ROMMap[0x100 - ROMBanks + i] = (BYTE*)malloc(0x10000)) != NULL)
        {
            if (fread(ROMMap[0x100 - ROMBanks + i], 1, 0x10000, fp) == 0x10000)
            {
                for (j = 0; j < 0x10000; j++)
                {
                    Checksum -= ROMMap[0x100 - ROMBanks + i][j];
                }
            }
            else
            {
                //ErrorMsg(ERR_FREAD_ROM);
				fprintf(stderr,"ERR_FREAD_ROM\n");
                break;
            }
        }
        else
        {
            //ErrorMsg(ERR_MALLOC);
			fprintf(stderr,"ERR_MALLOC\n");
            return ERR_MALLOC;
        }
    }
    fclose(fp);
    if (i >= 0)
    {
        return 0;
    }
    if (Checksum & 0xFFFF)
    {
        //ErrorMsg(ERR_CHECKSUM);
		fprintf(stderr,"ERR_CHECKSUM\n");
    }
    if (RAMBanks)
    {
        for (i = 0; i < RAMBanks; i++)
        {
            if ((RAMMap[i] = (BYTE*)malloc(0x10000)) != NULL)
            {
                memset(RAMMap[i], 0x00, 0x10000);
            }
            else
            {
                //ErrorMsg(ERR_MALLOC);
				fprintf(stderr,"ERR_MALLOC 1\n");
				return ERR_MALLOC;
            }
        }
    }
    if (RAMSize)
    {
        if ((fp = fopen(SaveName, "rb")) != NULL)
        {
            for (i = 0; i < RAMBanks; i++)
            {
                if (RAMSize < 0x10000)
                {
                    if (fread(RAMMap[i], 1, RAMSize, fp) != RAMSize)
                    {
                        //ErrorMsg(ERR_FREAD_SAVE);
						fprintf(stderr,"ERR_FREAD_SAVE\n");
						break;
                    }
                }
                else
                {
                    if (fread(RAMMap[i], 1, 0x10000, fp) != 0x10000)
                    {
                        //ErrorMsg(ERR_FREAD_SAVE);
						fprintf(stderr,"ERR_FREAD_SAVE 1\n");
                        break;
                    }
                }
            }
            fclose(fp);
        }
    }
    else
    {
        SaveName[0] = 0;
    }
    WsReset();
	SetHVMode(buf[6] & 1); // 0:�� 1:�c
    
	return 1;
}

void WsRelease(void)
{
    FILE* fp;
    int i;

    if (SaveName[0] != 0)
    {
        if ((fp = fopen(SaveName, "wb"))!= NULL)
        {
            for (i  =0; i < RAMBanks; i++)
            {
                if (RAMSize<0x10000)
                {
                    if (fwrite(RAMMap[i], 1, RAMSize, fp) != RAMSize)
                    {
                        break;
                    }
                }
                else
                {
                    if (fwrite(RAMMap[i], 1, 0x10000, fp)!=0x10000)
                    {
                        break;
                    }
                }
                free(RAMMap[i]);
            }
            fclose(fp);
        }
    }
    for (i = 0xFF; i >= 0; i--)
    {
        if (ROMMap[i] == MemDummy)
        {
            break;
        }
        free(ROMMap[i]);
        ROMMap[i] = MemDummy;
    }
}

int WsCreateFromMemory(const uint8_t *romData, size_t romSize)
{
    BYTE  footer[10];
    int   i;
    static BYTE dummy_byte = 0xFF;
    BYTE* dummy = &dummy_byte;

    printf("[WS] ===== WsCreateFromMemory (XIP, no-copy) =====\n");
    printf("[WS] ROM buffer: %p, size: %zu bytes\n", romData, romSize);

    if (!romData || romSize < 10) {
        printf("[WS] Invalid ROM buffer\n");
        return -1;
    }

    /* Read the last 10 bytes */
    memcpy(footer, romData + romSize - 10, 10);
    printf("[WS] Footer: ");
    for (i = 0; i < 10; i++) printf("%02X ", footer[i]);
    printf("\n");

    /* Determine ROMBanks from footer */
    switch (footer[4]) {
        case 1: ROMBanks = 4;   break;
        case 2: ROMBanks = 8;   break;
        case 3: ROMBanks = 16;  break;
        case 4: ROMBanks = 32;  break;
        case 5: ROMBanks = 48;  break;
        case 6: ROMBanks = 64;  break;
        case 7: ROMBanks = 96;  break;
        case 8: ROMBanks = 128; break;
        case 9: ROMBanks = 256; break;
        default:
            ROMBanks = (int)((romSize + 0xFFFFu) >> 16);
            if (ROMBanks <= 0)   ROMBanks = 1;
            if (ROMBanks > 256)  ROMBanks = 256;
            printf("[WS] Unknown ROM size flag %02X, fallback banks=%d\n", footer[4], ROMBanks);
            break;
    }
    printf("[WS] ROMBanks = %d\n", ROMBanks);

    /* RAM/EEP for save */
    switch (footer[5]) {
        case 0x01: RAMBanks = 1; RAMSize = 0x2000;  CartKind = 0;      break;
        case 0x02: RAMBanks = 1; RAMSize = 0x8000;  CartKind = 0;      break;
        case 0x03: RAMBanks = 2; RAMSize = 0x20000; CartKind = 0;      break;
        case 0x04: RAMBanks = 4; RAMSize = 0x40000; CartKind = 0;      break;
        case 0x10: RAMBanks = 1; RAMSize = 0x80;    CartKind = CK_EEP; break;  /* 128 B */
        case 0x20: RAMBanks = 1; RAMSize = 0x800;   CartKind = CK_EEP; break;  /* 2 KiB  */
        case 0x50: RAMBanks = 1; RAMSize = 0x400;   CartKind = CK_EEP; break;  /* 1 KiB  */
        default:   RAMBanks = 0; RAMSize = 0;       CartKind = 0;      break;
    }
    printf("[WS] RAMBanks=%d, RAMSize=0x%X, CartKind=%d\n", RAMBanks, RAMSize, CartKind);

    /* Possible ROM patches */
    WsRomPatch((BYTE*)footer);

    /* Direct ROM XIP mapping */
    for (i = 0; i < 256; ++i) ROMMap[i] = MemDummy;
    for (i = 0; i < ROMBanks; ++i) {
        int dst = 0x100 - ROMBanks + i; 
        size_t ofs = ((size_t)i) << 16;          // i * 64K
        ROMMap[dst] = (ofs < romSize) ? (BYTE*)(romData + ofs) : MemDummy;
    }

    /* Save memory. EEPROM does not map into page 1, so allocate only the
       declared EEPROM payload. SRAM keeps the existing 32 KiB page window. */
    for (i = 0; i < 256; ++i) RAMMap[i] = MemDummy;
    if (RAMBanks == 1) {
        const size_t allocSize = (CartKind & CK_EEP) ? (size_t)RAMSize : 0x8000u;
        BYTE* one = allocSize ? (BYTE*)malloc(allocSize) : NULL;
        if (!one) {
            printf("[WS] Save memory malloc %uB failed, mapping to MemDummy\n", (unsigned)allocSize);
        } else {
            memset(one, (CartKind & CK_EEP) ? 0xFF : 0x00, allocSize);
            RAMMap[0] = one; 
            printf("[WS] Save memory allocated: %u bytes (%s)\n",
                   (unsigned)allocSize,
                   (CartKind & CK_EEP) ? "EEP" : "SRAM");
        }
    } else if (RAMBanks > 1) {
        printf("[WS] Multi-bank SRAM unsupported in XIP mode (%d banks, size=0x%X)\n",
               RAMBanks, RAMSize);
    }

    WsReset();
    SetHVMode(footer[6] & 1);  /* 0: H, 1: V */

    printf("[WS] Ready (HVMode=%d). XIP ROM mapped.\n", footer[6] & 1);
    printf("[WS] ============================================\n");
    return 0;
}

void WsLoadIEep(void)
{
    FILE* fp;

    if ((fp = fopen(IEepPath, "rb")) != NULL)
    {
        fread(IEep, sizeof(WORD), 64, fp);
        fclose(fp);
    }
	else
	{
		WORD* p = IEep + 0x30;
		memset(IEep, 0xFF, 0x60);
		memset(p, 0, 0x20);
		*p++ = 0x211D;
		*p++ = 0x180B;
		*p++ = 0x1C0D;
		*p++ = 0x1D23;
		*p++ = 0x0B1E;
		*p   = 0x0016;
	}
}

void WsSaveIEep(void)
{
    FILE* fp;

    if ((fp = fopen(IEepPath, "wb")) != NULL)
    {
        fwrite(IEep, sizeof(WORD), 64, fp);
        fclose(fp);
    }
}

#define MacroLoadNecRegisterFromFile(F,R) \
		fread(&value, sizeof(unsigned int), 1, fp); \
	    nec_set_reg(R,value); 
void WsLoadState(int num)
{
    FILE* fp;
    char buf[512];
	unsigned int value;
	int i;

	sprintf(buf, "%s.%03d", StateName, num);
    if ((fp = fopen(buf, "rb")) == NULL)
    {
		return;
	}
	MacroLoadNecRegisterFromFile(fp,NEC_IP);
	MacroLoadNecRegisterFromFile(fp,NEC_AW);
	MacroLoadNecRegisterFromFile(fp,NEC_BW);
	MacroLoadNecRegisterFromFile(fp,NEC_CW);
	MacroLoadNecRegisterFromFile(fp,NEC_DW);
	MacroLoadNecRegisterFromFile(fp,NEC_CS);
	MacroLoadNecRegisterFromFile(fp,NEC_DS);
	MacroLoadNecRegisterFromFile(fp,NEC_ES);
	MacroLoadNecRegisterFromFile(fp,NEC_SS);
	MacroLoadNecRegisterFromFile(fp,NEC_IX);
	MacroLoadNecRegisterFromFile(fp,NEC_IY);
	MacroLoadNecRegisterFromFile(fp,NEC_BP);
	MacroLoadNecRegisterFromFile(fp,NEC_SP);
	MacroLoadNecRegisterFromFile(fp,NEC_FLAGS);
	MacroLoadNecRegisterFromFile(fp,NEC_VECTOR);
	MacroLoadNecRegisterFromFile(fp,NEC_PENDING);
	MacroLoadNecRegisterFromFile(fp,NEC_NMI_STATE);
	MacroLoadNecRegisterFromFile(fp,NEC_IRQ_STATE);
    fread(IRAM, sizeof(BYTE), 0x10000, fp);
    fread(IO, sizeof(BYTE), 0x100, fp);
    for (i  =0; i < RAMBanks; i++)
    {
        if (RAMSize < 0x10000)
        {
            fread(RAMMap[i], 1, RAMSize, fp);
        }
        else
        {
            fread(RAMMap[i], 1, 0x10000, fp);
        }
    }
	fread(Palette, sizeof(WORD), 16 * 16, fp);
    fclose(fp);
	WriteIO(0xC1, IO[0xC1]);
	WriteIO(0xC2, IO[0xC2]);
	WriteIO(0xC3, IO[0xC3]);
	WriteIO(0xC0, IO[0xC0]);
	for (i = 0x80; i <= 0x90; i++)
	{
		WriteIO(i, IO[i]);
	}
}

#define MacroStoreNecRegisterToFile(F,R) \
	    value = nec_get_reg(R); \
		fwrite(&value, sizeof(unsigned int), 1, fp);
void WsSaveState(int num)
{
    FILE* fp;
    char buf[512];
	unsigned int value;
	int i;

	if (StateName[0] == 0)
	{
		return;
	}
	sprintf(buf, "%s.%03d", StateName, num);
    if ((fp = fopen(buf, "wb")) == NULL)
    {
		return;
	}
	MacroStoreNecRegisterToFile(fp,NEC_IP);
	MacroStoreNecRegisterToFile(fp,NEC_AW);
	MacroStoreNecRegisterToFile(fp,NEC_BW);
	MacroStoreNecRegisterToFile(fp,NEC_CW);
	MacroStoreNecRegisterToFile(fp,NEC_DW);
	MacroStoreNecRegisterToFile(fp,NEC_CS);
	MacroStoreNecRegisterToFile(fp,NEC_DS);
	MacroStoreNecRegisterToFile(fp,NEC_ES);
	MacroStoreNecRegisterToFile(fp,NEC_SS);
	MacroStoreNecRegisterToFile(fp,NEC_IX);
	MacroStoreNecRegisterToFile(fp,NEC_IY);
	MacroStoreNecRegisterToFile(fp,NEC_BP);
	MacroStoreNecRegisterToFile(fp,NEC_SP);
	MacroStoreNecRegisterToFile(fp,NEC_FLAGS);
	MacroStoreNecRegisterToFile(fp,NEC_VECTOR);
	MacroStoreNecRegisterToFile(fp,NEC_PENDING);
	MacroStoreNecRegisterToFile(fp,NEC_NMI_STATE);
	MacroStoreNecRegisterToFile(fp,NEC_IRQ_STATE);
    fwrite(IRAM, sizeof(BYTE), 0x10000, fp);
    fwrite(IO, sizeof(BYTE), 0x100, fp);
    for (i  =0; i < RAMBanks; i++)
    {
        if (RAMSize < 0x10000)
        {
            fwrite(RAMMap[i], 1, RAMSize, fp);
        }
        else
        {
            fwrite(RAMMap[i], 1, 0x10000, fp);
        }
    }
	fwrite(Palette, sizeof(WORD), 16 * 16, fp);
    fclose(fp);
}
