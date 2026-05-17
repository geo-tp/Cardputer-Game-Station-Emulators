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
#include "audioevent.h"
#include "cpc.h"
//#include "debugmain.h"  
#include "psg.h"

#include "headers.h"

#include "host.h"

void	Digiblaster_Reset(void);

int SamplesToGenerate;
int AudioEvent_SampleBits;
int AudioEvent_SampleChannels;

void	AudioEvent_ConvertToOutputFormat_8BitStereo(char *pSrcBuffer, char *pDestBuffer, int Count);

static int *pVolumeLookupPSG;
static int (*pAudioWriteFunc)(unsigned char *, unsigned long, unsigned long);
int AudioEvent_Mix8BitStereo(unsigned char *, unsigned long, unsigned long);
int AudioEvent_Mix8BitMono(unsigned char *, unsigned long, unsigned long);
int AudioEvent_Mix16BitStereo(unsigned char *, unsigned long, unsigned long);
int AudioEvent_Mix16BitMono(unsigned char *, unsigned long, unsigned long);
void    Audio_Init(int newFrequency, int newBitsPerSample, int newNoOfChannels);

unsigned char *Digiblaster_Buffer = NULL;
unsigned char *pMixedSampleData = NULL;




int     AudioSampleRate;
/*int AudioNopCount=0; */
/*int NopCountVol=0; */
/*int NopCountVolBase = 0; */
unsigned char Vol;
BOOL DAC_AudioActive = TRUE;


float   NopScale;
float   SamplesPerPSGEvent;
FIXED_POINT16   fNopScale;
float   NopsPerSample;


/* screen refresh frequency */
float ScreenRefreshFrequency;
/* no of samples per screen refresh */
float   SamplesPerScreen;
/* no of bytes per sample element. */
int             BytesPerSample;
/* bytes per sample part - e.g. for a single channel */
int BytesPerSamplePart;

/* size of sample buffer */
int             AudioBufferSize;

int AudioBufSize;

/* addr of audio buffer */
unsigned char *pAudioBuffer = NULL;
unsigned char *pAudioBuffer2 = NULL;

/* sample rate */
unsigned long SampleRate;
/* bits per sample */
int BitsPerSample;
/* no of channels */
int NoOfChannels;
/* no of samples per Nop-Cycle */
float SamplesPerNop;
/* no of PSG events per sample */
float PSGEventsPerSample;
/* no of nops per sample */
float NopsPerSample;

FIXED_POINT16   fSamplesPerPSGEvent;
FIXED_POINT16   fNopsPerSample;
FIXED_POINT16   fPSGEventsPerSample;
FIXED_POINT16   fSamplesPerNop;

int NoOfSamplesInSampleBuffer;


/*---------- EVENT BUFFER -----------*/
/* size of event buffer to store events */
static unsigned long EventBufferLength;
static int EventBufferCapacity;
/* pointer to base of event buffer */
static char *pEventBuffer;
/* pointer to position to add next event */
static char *pEventBufferStartPtr;
static char *pEventBufferPtr;
/* no of events currently in buffer */
static int NoOfEventsInBuffer;
/* NOP count at which this event buffer was started */
static int EventBufferNopCount;

static FIXED_POINT16    NopCount;

void    AudioEvent_Initialise(void)
{
	int NoOfEvents;
	int TryEvents;

		/* On constrained embedded heaps, 19968 events is too expensive and can
		starve later video allocations. Start with a practical cap and degrade
		gracefully if needed. */
		NoOfEvents = 4096;
	pEventBuffer = NULL;
	EventBufferLength = 0;
	EventBufferCapacity = 0;

	for (TryEvents = NoOfEvents; TryEvents >= 512; TryEvents >>= 1)
	{
		EventBufferLength = (unsigned long)TryEvents * sizeof(AUDIO_EVENT);
		pEventBuffer = malloc(EventBufferLength);
		if (pEventBuffer!=NULL)
		{
			EventBufferCapacity = TryEvents;
			break;
		}
	}

        if (pEventBuffer!=NULL)
        {
                AudioEvent_RestartEventBuffer(0);
	}
	else
	{
		printf("[GX4-AUDCORE] eventbuf alloc failed\n");
        }

        NopCount.FixedPoint.L = 0;
}

