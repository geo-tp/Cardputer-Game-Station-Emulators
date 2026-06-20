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
/* code to emulate the sound */

#include "psgplay.h"
#include "audioevent.h"
#include "psg.h"
#include "headers.h"
#include <stdlib.h>

static int	AY_VolumeTranslation[]= {0,0,0,1,1,1,2,3,4,6,8,10,14,19,24,31};

/* these are only updated when the audio event update requests it */
static int		PSGPlay_Registers[16];

static int PSGPlay_ChannelOutputVolumes[3];

typedef struct
{
	/* tone period for this channel. Period is time for 1/2
	the square wave */
	unsigned long Period;

	/* Period position - used to define where in waveform we are. */
	FIXED_POINT16 PeriodPosition;
	/* state of square wave. 0x00 or 0x0ff */
	int		WaveFormState;
	FIXED_POINT16	ToneUpdate;
} CHANNEL_PERIOD;


CHANNEL_PERIOD	ChannelPeriods[3]=
{
	
	{0,{{0}},0x0ff,{{0}}},
	{0,{{0}},0x0ff,{{0}}},
	{0,{{0}},0x0ff,{{0}}}

};

static 	FIXED_POINT16	NoisePeriod;
static FIXED_POINT16 NoisePeriodPosition;
static int RNG=1;
static int NoiseOutput = 0x0ff;
static int NewNoisePeriod = 0;
static FIXED_POINT16 NewNoiseUpdate;
static FIXED_POINT16 NoiseUpdate;

static unsigned short EnvelopePeriod= 0;



void	InitChannelTone(int ChannelIndex);
void	InitNoisePeriod(void);


void	PSG_Envelope_Initialise(void);
void	PSG_Envelope_SetPeriod(void);


static FIXED_POINT16	Update;

void	PSG_InitialiseToneUpdates(FIXED_POINT16 *pUpdate)
{
	Update.FixedPoint.L = pUpdate->FixedPoint.L;
}

void	PSGPlay_Reset(void)
{
	NoiseUpdate.FixedPoint.L = 1;

	ChannelPeriods[0].WaveFormState = 0xff;
	ChannelPeriods[1].WaveFormState = 0xff;
	ChannelPeriods[2].WaveFormState = 0xff;

	RNG=1;
	NoiseOutput=0x0ff;
}

void	PSGPlay_ReloadChannelTone(int ChannelIndex)
{
	unsigned long CurrentCount;
	unsigned long Period;
	CHANNEL_PERIOD *pChannelPeriod = &ChannelPeriods[ChannelIndex];

	Period = (
		PSGPlay_Registers[(ChannelIndex<<1)] |
		(PSGPlay_Registers[(ChannelIndex<<1)+1]<<8)
		);

	if (Period == 0)
		Period = 1;

	/* calculate current count */
	CurrentCount = (pChannelPeriod->PeriodPosition.FixedPoint.L*pChannelPeriod->Period)>>16;

	if (Period<CurrentCount)
	{
		/* reset counter */
		pChannelPeriod->PeriodPosition.FixedPoint.L &= 0x0ffff;
	}

	/* set new period */
	pChannelPeriod->Period = Period; 
	
	if (Period<5)
	{
		/* if period <5 is programmed I can't hear any sound  */
		/* but it is possible to hear sound up to 0x0fff */
		pChannelPeriod->ToneUpdate.FixedPoint.L = 0;
	}
	else
	{
		/* set new update */
		pChannelPeriod->ToneUpdate.FixedPoint.L = Update.FixedPoint.L/Period;
	}
}


void	PSG_UpdateState(unsigned long Reg, unsigned long Data)
{
	/* write register data */
	PSGPlay_Registers[Reg & 0x0f] = Data;

	switch(Reg)
	{
		case 0:
		case 1:
		{
			PSGPlay_ReloadChannelTone(0);
		}
		break;

		case 2:
		case 3:
		{
			PSGPlay_ReloadChannelTone(1);

		}
		break;

		case 4:
		case 5:
		{
			PSGPlay_ReloadChannelTone(2);
		}
		break;

		case 6:
		{

			InitNoisePeriod();
		}
		break;

		case 11:
		case 12:
			PSG_Envelope_SetPeriod();
			break;

		case 13:
		{
			PSG_Envelope_Initialise();
		}
		break;

		case 7:
		{


		}
		break;

		case 8:
		case 9:
		case 10:
			{
				/* setup the output volume - to remove extra lookup */
				
				/* use hardware envelope? */
				if ((Data & (1<<4))!=0)
				{
					/* no */
					PSGPlay_ChannelOutputVolumes[Reg-8] = AY_VolumeTranslation[Data & 0x0f];
				}

			}
			break;
	}
}


