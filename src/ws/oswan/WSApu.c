
#include <stdio.h>
#include <string.h>

#include "WSHard.h"
#include "WS.h"
#include "WSApu.h"

#include "startup.h"

    #define WS_NO_SPLASH
#include <esp_attr.h>

// -----------------------------------------------------------------------------
// Config
// -----------------------------------------------------------------------------
#define WS_APU_TICK_RATE (75 * 159)
#define WS_OUTPUT_FREQ   24000
#define WAV_FREQ         WS_APU_TICK_RATE
#define WAV_VOLUME       40
#define WS_RING_BARRIER() __sync_synchronize()
// -----------------------------------------------------------------------------
// State APU
// -----------------------------------------------------------------------------
unsigned long WaveMap;
SOUND Ch[4];
int   VoiceOn;
SWEEP Swp;
NOISE Noise;
int   Sound[7] = {1, 1, 1, 1, 1, 1, 1};

// Stereo ring produced by the APU 
unsigned char* PData[4] = { NULL, NULL, NULL, NULL };// static unsigned char PDataN[8][BUFSIZEN];
int16_t* sndbuffer[2] = { NULL, NULL };  // [L/R]
volatile int32_t rBuf = 0, wBuf = 0;
static int   StartupFlag;
static int   s_outputAccum = 0;
static WORD  s_noiseLfsr = 0;
static int   s_noiseEnabled = 0;

// -----------------------------------------------------------------------------
// Allocation buffers sound
// -----------------------------------------------------------------------------
void apuAllocateBuffers(void) {
    // Allocate two buffers for left and right channels
    for (int ch = 0; ch < 2; ++ch) {
        sndbuffer[ch] = (int16_t*)malloc(SND_RNGSIZE * sizeof(int16_t));
        if (sndbuffer[ch] == NULL) {
            // In case of allocation error
            fprintf(stderr, "Error: unable to allocate APU buffer for channel %d\n", ch);
            exit(1);
        }
        memset(sndbuffer[ch], 0, SND_RNGSIZE * sizeof(int16_t));
    }

    // Alloue PData[4][32]
    for (int i = 0; i < 4; ++i) {
        PData[i] = (unsigned char*)malloc(32 * sizeof(unsigned char));
        if (PData[i] == NULL) {
            fprintf(stderr, "Erreur: impossible d’allouer PData[%d]\n", i);
            exit(1);
        }
        memset(PData[i], 0, 32 * sizeof(unsigned char));
    }
}

// -----------------------------------------------------------------------------
// Ring helpers 
// -----------------------------------------------------------------------------
int apuBufLen(void)
{
  int32_t read = rBuf;
  int32_t write = wBuf;
  if (write >= read) return write - read;
  return SND_RNGSIZE + write - read;
}

int apuReadStereo(int16_t* left, int16_t* right)
{
  int32_t read = rBuf;
  int32_t write = wBuf;

  if (read == write) return 0;

  WS_RING_BARRIER();
  if (left)  *left  = sndbuffer[0][read];
  if (right) *right = sndbuffer[1][read];

  read++;
  if (read >= SND_RNGSIZE) read = 0;

  WS_RING_BARRIER();
  rBuf = read;
  return 1;
}

static inline void apuWriteStereo(int16_t left, int16_t right)
{
  int32_t write = wBuf;
  int32_t next = write + 1;
  if (next >= SND_RNGSIZE) next = 0;

  if (next == rBuf) return;

  sndbuffer[0][write] = left;
  sndbuffer[1][write] = right;
  WS_RING_BARRIER();
  wBuf = next;
}

// -----------------------------------------------------------------------------
// Init / End
// -----------------------------------------------------------------------------
void apuWaveCreate(void)
{
  // No more SDL: nothing to do here
}

void apuWaveDel(void)
{
  // No more SDL: nothing to do here
}


void apuWaveClear(void)
{
  // No more SDL: nothing to do here
}

int apuInit(void)
{
  apuAllocateBuffers();
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 32; j++)
      PData[i][j] = 8;

  rBuf = 0;
  wBuf = 0;
  s_outputAccum = 0;
  s_noiseLfsr = 0;
  s_noiseEnabled = 0;
  apuWaveCreate();
  return 0;
}

void apuEnd(void)
{
  apuWaveDel();
}