static int	AY_VolumeTranslation[]= {0,0,0,1,1,1,2,3,4,6,8,10,14,19,24,31};

void	Audio_Reset(void)
{
		/* empty event buffer ready for next re-fill */
		AudioEvent_RestartEventBuffer(CPC_GetNopCount());

		Digiblaster_Reset();
}

void    Audio_Init(int newFrequency, int newBitsPerSample, int newNoOfChannels)
{
        SampleRate = newFrequency;
        BitsPerSample = newBitsPerSample;
        NoOfChannels = newNoOfChannels;

        ScreenRefreshFrequency = 50.08f;
	
        /* no of samples per screen refresh */
        SamplesPerScreen = (float)SampleRate/(float)ScreenRefreshFrequency;
	


		


        /* no of bytes per sample element */

        /* calculate size of sample buffer */
        AudioBufSize = (int)(SamplesPerScreen * 4.0f);	/*BytesPerSample; */
		AudioBufferSize = AudioBufSize;

        NoOfSamplesInSampleBuffer = (int)SamplesPerScreen;


        /* allocate memory for sample buffer */
        pAudioBuffer = (unsigned char *)malloc(AudioBufSize);
        pAudioBuffer2 = (unsigned char *)malloc(AudioBufSize);
		Digiblaster_Buffer = (unsigned char *)malloc(AudioBufSize);
		pMixedSampleData  = (unsigned char *)malloc(AudioBufSize);
	
		Digiblaster_Init();

        if (pAudioBuffer!=NULL)
        {
                /* initialise buffer */
                memset(pAudioBuffer, 0x080, AudioBufferSize);
        }

        /* samples per nop */
        SamplesPerNop = (float)SamplesPerScreen/(float)NOPS_PER_FRAME;

        NopsPerSample = (float)NOPS_PER_FRAME/(float)SamplesPerScreen;

        /* samples per psg event */
        /* the PSG clock is divided by 8, and used for tone and noise
        generation.
        Therefore, we only really need a clock resolution of PSG_CLOCK_FREQUENCY/8 */
        PSGEventsPerSample = (float)(PSG_CLOCK_FREQUENCY>>3)/(float)SampleRate;
                   PSGEventsPerSample =
//(float)(PSG_CLOCK_FREQUENCY>>3)/(float)SampleRate;

        /* fixed point versions */
        fSamplesPerPSGEvent.FixedPoint.L = (int)(PSGEventsPerSample*65536.0f);
        fNopScale.FixedPoint.L = (int)(SamplesPerNop*65536.0f);
        fNopsPerSample.FixedPoint.L = (int)(NopsPerSample*65536.0f);
        fPSGEventsPerSample.FixedPoint.L = (int)(PSGEventsPerSample*65536.0f);
        /* samples per nop */
        fSamplesPerNop.FixedPoint.L = (int)(SamplesPerNop*65536.0f);

		{
			float NopsPerSecond;
			float NopsPerSample;
			float PSGEventsPerSample;

			NopsPerSecond = 19968*ScreenRefreshFrequency;
			
			NopsPerSample = NopsPerSecond/SampleRate;

			PSGEventsPerSample = (float)(PSG_CLOCK_FREQUENCY>>3)/(float)SampleRate;

			fNopsPerSample.FixedPoint.L = (int)(NopsPerSample*65536.0f);
			fPSGEventsPerSample.FixedPoint.L = (int)(PSGEventsPerSample*65536.0f); 
		}



		PSG_InitialiseToneUpdates(&fPSGEventsPerSample);


        /* initialise format that samples will be written to file as */
    //    WavOutput_InitialiseFormat(NoOfChannels, SampleRate, BitsPerSample);
		{
			/* Lookup tables removed: not used by current GX4000 audio path. */
		}
}

void    Audio_Finish(void)
{
        if (pAudioBuffer!=NULL)
        {
                free(pAudioBuffer);
        }
        if (pAudioBuffer2!=NULL)
        {
                free(pAudioBuffer2);
        }

		if (Digiblaster_Buffer!=NULL)
			free(Digiblaster_Buffer);


}

void    Audio_Enable(BOOL State)
{
        DAC_AudioActive = State;
}


void    AudioEvent_SetFormat(int SampleRate, int SampleBits, int SampleChannels)
{
        AudioEvent_SampleBits = SampleBits;
		AudioEvent_SampleChannels = SampleChannels;
        Audio_Init(SampleRate, SampleBits, SampleChannels);

}