static unsigned char Attack[16]=
{
	 0,  1,  2,  3,	 4,  5, 6, 7, 8, 9,10,11,12,13,14,15
};

static unsigned char Decay[16]=
{
	15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
};

static unsigned char *EnvelopePatterns = NULL;

/*
* 0	0	-	-	\________________________
*

* 0	1	-	-	/|_______________________
*

* 1	0	0	0	\|\|\|\|\|\|\|\|\|\|\|\|\

* 1	0	0	1	\________________________
*

* 1	0	1	0	\/\/\/\/\/\/\/\/\/\/\/\/		

*				  _______________________
* 1	0	1	1	\|

* 1	1	0	0	/|/|/|/|/|/|/|/|/|/|/|/|/|

*				 _________________________
* 1	1	0	1	/

* 1	1	1	0	/\/\/\/\/\/\/\/\/\/\/\/\/\

* 1	1	1	1	/|________________________
*
*/

void	PSG_InitialiseEnvelopeShapes(void)
{
	int i;

	if (EnvelopePatterns == NULL)
	{
		EnvelopePatterns = (unsigned char *)malloc(16 * (16 * 2));
		if (EnvelopePatterns == NULL)
		{
			return;
		}
	}

	/* TEMP TO REMOVE THIS LOOKUP */
	for (i=0; i<16; i++)
	{
		Decay[i] = AY_VolumeTranslation[i];
	}

	for (i=0; i<16; i++)
	{
		unsigned char *pEnvelopeData;
		unsigned char *pEnvelope;

		pEnvelopeData = &EnvelopePatterns[i*(16*2)];

		if (i & PSG_ENVELOPE_ATTACK)
		{
			pEnvelope = Attack;
		}
		else
		{
			pEnvelope = Decay;
		}

		memcpy(pEnvelopeData, pEnvelope, 16);
		pEnvelopeData+=16;

		if ((i & PSG_ENVELOPE_CONTINUE)==0)
		{
			memset(pEnvelopeData, 0, (16*1));
		}
		else
		{

			/* hold envelope at a value ? */
			if (i & PSG_ENVELOPE_HOLD)
			{
				unsigned char Volume;

				Volume = (pEnvelopeData-1)[0];

				if (i & PSG_ENVELOPE_ALTERNATE)
				{
					Volume = (15-Volume) & 15;
				}

				memset(pEnvelopeData, Volume, (16*1));
			}
			else
			{

				if (i & PSG_ENVELOPE_ALTERNATE)
				{
					/* change envelope pattern */
					if (pEnvelope==Attack)
					{
						pEnvelope = Decay;
					}
					else if (pEnvelope==Decay)
					{
						pEnvelope = Attack;
					}
				}

				memcpy(pEnvelopeData, pEnvelope,16);
				pEnvelopeData+=16;
#if 0
				if (i & PSG_ENVELOPE_ALTERNATE)
				{
					/* change envelope pattern */
					if (pEnvelope==Attack)
					{
						pEnvelope = Decay;
					}
					else if (pEnvelope==Decay)
					{
						pEnvelope = Attack;
					}
				}
				memcpy(pEnvelopeData, pEnvelope,16);
#endif		
			}
		}

	}
}




static int EnvelopePhase = 0;

static FIXED_POINT16 EnvelopePeriodPosition;

static int PositionInEnvelope = 0;
static char *pEnvelope = (char *)Attack;
static int EnvelopeVolume = 0;
static int CurrentEnvelope;
static FIXED_POINT16	EnvelopeUpdate;