// -----------------------------------------------------------------------------
// LFSR util
// -----------------------------------------------------------------------------
unsigned int apuMrand(unsigned int Degree)
{
#define BIT(n) (1U<<(n))
  typedef struct {
    unsigned int N;
    int          InputBit;
    int          Mask;
  } POLYNOMIAL;

  static POLYNOMIAL TblMask[] =
  {
    { 2, BIT(2),  BIT(0)|BIT(1)},
    { 3, BIT(3),  BIT(0)|BIT(1)},
    { 4, BIT(4),  BIT(0)|BIT(1)},
    { 5, BIT(5),  BIT(0)|BIT(2)},
    { 6, BIT(6),  BIT(0)|BIT(1)},
    { 7, BIT(7),  BIT(0)|BIT(1)},
    { 8, BIT(8),  BIT(0)|BIT(2)|BIT(3)|BIT(4)},
    { 9, BIT(9),  BIT(0)|BIT(4)},
    {10, BIT(10), BIT(0)|BIT(3)},
    {11, BIT(11), BIT(0)|BIT(2)},
    {12, BIT(12), BIT(0)|BIT(1)|BIT(4)|BIT(6)},
    {13, BIT(13), BIT(0)|BIT(1)|BIT(3)|BIT(4)},
    {14, BIT(14), BIT(0)|BIT(1)|BIT(4)|BIT(5)},
    {15, BIT(15), BIT(0)|BIT(1)},
    { 0, 0, 0},
  };
  static POLYNOMIAL *pTbl   = TblMask;
  static int         ShiftReg = BIT(2)-1;
  int XorReg = 0;
  int Masked;

  if (pTbl->N != Degree) {
    pTbl = TblMask;
    while (pTbl->N) {
      if (pTbl->N == Degree) break;
      pTbl++;
    }
    if (!pTbl->N) pTbl--;
    ShiftReg &= pTbl->InputBit - 1;
    if (!ShiftReg) ShiftReg = pTbl->InputBit - 1;
  }

  Masked = ShiftReg & pTbl->Mask;
  while (Masked) {
    XorReg ^= Masked & 1;
    Masked >>= 1;
  }
  if (XorReg) ShiftReg |=  pTbl->InputBit;
  else        ShiftReg &= ~pTbl->InputBit;

  ShiftReg >>= 1;
  return (unsigned int)ShiftReg;
}

// -----------------------------------------------------------------------------
// Tables PData
// -----------------------------------------------------------------------------
void apuSetPData(int addr, unsigned char val)
{
  int i = (addr & 0x30) >> 4;   // channel
  int j = (addr & 0x0F) << 1;   // two packed nibbles
  PData[i][j]     = (unsigned char)(val & 0x0F);
  PData[i][j + 1] = (unsigned char)((val & 0xF0) >> 4);
}

// -----------------------------------------------------------------------------
// DMA / Hyper Voice
// -----------------------------------------------------------------------------
unsigned char apuVoice(void)
{
  static int index = 0, b = 0;
  unsigned char v;

  if ((SDMACTL & 0x98) == 0x98) {  // Hyper voice
    BYTE* page = Page[(SDMASB + b) & 0x0F];
    v = (!page || page == MemDummy) ? MemDummy[0] : page[SDMASA + index];
    index++;
    if ((SDMASA + index) == 0) b++;
    v = (v < 0x80) ? (v + 0x80) : (v - 0x80);
    if (SDMACNT <= index) {
      index = 0;
      b     = 0;
    }
    return v;
  }
  else if ((SDMACTL & 0x88) == 0x80) { // DMA start
    BYTE* page = Page[(SDMASB + b) & 0x0F];
    IO[0x89] = (!page || page == MemDummy) ? MemDummy[0] : page[SDMASA + index];
    index++;
    if ((SDMASA + index) == 0) b++;
    if (SDMACNT <= index) {
      SDMACTL &= 0x7F; // DMA end
      SDMACNT  = 0;
      index    = 0;
      b        = 0;
    }
  }
  return ((VoiceOn && Sound[4]) ? IO[0x89] : 0x80);
}

unsigned char ws_apuhVoice(int count, BYTE *hvoice)
{
  static int index = 0;

  if ((IO[0x52] & 0x98) == 0x98) { // Hyper Voice On?
    int address = (IO[0x4c] << 16) | (IO[0x4b] << 8) | IO[0x4a];
    int size    =                   (IO[0x4f] << 8) | IO[0x4e];

    int value1  = cpu_readmem20(address + index);
    if (value1 < 0x80) *hvoice = (BYTE)(value1 + 0x80);
    else               *hvoice = (BYTE)(value1 - 0x80);

    if (count == 0) {
      if (size <= (++index)) index = 0;
    }
  } else {
    *hvoice = 0x80;
    index   = 0;
  }
  return *hvoice;
}