void    AudioEvent_Finish(void)
{
        if (pEventBuffer!=NULL)
        {
                free(pEventBuffer);
                pEventBuffer = NULL;
        }

        Audio_Finish();
}

/* start entering new events from start of buffer */
void    AudioEvent_RestartEventBuffer(int CPCNopCount)
{
        pEventBufferPtr = pEventBuffer;
        NoOfEventsInBuffer = 0;
        EventBufferNopCount = CPCNopCount;      
}

/* add event to buffer as long as there is space, otherwise silently ignore it */
void    AudioEvent_AddEventToBuffer(unsigned long EventType, unsigned long Value1, unsigned long Value2)
{
        if (pEventBuffer!=NULL)
        {
                if (pEventBufferPtr<(pEventBuffer + EventBufferLength))
                {
                        AUDIO_EVENT *pEvent = (AUDIO_EVENT *)pEventBufferPtr;

                        pEvent->NopCount = CPC_GetNopCount();
                        pEvent->Type = EventType;

                        switch (EventType)
						{
							case AUDIO_EVENT_PSG:
							{
                                pEvent->AudioEvent.PSG_Event.PSG_Register = Value1;
                                pEvent->AudioEvent.PSG_Event.PSG_RegisterData = Value2;
							}
							break;

							case AUDIO_EVENT_DIGIBLASTER:
							{
                                pEvent->AudioEvent.Digiblaster_Event.Volume = Value1;
							}
							break;

							case AUDIO_EVENT_TAPE:
							{
                                pEvent->AudioEvent.Tape_Event.Volume = Value1;
							}
							break;

							default:
								break;
						}

                        pEventBufferPtr+=sizeof(AUDIO_EVENT);
                        NoOfEventsInBuffer++;
                }
        }
}

int AudioEvent_GetPendingEventCount(void)
{
	return NoOfEventsInBuffer;
}

int AudioEvent_GetEventCapacity(void)
{
	return EventBufferCapacity;
}


/* left and right audio channel volumes from digiblaster sound device */
unsigned long Digiblaster_Volume=0x080;
unsigned long Tape_Volume = 0;








char *AudioEvent_UpdateCycle(char *pAudioPtr)
{
	PSG_OUTPUT_VOLUME PSG_Output;
    signed char LeftVolume;
	signed char RightVolume;
	
	/* update PSG and get volume outputs for channel A,B and C */
	PSG_Output = PSG_UpdateChannels(&fPSGEventsPerSample);
    
	/* Mix Volume channels as on a real CPC */

	LeftVolume = (signed char)((PSG_Output.VolA>>1) + (PSG_Output.VolB>>1));
	RightVolume = (signed char)((PSG_Output.VolC>>1) + (PSG_Output.VolB>>1));

	/* Left Volume = A + (C>>1); Right Volume = B + (C>>1); */
/*	ChannelBVolume = AY_VolumeTranslation[PSG_Output.VolB]>>1; */

/*  LeftVolume = (AY_VolumeTranslation[PSG_Output.VolA]>>1) + ChannelBVolume; */
/*	RightVolume = (AY_VolumeTranslation[PSG_Output.VolC]>>1) + ChannelBVolume; */

	/* store outputs */
	pAudioPtr[0] = LeftVolume;
	pAudioPtr[1] = RightVolume;
/*	pAudioPtr[2] = Digiblaster_Volume; */
	pAudioPtr+=4;

	return pAudioPtr;       
}


#if 0
void	AudioEvent_ConvertToOutputFormat_8BitStereo(char *pSrcBuffer, char *pDestBuffer, int Count)
{
	int i;

	char *pSrcAudioPtr = pSrcBuffer;
	char *pDestAudioPtr = pDestBuffer;

	for (i=0; i<Count; i++)
	{
		unsigned char PSG_LeftVolume, PSG_RightVolume;
		unsigned char Digiblaster_Vol;

		/* get left volume, get right volume */
		PSG_LeftVolume = pSrcAudioPtr[0];
		pSrcAudioPtr++;
		PSG_RightVolume = pSrcAudioPtr[0];
		pSrcAudioPtr++;
		pSrcAudioPtr+=2;

		pDestAudioPtr[0] = PSG_LeftVolume;
		pDestAudioPtr++;
		pDestAudioPtr[0] = PSG_RightVolume;
		pDestAudioPtr++;
	}

/*	{
		for (i=0; i<Count; i++)
		{
			unsigned char *pSource;

			unsigned char Data;

			Data = pSource[0];

			Data = (Data>>1) + 
		}
	}
*/
}
#endif