void	PSG_Envelope_Initialise(void)
{
	unsigned long EnvelopeTimes16;
	
	CurrentEnvelope = PSGPlay_Registers[13];
	EnvelopeTimes16 = (CurrentEnvelope & 0x0f)<<4;
	if (EnvelopePatterns!=NULL)
	{
		pEnvelope = (char *)&EnvelopePatterns[EnvelopeTimes16 + (EnvelopeTimes16<<1)];
	}
	else
	{
		pEnvelope = (char *)Attack;
	}
	EnvelopePhase = 0;
	PositionInEnvelope = 0;
}

void	PSG_Envelope_SetPeriod(void)
{
	unsigned long CurrentCount;
	unsigned long Period = (PSGPlay_Registers[11] & 0x0ff) |
					((PSGPlay_Registers[12] & 0x0ff)<<8);

	/* calculate current count */
	CurrentCount = (EnvelopePeriodPosition.FixedPoint.L*EnvelopePeriod)>>16;

	if (Period<CurrentCount)
	{
		/* reset counter */
		EnvelopePeriodPosition.FixedPoint.L &= 0x0ffff;
	}

	/* set new period */
	EnvelopePeriod = Period; 

	/* set new update */

	/* on amstrad I can't tell the difference between Period of 0 and period of 1 */
	if (Period==0)
	{
		Period = 1;
	}
	EnvelopeUpdate.FixedPoint.L = Update.FixedPoint.L/Period;
}


static INLINE void	Envelope_Update(FIXED_POINT16 *pUpdate)
{
	int NoOfCycles;

	/* update position in period */
	EnvelopePeriodPosition.FixedPoint.L +=EnvelopeUpdate.FixedPoint.L;	
	
	/* update number of cycles actually processed */
	NoOfCycles = (EnvelopePeriodPosition.FixedPoint.L>>16);	
	
	EnvelopePeriodPosition.FixedPoint.L &= 0x0ffff;
	
	/* exceeded 16 cycles = one whole wave complete. */
	if (NoOfCycles>=16)
	{
		EnvelopePhase = 0;
	}
		
	/* update position in envelope */
	PositionInEnvelope = (PositionInEnvelope + NoOfCycles);

	PositionInEnvelope = PositionInEnvelope & 0x1f;

	EnvelopeVolume = pEnvelope[(EnvelopePhase<<4) + PositionInEnvelope];
}

INLINE void	UpdateChannelToneState(int ChannelIndex, FIXED_POINT16 *pUpdate)
{
	CHANNEL_PERIOD *pChannelPeriod = &ChannelPeriods[ChannelIndex];

	int NoOfCycles;

	pChannelPeriod->PeriodPosition.FixedPoint.L+=pChannelPeriod->ToneUpdate.FixedPoint.L;

	/* update position in waveform */

	/* how many cycles have occured in this update ? */
	NoOfCycles = (pChannelPeriod->PeriodPosition.FixedPoint.L>>16);	

	/* if odd, invert state, else keep state the same */
	if (NoOfCycles & 0x01)
	{
		/* odd number of cycles - invert state*/
		pChannelPeriod->WaveFormState^=0x0ff;
	}

	/* zeroise integer part */
	pChannelPeriod->PeriodPosition.FixedPoint.L &= 0x0ffff;
}

static INLINE void	NoiseChooseOutput(void)
{
	/* Is noise output going to change? */
	if ((RNG + 1) & 2)	/* (bit0^bit1)? */
	{
		NoiseOutput = NoiseOutput^0x0ff;
	}

	/* The Random Number Generator of the 8910 is a 17-bit shift */
	/* register. The input to the shift register is bit0 XOR bit2 */
	/* (bit0 is the output). */

	/* The following is a fast way to compute bit 17 = bit0^bit2. */
	/* Instead of doing all the logic operations, we only check */
	/* bit 0, relying on the fact that after two shifts of the */
	/* register, what now is bit 2 will become bit 0, and will */
	/* invert, if necessary, bit 16, which previously was bit 18. */
	if (RNG & 1) RNG ^= 0x28000;
	RNG >>= 1;
}