unsigned char ws_apuVoice(int count)
{
  if ((SDMACTL & 0x88) == 0x80) { // DMA start
    int i =                   (IO[0x4f] << 8) | IO[0x4e]; // size
    int j = (IO[0x4c] << 16) | (IO[0x4b] << 8) | IO[0x4a]; // start bank:address
    int k = ((IO[0x52] & 0x03) == 0x03 ? 2 : 1);

    IO[0x89] = (BYTE)cpu_readmem20(j);

    if ((count % (WS_OUTPUT_FREQ / 12000 / k)) == 0) {
      i--;
      j++;
    }

    if (i <= 0) {
      i = 0;
      IO[0x52] &= 0x7f; // DMA end
    }

    IO[0x4a] = (BYTE)  j;
    IO[0x4b] = (BYTE) (j >>  8);
    IO[0x4c] = (BYTE) (j >> 16);
    IO[0x4e] = (BYTE)  i;
    IO[0x4f] = (BYTE) (i >>  8);
  }
  return ((VoiceOn && Sound[4]) ? IO[0x89] : 0x80);
}

// -----------------------------------------------------------------------------
// Sweep / Noise stubs
// -----------------------------------------------------------------------------
void apuSweep(void)
{
  if ((Swp.step) && Swp.on) { // sweep on
    if (Swp.cnt < 0) {
      Swp.cnt   = Swp.time;
      Ch[2].freq += Swp.step;
      Ch[2].freq &= 0x7ff;
    }
    Swp.cnt--;
  }
}

void apuNoiseControl(unsigned char val)
{
  Noise.pattern = val & 0x07;
  s_noiseEnabled = val & 0x80;
  if (val & 0x08) {
    s_noiseLfsr = 0;
  }
}

static unsigned int apuNoiseBit(void)
{
  static const unsigned char tapBits[8] = {14, 10, 13, 4, 8, 6, 9, 11};
  const unsigned int tap = (s_noiseLfsr >> tapBits[Noise.pattern & 0x07]) & 1;
  const unsigned int bit7 = (s_noiseLfsr >> 7) & 1;
  const unsigned int newBit = (tap ^ bit7) ^ 1;

  s_noiseLfsr = (WORD)(((s_noiseLfsr << 1) & 0x7FFE) | newBit);
  return newBit;
}

WORD apuShiftReg(void)
{
  return s_noiseLfsr & 0x7FFF;
}

// -----------------------------------------------------------------------------
// Mix & push into ring 
// -----------------------------------------------------------------------------
static inline int16_t clamp16(int32_t v)
{
  if (v >  32767) return  32767;
  if (v < -32768) return -32768;
  return (int16_t)v;
}

void WsWaveSet(BYTE voice, BYTE hvoice)
{
  static int point[4]    = {0,0,0,0};
  static int preindex[4] = {0,0,0,0};
  int16_t lVol[4], rVol[4];
  int channel, index;
  int16_t value;

  for (channel = 0; channel < 4; channel++) {
    if (!Ch[channel].on) { lVol[channel]=rVol[channel]=0; continue; }

    if (channel == 1 && VoiceOn && Sound[4])      { lVol[channel]=rVol[channel]=0; continue; }
    if (channel == 2 && Swp.on && !Sound[5])      { lVol[channel]=rVol[channel]=0; continue; }

    if (channel == 3 && Noise.on && s_noiseEnabled && Sound[6]) {
      value = apuNoiseBit() ? 7 : -8;
    } else if (Sound[channel] == 0) {
      lVol[channel]=rVol[channel]=0; continue;
    } else {
      index = (3072000 / WAV_FREQ) * point[channel] / (2048 - Ch[channel].freq);
      if ((index %= 32) == 0 && preindex[channel]) point[channel] = 0;
      value = (int16_t)PData[channel][index] - 8;   // <- **PSG actif**
      preindex[channel] = index;
      point[channel]++;
    }

    lVol[channel] = (int16_t)(value * Ch[channel].volL);
    rVol[channel] = (int16_t)(value * Ch[channel].volR);
  }

  int16_t vVol = ((int16_t)voice  - 0x80) * 2;  // DMA voice
  int16_t hVol = ((int16_t)hvoice - 0x80) * 2;  // Hyper voice
  int32_t mixL = (int32_t)(lVol[0]+lVol[1]+lVol[2]+lVol[3] + vVol + hVol) * WAV_VOLUME;
  int32_t mixR = (int32_t)(rVol[0]+rVol[1]+rVol[2]+rVol[3] + vVol + hVol) * WAV_VOLUME;


  int16_t LL = clamp16(mixL);
  int16_t RR = clamp16(mixR);
  s_outputAccum += WS_OUTPUT_FREQ;
  while (s_outputAccum >= WS_APU_TICK_RATE) {
    apuWriteStereo(LL, RR);
    s_outputAccum -= WS_APU_TICK_RATE;
  }
}

void apuWaveSet(void)
{
  BYTE voice, hvoice;
  apuSweep();

  voice = ws_apuVoice(0);
  ws_apuhVoice(0, &hvoice);
  WsWaveSet(voice, hvoice);
  NCSR = apuShiftReg();
}

void apuStartupSound(void)
{
  StartupFlag = 1;
}