void	AudioEvent_ConvertToOutputFormat_8BitMono(char *pSrcBuffer, char *pDestBuffer, int Count)
{
	int i;

	char *pSrcAudioPtr = pSrcBuffer;
	char *pDestAudioPtr = pDestBuffer;

	for (i=0; i<Count; i++)
	{
		unsigned char PSG_LeftVolume, PSG_RightVolume;
		unsigned long PSG_Volume;
/*		unsigned char Digiblaster_Vol; */

		/* get left volume, get right volume */
		PSG_LeftVolume = pSrcAudioPtr[0];
		pSrcAudioPtr++;
		PSG_RightVolume = pSrcAudioPtr[0];
		pSrcAudioPtr++;
/*		Digiblaster_Vol = Digiblaster_Lookup8Bit[pSrcAudioPtr[0]]>>1; */
		pSrcAudioPtr+=2;
		
		/* mix it */
		PSG_Volume = (PSG_LeftVolume + PSG_RightVolume)>>1;

		pDestAudioPtr[0] = PSG_Volume;	/*(VolumeLookup8Bit[PSG_Volume]>>1) + Digiblaster_Vol; */
		pDestAudioPtr++;
	}
}


void	AudioEvent_ConvertToOutputFormat_16BitStereo(char *pSrcBuffer, char *pDestBuffer, int Count)
{
	int i;

	unsigned char *pSrcAudioPtr = (unsigned char *)pSrcBuffer;
	short *pDestAudioPtr = (short *)pDestBuffer;

	for (i=0; i<Count; i++)
	{
		/* Mixer bytes are unsigned-centred (0x80 = silence). */
		int PSG_LeftVolume = (int)pSrcAudioPtr[0] - 128;
		int PSG_RightVolume = (int)pSrcAudioPtr[1] - 128;
		pSrcAudioPtr += 4;

		/* Expand signed 8-bit-like range to signed 16-bit PCM. */
		pDestAudioPtr[0] = (short)(PSG_LeftVolume << 8);
		pDestAudioPtr++;
		pDestAudioPtr[0] = (short)(PSG_RightVolume << 8);
		pDestAudioPtr++;
	}
}

void	AudioEvent_ConvertToOutputFormat_16BitMono(char *pSrcBuffer, char *pDestBuffer, int Count)
{
	int i;

	unsigned char *pSrcAudioPtr = (unsigned char *)pSrcBuffer;
	short *pDestAudioPtr = (short *)pDestBuffer;

	for (i=0; i<Count; i++)
	{
		int PSG_LeftVolume = (int)pSrcAudioPtr[0] - 128;
		int PSG_RightVolume = (int)pSrcAudioPtr[1] - 128;
		int Mixed = (PSG_LeftVolume + PSG_RightVolume) >> 1;
		pSrcAudioPtr += 4;

		pDestAudioPtr[0] = (short)(Mixed << 8);
		pDestAudioPtr++;
	}
}