void	UpdateNoise(FIXED_POINT16 *pUpdate)
{
	int NoOfCycles;

	NoisePeriodPosition.FixedPoint.L += NoiseUpdate.FixedPoint.L;	
	
	/* how many cycles have occured in this update ? */
	NoOfCycles = NoisePeriodPosition.FixedPoint.L>>16;	
	
	NoisePeriodPosition.FixedPoint.L &= 0x0ffff;
	
	/* if odd, invert state, else keep state the same */
	if (NoOfCycles & 0x01)
	{
		NoiseChooseOutput();
		NoisePeriod.FixedPoint.L = NewNoisePeriod<<16;
		NoiseUpdate.FixedPoint.L = NewNoiseUpdate.FixedPoint.L;
	}
	
}

void	InitNoisePeriod(void)
{
	int Period = (PSGPlay_Registers[6] & 0x01f);

	if (Period==0)
	{
		Period = 1;
	}

	NewNoisePeriod = Period;
	NewNoiseUpdate.FixedPoint.L = Update.FixedPoint.L/Period; 
}

static INLINE int	GetMixedOutputForChannel(int ChannelIndex)
{
	unsigned long Mixer = PSGPlay_Registers[7];
	unsigned long ChannelMask = (1<<ChannelIndex);
	unsigned long ChannelToneOutput, ChannelNoiseOutput;

	{
		unsigned long Mixer_ToneOutput;
		
		/* tone enable */
		if ((Mixer & ChannelMask)==0)
		{
			/* tone enabled */
			Mixer_ToneOutput = 0;
		}
		else
		{
			Mixer_ToneOutput = -1;
		}

		ChannelToneOutput = ChannelPeriods[ChannelIndex].WaveFormState | Mixer_ToneOutput;
	}

	ChannelMask = ChannelMask<<3;

	{
		unsigned long Mixer_NoiseOutput;
	
		/* noise enable */
		if ((Mixer & ChannelMask)==0)
		{
			/* noise enabled */
			Mixer_NoiseOutput = 0;
		}
		else
		{
			Mixer_NoiseOutput = -1;
		}

		ChannelNoiseOutput = NoiseOutput | Mixer_NoiseOutput;
	}

	return ChannelToneOutput & ChannelNoiseOutput;
}

static INLINE int	GetFinalVolumeForChannel(int ChannelIndex)
{
	unsigned long ChannelOutput;
	int ChannelOutputVolume;
	int ChannelVolume;

	/* get programmed volume */
	ChannelVolume = PSGPlay_Registers[8+ChannelIndex];

	/* get state of waveform */
	ChannelOutput = GetMixedOutputForChannel(ChannelIndex);
	
	
	if ((ChannelVolume & 0x010)!=0)
	{
		/* envelope controls volume */
		if (ChannelOutput==0x00)
		{
			ChannelOutputVolume = (0x080 - ((int)EnvelopeVolume<<2));	
		}
		else
		{
			ChannelOutputVolume = (0x080 + ((int)EnvelopeVolume<<2));	
		}

	}
	else
	{
		/* volume used is the programmed volume */
	
		if (ChannelOutput==0x00)
		{
			ChannelOutputVolume = (0x080 - (AY_VolumeTranslation[ChannelVolume]<<2));
		}
		else
		{
			ChannelOutputVolume = (0x080 + (AY_VolumeTranslation[ChannelVolume]<<2));
		}
	
	}
	
	/* return volume for that channel */
	return ChannelOutputVolume;

}


PSG_OUTPUT_VOLUME		PSG_UpdateChannels(FIXED_POINT16 *pPeriodUpdate)
{
	PSG_OUTPUT_VOLUME PSG_Output={0,0,0,0};

	/* update envelope */
	Envelope_Update(pPeriodUpdate);
	
	UpdateNoise(pPeriodUpdate);

	/* update tones */
	UpdateChannelToneState(0, pPeriodUpdate);
	UpdateChannelToneState(1, pPeriodUpdate);
	UpdateChannelToneState(2, pPeriodUpdate);

	PSG_Output.VolA = GetFinalVolumeForChannel(0);
	PSG_Output.VolB = GetFinalVolumeForChannel(1);
	PSG_Output.VolC = GetFinalVolumeForChannel(2);

	return PSG_Output;
}


void PSGPlay_Initialise(void)
{
	PSG_InitialiseEnvelopeShapes();
}

void PSGPlay_Write(int Register, int Data)
{
	AudioEvent_AddEventToBuffer(AUDIO_EVENT_PSG, Register, Data);
}