void	AudioEvent_ConvertToOutputFormat(void)
{
/*    unsigned char *pAudioPtr = pAudioBuffer; */
	unsigned char *pAudio1 = NULL, *pAudio2 = NULL;
	unsigned long AudioBlock1Size = 0, AudioBlock2Size = 0;

	SamplesToGenerate = NoOfSamplesInSampleBuffer;
	AudioBufferSize = (SamplesToGenerate * AudioEvent_SampleChannels * (AudioEvent_SampleBits>>3));

	if (Host_LockAudioBuffer(&pAudio1, &AudioBlock1Size, &pAudio2, &AudioBlock2Size, AudioBufferSize))
	{
		memcpy(pAudio1, pMixedSampleData, AudioBlock1Size);
		
		if (pAudio2!=NULL)
		{
			memcpy(pAudio2, pMixedSampleData+AudioBlock1Size, AudioBlock2Size);
		}

#if 0
		pSrcAudioPtr1 =	pAudioBuffer2;	
		pSrcAudioPtr2 = pSrcAudioPtr1 + (Block1Count<<2);

		switch (AudioEvent_SampleBits)
		{
			case 8:
			{
				if (AudioEvent_SampleChannels == 2)
				{
					AudioEvent_ConvertToOutputFormat_8BitStereo(pSrcAudioPtr1,pAudio1,Block1Count);

					if (pAudio2!=NULL)
					{
						AudioEvent_ConvertToOutputFormat_8BitStereo(pSrcAudioPtr2,pAudio2,Block2Count);
					}
				}
				else
				{
					AudioEvent_ConvertToOutputFormat_8BitMono(pAudioBuffer2, pAudioBuffer, Block1Count);
								
					if (pAudio2!=NULL)
					{
						AudioEvent_ConvertToOutputFormat_8BitMono(pAudioBuffer2,pAudioBuffer,Block2Count);
					}
				}
			}
			break;

			case 16:
			{
				if (AudioEvent_SampleChannels == 2)
				{
					AudioEvent_ConvertToOutputFormat_16BitStereo(pSrcAudioPtr1,pAudio1,Block1Count);

					if (pAudio2!=NULL)
					{
						AudioEvent_ConvertToOutputFormat_16BitStereo(pSrcAudioPtr2,pAudio2,Block2Count);
					}
				}
				else
				{
					AudioEvent_ConvertToOutputFormat_16BitMono(pAudioBuffer2, pAudioBuffer, Block1Count);
								
					if (pAudio2!=NULL)
					{
						AudioEvent_ConvertToOutputFormat_16BitMono(pAudioBuffer2,pAudioBuffer,Block2Count);
					}
				}
			}
			break;
		}
#endif
		/* if wave output is enabled, then dump sound data to a wav file */
//		WavOutput_WriteBlock((char *)pAudio1, AudioBlock1Size);
		
		if (pAudio2!=NULL)
		{
//			WavOutput_WriteBlock((char *)pAudio2, AudioBlock2Size);
		}

		/* unlock */
		Host_UnLockAudioBuffer();
	}
	else
	{
		/* Embedded host backend does not expose a lockable ring buffer.
		   Push the mixed block directly to the host audio sink. */
		Host_WriteDataToSoundBuffer(pMixedSampleData, AudioBufferSize);
	}
}

#if 0
void	AudioEvent_ConvertToOutputFormat(void)
{
    unsigned char *pAudioPtr = pAudioBuffer;
	unsigned char *pAudio1 = NULL, *pAudio2 = NULL;
	unsigned long AudioBlock1Size = 0, AudioBlock2Size = 0;

	AudioBufferSize = (NoOfSamplesInSampleBuffer * AudioEvent_SampleChannels * (AudioEvent_SampleBits>>3));

	if (Host_LockAudioBuffer(&pAudio1, &AudioBlock1Size, &pAudio2, &AudioBlock2Size, AudioBufferSize))
	{
		unsigned char *pSrcAudioPtr1;
		unsigned char *pSrcAudioPtr2;
		int Block1Count = AudioBlock1Size / (AudioEvent_SampleChannels* (AudioEvent_SampleBits>>3));
		int Block2Count = AudioBlock2Size / (AudioEvent_SampleChannels* (AudioEvent_SampleBits>>3));

		pSrcAudioPtr1 =	pAudioBuffer2;	
		pSrcAudioPtr2 = pSrcAudioPtr1 + (Block1Count<<2);

		switch (AudioEvent_SampleBits)
		{
			case 8:
			{
				if (AudioEvent_SampleChannels == 2)
				{
					AudioEvent_ConvertToOutputFormat_8BitStereo(pSrcAudioPtr1,pAudio1,Block1Count);

					if (pAudio2!=NULL)
					{
						AudioEvent_ConvertToOutputFormat_8BitStereo(pSrcAudioPtr2,pAudio2,Block2Count);
					}
				}
				else
				{
					AudioEvent_ConvertToOutputFormat_8BitMono(pAudioBuffer2, pAudioBuffer, Block1Count);
								
					if (pAudio2!=NULL)
					{
						AudioEvent_ConvertToOutputFormat_8BitMono(pAudioBuffer2,pAudioBuffer,Block2Count);
					}
				}
			}
			break;

			case 16:
			{
				if (AudioEvent_SampleChannels == 2)
				{
					AudioEvent_ConvertToOutputFormat_16BitStereo(pSrcAudioPtr1,pAudio1,Block1Count);

					if (pAudio2!=NULL)
					{
						AudioEvent_ConvertToOutputFormat_16BitStereo(pSrcAudioPtr2,pAudio2,Block2Count);
					}
				}
				else
				{
					AudioEvent_ConvertToOutputFormat_16BitMono(pAudioBuffer2, pAudioBuffer, Block1Count);
								
					if (pAudio2!=NULL)
					{
						AudioEvent_ConvertToOutputFormat_16BitMono(pAudioBuffer2,pAudioBuffer,Block2Count);
					}
				}
			}
			break;
		}

		/* if wave output is enabled, then dump sound data to a wav file */
		WavOutput_WriteBlock(pAudio1, AudioBlock1Size);
		
		if (pAudio2!=NULL)
		{
			WavOutput_WriteBlock(pAudio2, AudioBlock2Size);
		}

		/* unlock */
		Host_UnLockAudioBuffer();
	}
}
#endif

unsigned long AudioEvent_PreviousFraction = 0;

/* go through all events and build sample data */
int     AudioEvent_TraverseAudioEventsAndBuildSampleData(int CPCNopCount, int NopsPerFrameUpdate)
{
        int i;
/*        int EventBufferNopEnd; */

        
        {
        AUDIO_EVENT *pCurrentEvent = (AUDIO_EVENT *)pEventBuffer;

        unsigned char *pAudioPtr = pAudioBuffer2;

        int EventCount = NoOfEventsInBuffer;
        int NopOffset;

        int NopsCountedThisFrame = CPCNopCount - EventBufferNopCount;

        FIXED_POINT16   NopScale;
        FIXED_POINT16   NopsPerSampleScaled;
/*        FIXED_POINT16   PSGEventsPerSample; */

        if (NopsCountedThisFrame==0)
        {
                NopsCountedThisFrame = NopsPerFrameUpdate;
        }

        NopCount.FixedPoint.L = AudioEvent_PreviousFraction;	

        /* nop scale will be 1 if running at exact speed.  */

		/* NopsCountThisFrame = Number Of Nops that have passed between this and the last audio update */
		/* NopsPerFrameUpdate = 19968 = Number Of Nops which should occur in frame for 1:1 sound output */
		
        NopScale.FixedPoint.L = (NopsCountedThisFrame<<16)/NopsPerFrameUpdate;
        
		/* we want the increment to add to the NOP count so that at the end of NoOfSamplesInSampleBuffer it
		gives the number of nops */
		

		NopsPerSampleScaled.FixedPoint.L = fNopsPerSample.FixedPoint.L;	

        if (EventCount<=0)
        {
                /* no events in buffer, therefore, no event data should be looked at */
                NopOffset = -1;
        }
        else
        {
                /* no of nops to first event in buffer */
                NopOffset = pCurrentEvent->NopCount - EventBufferNopCount;
        }

		{
			/* for each sample in sample buffer */
			for (i=0; i<NoOfSamplesInSampleBuffer; i++)
			{
				/* compare current NopCount to NopCount of next event */
				/* if it is equal or greater, then we must perform this
				event */
				if (NopOffset!=-1)
				{
					int NopCountBefore = NopCount.FixedPoint.L;
					int NopCountAfter = NopCount.FixedPoint.L + NopsPerSampleScaled.FixedPoint.L;
					if ((NopCountBefore<=(NopOffset<<16)) &&
						(NopCountAfter>(NopOffset<<16)))
					{
						do
						{
							/* we need to perform this event */
							switch (pCurrentEvent->Type)
							{
								case AUDIO_EVENT_PSG:
								{
										/* it is a PSG event */

									unsigned long TimeBefore;

									/* gets time before this value was written */
									TimeBefore = (NopOffset<<16) - NopCountBefore;



										PSG_UpdateState(pCurrentEvent->AudioEvent.PSG_Event.PSG_Register, pCurrentEvent->AudioEvent.PSG_Event.PSG_RegisterData);
								}
								break;
#if 0
								case AUDIO_EVENT_DIGIBLASTER:
								{
										/* it is a Digiblaster event */
										Digiblaster_Volume = pCurrentEvent->AudioEvent.Digiblaster_Event.Volume & 0x0ff;
								}
								break;
#endif					
							
								default:
									break;
							}

							EventCount--;

							/* have we run out of events? */
							if (EventCount<=0)
							{
									/* stop it looking for events */
									NopOffset = -1;
							}
							else
							{
									/* next event */
									pCurrentEvent++;

									/* get no of nops to this event */
									NopOffset = pCurrentEvent->NopCount - EventBufferNopCount;
    
							}
						}
						while ((NopCountBefore<=(NopOffset<<16)) && 
								(NopCountAfter>(NopOffset<<16)) && (NopOffset!=-1));
					}
				}
			
				pAudioPtr = (char *)AudioEvent_UpdateCycle(pAudioPtr);
            
				NopCount.FixedPoint.L += NopsPerSampleScaled.FixedPoint.L;    
			}
		}

}

		AudioEvent_PreviousFraction = NopCount.FixedPoint.L & 0x0ffff;



		switch (AudioEvent_SampleBits)
		{
			case 8:
			{
				if (AudioEvent_SampleChannels == 2)
				{
					AudioEvent_ConvertToOutputFormat_8BitStereo((char *)pAudioBuffer2,(char *)pMixedSampleData,SamplesToGenerate);
/*					for (i=0; i<NoOfSamplesInSampleBuffer; i++)
					{
						pMixedSampleData[(i<<1)] = Digiblaster_Buffer[i];
						pMixedSampleData[(i<<1)+1] = Digiblaster_Buffer[i];
					}

*/
				}
				else
				{
/*					for (i=0; i<NoOfSamplesInSampleBuffer; i++)
					{
						unsigned char Digiblaster_DataByte;
						
						pMixedSampleData[(i<<1)] = pDigiblaster_Data;
						pMixedSampleData[(i<<1)+1] = pDigiblaster_Data;
					}
*/
					AudioEvent_ConvertToOutputFormat_8BitMono((char *)pAudioBuffer2, (char *)pMixedSampleData, SamplesToGenerate);
				}
			}
			break;

			case 16:
			{
				if (AudioEvent_SampleChannels == 2)
				{
					AudioEvent_ConvertToOutputFormat_16BitStereo((char *)pAudioBuffer2,(char *)pMixedSampleData,SamplesToGenerate);
				}
				else
				{
					AudioEvent_ConvertToOutputFormat_16BitMono((char *)pAudioBuffer2, (char *)pMixedSampleData, SamplesToGenerate);
				}
			}
			break;
		}




		/* if wave output is enabled, then dump sound data to a wav file */
/*		WavOutput_WriteBlock(pMixedSampleData, SamplesToGenerate); */
	

		/* convert PSG vol/Digiblaster data to sample data */
		AudioEvent_ConvertToOutputFormat();

		/* empty event buffer ready for next re-fill */
		AudioEvent_RestartEventBuffer(CPCNopCount);

        return CPCNopCount;
}


/***************/
/* DIGIBLASTER */
/***************/

#define NOPS_PER_SECOND 999997
/*#define DIGIBLASTER_SMOOTH */
static unsigned char Digiblaster_PreviousData = 0;
static unsigned char Digiblaster_SoundData = 0;
static unsigned long Digiblaster_CPCNopCountOfLastWrite = 0;
static unsigned long Digiblaster_PreviousFraction;
static unsigned char *pDigiblaster_Data;
static unsigned long Digiblaster_NumberOfSamplesPerNop;

void	Digiblaster_Reset(void)
{
	Digiblaster_CPCNopCountOfLastWrite = 0;
}

void	Digiblaster_Init(void)
{
	pDigiblaster_Data = Digiblaster_Buffer;

	Digiblaster_NumberOfSamplesPerNop = (SampleRate<<16)/NOPS_PER_SECOND;
}


static INLINE void	Digiblaster_Update(unsigned char Digiblaster_Data)
{
		unsigned long CurrentNopCount;
		unsigned long NopDifference;
		unsigned long NumberOfSamplesPassed;
		unsigned long CurrentFraction;
		unsigned long NumberOfSamplesToFill;

		/* get current nop count */
		CurrentNopCount = CPC_GetNopCount();

		/* number of nops between writes of different digiblaster data */
		NopDifference = CurrentNopCount - Digiblaster_CPCNopCountOfLastWrite;

		NumberOfSamplesPassed = (Digiblaster_NumberOfSamplesPerNop * NopDifference)+Digiblaster_PreviousFraction; /* 16:16 fraction */

		/* number of complete samples passed since last write */
		NumberOfSamplesToFill = (NumberOfSamplesPassed>>16);

		CurrentFraction = NumberOfSamplesPassed & 0x0ffff;

#ifdef DIGIBLASTER_SMOOTH
		if (NumberOfSamplesToFill>0)
		{
			/* at this point we have completed one or more samples */
			/* the first sample is filled with the contribution of the previous data up
			to the end of the sample time. The samples skipped are filled with the previous data,
			and the previous data ends up as far as CurrentFraction within this sample byte */

			/* add contribution of last byte written - the time this was active was up to the end of
			the current sample */
			Digiblaster_SoundData += (Digiblaster_PreviousData*((1<<16) - Digiblaster_PreviousFraction))>>16;

			/* write sound data */
			pDigiblaster_Data[0] = Digiblaster_SoundData;

			if ((NumberOfSamplesToFill-1)>0)
			{
				/* fill rest with previous data */
				memset(&pDigiblaster_Data[1], Digiblaster_PreviousData, NumberOfSamplesToFill-1);
			}

			pDigiblaster_Data += NumberOfSamplesToFill;

			/* setup data with contribution into this sample */
			Digiblaster_SoundData = (Digiblaster_PreviousData*CurrentFraction)>>16;

		}
		else
		{
			/* sound changed more than once within the same sample */

			/* add contribution from the previous value written  because now we know
			how long it was active for.
			
			Active_Time = CurrentFraction - Digiblaster_PreviousFraction */
			
			/* Data_Contribution = Data * Active_Time */
			Digiblaster_SoundData += (Digiblaster_PreviousData*(CurrentFraction - Digiblaster_PreviousFraction))>>16;

			/* next time here, we will have different data, and we can add the contribution from that */
		}
#else
	if (NumberOfSamplesToFill>0)
	{
		/* at this point we have completed one or more samples */
		/* the samples passed are filled with the previous data */

		memset(pDigiblaster_Data, Digiblaster_PreviousData, NumberOfSamplesToFill);
		pDigiblaster_Data += NumberOfSamplesToFill;
	}
#endif

		/* store current nop count */
		Digiblaster_CPCNopCountOfLastWrite = CurrentNopCount;

		/* store data written */
		Digiblaster_PreviousData = Digiblaster_Data;

		/* store the new fraction */
		Digiblaster_PreviousFraction = CurrentFraction;
}

void	Digiblaster_EndFrame(void)
{
	Digiblaster_Update(Digiblaster_PreviousData);

	pDigiblaster_Data = Digiblaster_Buffer;
}


void	Audio_Digiblaster_Write(unsigned char Digiblaster_Data)
{
/*	Digiblaster_Data ^=0x080; */

	/* data changed? */
	if (Digiblaster_Data!=Digiblaster_PreviousData)
	{
		Digiblaster_Update(Digiblaster_Data);
	}
}
					
void	AudioEvent_ConvertToOutputFormat_8BitStereo(char *pSrcBuffer, char *pDestBuffer, int Count)
{
	int i;

	char *pSrcAudioPtr = pSrcBuffer;
	char *pDestAudioPtr = pDestBuffer;

	for (i=0; i<Count; i++)
	{
		unsigned char PSG_LeftVolume, PSG_RightVolume;
		unsigned char Digiblaster_Vol = Digiblaster_Buffer[i];

		/* get left volume, get right volume */
		PSG_LeftVolume = pSrcAudioPtr[0];
		pSrcAudioPtr++;
		PSG_RightVolume = pSrcAudioPtr[0];
		pSrcAudioPtr++;
		pSrcAudioPtr+=2;

		{
			int Vol;

			Vol= PSG_LeftVolume + Digiblaster_Vol;

	/*		if (Vol>255)
			{
				Vol = 255;
			}
	*/	
			pDestAudioPtr[0] = Vol;
		}
		pDestAudioPtr++;
		{
			int Vol;

			Vol= PSG_RightVolume + Digiblaster_Vol;

	/*		if (Vol>255)
			{
				Vol = 255;
			}
	*/
			pDestAudioPtr[0] = Vol;
		}

		pDestAudioPtr++;
	}
}

