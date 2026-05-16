/******************************************************************************
 * channel.c
 * Waveform rendering and patch script decoding.
 *-----------------------------------------------------------------------------
 * Copyright (C) 2015 - 2016 Osman Celimli
 * For conditions of distribution and use, see copyright notice in coretone.c
 ******************************************************************************/
#include <stdint.h>

#include "../types.h"
#include "coretone.h"

#include "sample.h"
#include "channel.h"
#include "music.h"

/******************************************************************************
 * !!!!----                    EXTERNS AND GLOBALS                   ----!!!!
 ******************************************************************************/
extern CoreChannel_t *aCoreChannels;
extern CorePatch_t *aCorePatches;
extern CoreTrack_t *aCoreTracks;

const char szCoreSfx_Magic[] = CORETONE_SFXPAK_HEAD_MAGICWORD;


/******************************************************************************
 * !!!!----                CHANNEL WAVEFORM RENDERING                ----!!!!
 ******************************************************************************/
/* void ct_channel_render(CoreChannel_t *pChannel, int16_t *pBuffer, const int32_t iStamp)
 *  Render the channel's output into the supplied stereo buffer, writing the
 * amplitudes directly (0 != iStamp) or summing them with the previous buffer
 * contents (0 == iStamp). Render length is always CORETONE_BUFFER_LEN samples.
 *----------------------------------------------------------------------------*/
void ct_channel_render(CoreChannel_t *pChannel, int16_t *pBuffer, const int32_t iStamp)
{
	int16_t *pCursor,*pEnd;

	int16_t sSample;
	int8p8_t sample_L,sample_R;
	int8p8_t scale_L,scale_R;

	uint32_t uiLoop;

	pCursor = pBuffer;
	pEnd = pBuffer + CORETONE_BUFFER_LEN;

	scale_L.sWhole = pChannel->cVolMain * pChannel->cVolLeft;
	scale_L.sWhole = scale_L.cPair.cHi;
	scale_R.sWhole = pChannel->cVolMain * pChannel->cVolRight;
	scale_R.sWhole = scale_R.cPair.cHi;

	switch (pChannel->eMode)
	{
		case eCHANNEL_MODE_SINGLESHOT:
			/**
			 * ---- SINGLE SHOT MODE ----
			 * Just walk through the waveform from our current position
			 * until we reach its end, then shut off the channel.
			 *
			 * No need to have separate cases for forward and backward
			 * traversal here, since wandering outside the sample bounds
			 * in either direction is considered stopping criteria.
			 */
			if(iStamp)
			{
				while(pCursor != pEnd)
				{
					sSample = pChannel->pSample[pChannel->phaseAcc.usPair.usHi];
					sample_L.sWhole = sSample * scale_L.sWhole;
					sample_R.sWhole = sSample * scale_R.sWhole;

					*(pCursor++) = sample_L.sWhole + CORETONE_BUFFER_CENTER;
					*(pCursor++) = sample_R.sWhole + CORETONE_BUFFER_CENTER;

					pChannel->phaseAcc.iWhole += pChannel->phaseAdj.iWhole;
					if(pChannel->phaseAcc.usPair.usHi >= pChannel->usSampleLen)
					{
							pChannel->eMode = eCHANNEL_MODE_OFF;
							pChannel->phaseAdj.iWhole = 0;

							while(pCursor != pEnd)
							{
								*(pCursor++) = CORETONE_BUFFER_CENTER;
								*(pCursor++) = CORETONE_BUFFER_CENTER;
							}
							break;
					}
				}
			}
			else
			{
				while(pCursor != pEnd)
				{
					sSample = pChannel->pSample[pChannel->phaseAcc.usPair.usHi];
					sample_L.sWhole = sSample * scale_L.sWhole;
					sample_R.sWhole = sSample * scale_R.sWhole;

					*(pCursor++) += sample_L.sWhole;
					*(pCursor++) += sample_R.sWhole;

					pChannel->phaseAcc.iWhole += pChannel->phaseAdj.iWhole;
					if(pChannel->phaseAcc.usPair.usHi >= pChannel->usSampleLen)
					{
							pChannel->eMode = eCHANNEL_MODE_OFF;
							pChannel->phaseAdj.iWhole = 0;
							break;
					}
				}
			}
			break;

		case eCHANNEL_MODE_LOOP:
			/**
			 * ---- LOOP MODE ----
			 * Advance our waveform phase until it has passed by our loop
			 * endpoint, upon which we'll back it up into the loop region.
			 */
			uiLoop = pChannel->usLoopEnd - pChannel->usLoopStart;
			if(iStamp)
			{
				/* ==== STAMP THE BUFFER ====
				 * We're first in, so we get to clear the buffer contents
				 * with our render results.
				 */
				if(pChannel->phaseAdj.sPair.sHi < 0)
				{
					/*-- <<<< BACKWARD SAMPLE TRAVERSAL <<<< --*/
					while(pCursor != pEnd)
					{
						sSample = pChannel->pSample[pChannel->phaseAcc.usPair.usHi];
						sample_L.sWhole = sSample * scale_L.sWhole;
						sample_R.sWhole = sSample * scale_R.sWhole;

						*(pCursor++) = sample_L.sWhole + CORETONE_BUFFER_CENTER;
						*(pCursor++) = sample_R.sWhole + CORETONE_BUFFER_CENTER;

						pChannel->phaseAcc.iWhole += pChannel->phaseAdj.iWhole;
						while(pChannel->phaseAcc.usPair.usHi < pChannel->usLoopStart)
						{
								pChannel->phaseAcc.usPair.usHi += uiLoop;
						}
					}
				}
				else
				{
					/*-- >>>> FORWARD SAMPLE TRAVERSAL >>>> --*/
					while(pCursor != pEnd)
					{
						sSample = pChannel->pSample[pChannel->phaseAcc.usPair.usHi];
						sample_L.sWhole = sSample * scale_L.sWhole;
						sample_R.sWhole = sSample * scale_R.sWhole;

						*(pCursor++) = sample_L.sWhole + CORETONE_BUFFER_CENTER;
						*(pCursor++) = sample_R.sWhole + CORETONE_BUFFER_CENTER;

						pChannel->phaseAcc.iWhole += pChannel->phaseAdj.iWhole;
						while(pChannel->phaseAcc.usPair.usHi >= pChannel->usLoopEnd)
						{
								pChannel->phaseAcc.usPair.usHi -= uiLoop;
						}
					}
				}
			}
			else
			{
				/* ++++ ACCUMULATE WITH BUFFER CONTENTS +++++
				 * Someone's already marked the buffer, so we need to mix with
				 * their work rather than stomping over it.
				 */
				if(pChannel->phaseAdj.sPair.sHi < 0)
				{
					/*-- <<<< BACKWARD SAMPLE TRAVERSAL <<<< --*/
					while(pCursor != pEnd)
					{
						sSample = pChannel->pSample[pChannel->phaseAcc.usPair.usHi];
						sample_L.sWhole = sSample * scale_L.sWhole;
						sample_R.sWhole = sSample * scale_R.sWhole;

						*(pCursor++) += sample_L.sWhole;
						*(pCursor++) += sample_R.sWhole;

						pChannel->phaseAcc.iWhole += pChannel->phaseAdj.iWhole;
						while(pChannel->phaseAcc.usPair.usHi < pChannel->usLoopStart)
						{
								pChannel->phaseAcc.usPair.usHi += uiLoop;
						}
					}
				}
				else
				{
					/*-- >>>> FORWARD SAMPLE TRAVERSAL >>>> --*/
					while(pCursor != pEnd)
					{
						sSample = pChannel->pSample[pChannel->phaseAcc.usPair.usHi];
						sample_L.sWhole = sSample * scale_L.sWhole;
						sample_R.sWhole = sSample * scale_R.sWhole;

						*(pCursor++) += sample_L.sWhole;
						*(pCursor++) += sample_R.sWhole;

						pChannel->phaseAcc.iWhole += pChannel->phaseAdj.iWhole;
						while(pChannel->phaseAcc.usPair.usHi >= pChannel->usLoopEnd)
						{
								pChannel->phaseAcc.usPair.usHi -= uiLoop;
						}
					}
				}
			}
			break;
		case eCHANNEL_MODE_OFF:
		case eCHANNEL_MODE_FOOTER:
			break;
	}
}




/******************************************************************************
 * !!!!----                PATCH STATE RECALCULATION                 ----!!!!
 ******************************************************************************/
/* void ct_patch_recalc(CorePatch_t *pPatch)
 *  Calculate the next state of a channel's frequency and volume based
 * upon its currently configured patch. Should be called once per tick.
 *----------------------------------------------------------------------------*/
void ct_patch_recalc(CorePatch_t *pPatch)
{
	CoreChannel_t *pChannel = pPatch->pChannel;
	
	/**
	 * Final Phase Adjustment = Base + Pitch + Offset
	 * Pitch += Pitch Adjustment
	 * Offset += Offset Adjustment
	 */
	pPatch->freqPitch.iWhole += pPatch->pitchAdj.iWhole;
	pPatch->freqOffset.iWhole += pPatch->offsetAdj.iWhole;

	pChannel->phaseAdj.iWhole = (pPatch->iInstrument)
		? (pPatch->freqBase.iWhole + pPatch->freqPitch.iWhole + pPatch->freqOffset.iWhole)
		: pPatch->freqOffset.iWhole;

	/**
	 * Volume is just a straight 8.8 accumulation:
	 * Current += Adjustment
	 */
	pPatch->volCur.sWhole += pPatch->volAdj.sWhole;

	pChannel->cVolMain = pPatch->volCur.cPair.cHi;
}




/******************************************************************************
 * !!!!----                  PATCH SCRIPT COMMANDS                   ----!!!!
 ******************************************************************************/
/* CORETONE_PATCH_END()
 *----------------------------------------------------------------------------*/
void ct_patchCom_end(CorePatch_t *pPatch, CoreChannel_t *pChannel)
{
	pChannel->eMode = eCHANNEL_MODE_OFF;
	pPatch->iInstrument = 0;
	pPatch->iPriority = 0;
}

/* CORETONE_PATCH_MODE_SINGLESHOT()
 *----------------------------------------------------------------------------*/
void ct_patchCom_modeSingle(CorePatch_t *pPatch, CoreChannel_t *pChannel)
{
	pChannel->eMode = eCHANNEL_MODE_SINGLESHOT;
}

/* CORETONE_PATCH_MODE_LOOP(usLoopStart, usLoopEnd)
 *----------------------------------------------------------------------------*/
void ct_patchCom_modeLoop(CorePatch_t *pPatch, CoreChannel_t *pChannel)
{
	int8p8_t loopStart,loopEnd;

	loopStart.ucPair.ucLo = pPatch->pScript[pPatch->uiOffset++];
	loopStart.ucPair.ucHi = pPatch->pScript[pPatch->uiOffset++];
	loopEnd.ucPair.ucLo = pPatch->pScript[pPatch->uiOffset++];
	loopEnd.ucPair.ucHi = pPatch->pScript[pPatch->uiOffset++];

	pChannel->eMode = eCHANNEL_MODE_LOOP;
	pChannel->usLoopStart = loopStart.usWhole;
	pChannel->usLoopEnd = loopEnd.usWhole;
}

/* CORETONE_PATCH_VOLUME(cVol, cAdj_Lo, cAdj_Hi)
 *----------------------------------------------------------------------------*/
void ct_patchCom_vol(CorePatch_t *pPatch, CoreChannel_t *pChannel)
{
	pPatch->volCur.ucPair.ucLo = 0;
	pPatch->volCur.ucPair.ucHi = pPatch->pScript[pPatch->uiOffset++];
	pPatch->volAdj.ucPair.ucLo = pPatch->pScript[pPatch->uiOffset++];
	pPatch->volAdj.ucPair.ucHi = pPatch->pScript[pPatch->uiOffset++];
}

/* CORETONE_PATCH_FREQUENCY(sOffset_Lo, sOffset_Hi, sAdj_Lo, sAdj_Hi)
 *----------------------------------------------------------------------------*/
void ct_patchCom_freq(CorePatch_t *pPatch, CoreChannel_t *pChannel)
{
	int8p8_t fetchFreq;

	fetchFreq.ucPair.ucLo = pPatch->pScript[pPatch->uiOffset++];
	fetchFreq.ucPair.ucHi = pPatch->pScript[pPatch->uiOffset++];
	pPatch->freqOffset.usPair.usLo = fetchFreq.usWhole;
	fetchFreq.ucPair.ucLo = pPatch->pScript[pPatch->uiOffset++];
	fetchFreq.ucPair.ucHi = pPatch->pScript[pPatch->uiOffset++];
	pPatch->freqOffset.usPair.usHi = fetchFreq.usWhole;

	fetchFreq.ucPair.ucLo = pPatch->pScript[pPatch->uiOffset++];
	fetchFreq.ucPair.ucHi = pPatch->pScript[pPatch->uiOffset++];
	pPatch->offsetAdj.usPair.usLo = fetchFreq.usWhole;
	fetchFreq.ucPair.ucLo = pPatch->pScript[pPatch->uiOffset++];
	fetchFreq.ucPair.ucHi = pPatch->pScript[pPatch->uiOffset++];
	pPatch->offsetAdj.usPair.usHi = fetchFreq.usWhole;
}

/* CORETONE_PATCH_LOOP_START(cCount)
 *----------------------------------------------------------------------------*/
void ct_patchCom_loopStart(CorePatch_t *pPatch, CoreChannel_t *pChannel)
{
	int8_t cCount;

	if(pPatch->uiStackPos < CORETONE_PATCH_STACKDEPTH)
	{
		cCount = pPatch->pScript[pPatch->uiOffset++];
		pPatch->aiLoopStack[pPatch->uiStackPos] = cCount;
		pPatch->auiAddrStack[pPatch->uiStackPos] = pPatch->uiOffset;

		pPatch->uiStackPos++;
	}
}

/* CORETONE_PATCH_LOOP_END()
 *----------------------------------------------------------------------------*/
void ct_patchCom_loopEnd(CorePatch_t *pPatch, CoreChannel_t *pChannel)
{
	uint32_t uiX;

	if(pPatch->uiStackPos > 0)
	{
		uiX = pPatch->uiStackPos - 1;

		if((pPatch->aiLoopStack[uiX] >= 0) && (pPatch->aiLoopStack[uiX] < 2))
		{
			/**
			 * Loop counts of zero or one will allow the decoder to
			 * proceed past the loop end marker.
			 */
			pPatch->uiStackPos = uiX;
		}
		else if(pPatch->aiLoopStack[uiX] < 0)
		{
			/**
			 * Loop counts less than zero are considered infinite
			 * and will always cause the decoder to wrap back.
			 */
			pPatch->uiOffset = pPatch->auiAddrStack[uiX];
		}
		else
		{
			/**
			 * Loop counts of two or greater will cause the decoder
			 * to wrap back and decrement their count until they
			 * eventually reach the first case of this triad.
			 */
			pPatch->uiOffset = pPatch->auiAddrStack[uiX];
			pPatch->aiLoopStack[uiX]--;
		}
	}
}

/* CORETONE_PATCH_NOP()
 *----------------------------------------------------------------------------*/
void ct_patchCom_nop(CorePatch_t *pPatch, CoreChannel_t *pChannel)
{

}




/******************************************************************************
 * !!!!----                   PATCH COMMAND TABLE                    ----!!!!
 *----------------------------------------------------------------------------*/
typedef void (*ct_patchCom_t)(CorePatch_t *pPatch, CoreChannel_t *pChannel);
ct_patchCom_t aPatchComs[] =
{
	ct_patchCom_end,
	ct_patchCom_modeSingle, ct_patchCom_modeLoop,
	ct_patchCom_vol, ct_patchCom_freq,

	ct_patchCom_loopStart, ct_patchCom_loopEnd,
	ct_patchCom_nop
};

/******************************************************************************
 * !!!!----                  PATCH SCRIPT DECODING                   ----!!!!
 ******************************************************************************/
/* void ct_patch_keyOn(CorePatch_t *pPatch)
 *  Reset waveform and patch decoding parameters for the given channel, should
 * only be called once the given channel and patch have had their waveform,
 * script, and priority configured.
 *----------------------------------------------------------------------------*/
void ct_patch_keyOn(CorePatch_t *pPatch)
{
	CoreChannel_t *pChannel = pPatch->pChannel;

	pChannel->eMode = eCHANNEL_MODE_OFF;
	pChannel->phaseAcc.iWhole = 0;
	
	pPatch->freqOffset.iWhole = 0;
	pPatch->offsetAdj.iWhole = 0;
	pPatch->volCur.sWhole = 0;
	pPatch->volAdj.sWhole = 0;

	pPatch->uiOffset = 0;
	pPatch->uiStackPos = 0;
	pPatch->uiDel = 0;
}

/* void ct_patch_keyOff(CorePatch_t *pPatch)
 *  If the current patch is an instrument, the script decoder will be sent to
 * the note off portion of the patch. If the current patch is a sound effect
 * it will be terminated outright.
 *----------------------------------------------------------------------------*/
void ct_patch_keyOff(CorePatch_t *pPatch)
{
	CoreChannel_t *pChannel = pPatch->pChannel;

	if(pPatch->iInstrument)
	{
		pPatch->uiOffset = pPatch->uiNoteOff;
		pPatch->uiStackPos = 0;
		pPatch->uiDel = 0;
	}
	else
	{
		pChannel->eMode = eCHANNEL_MODE_OFF;
		pPatch->iPriority = 0;
	}
}

/* void ct_patch_decode(CorePatch_t *pPatch)
 *  Walk through the patch script if the given channel descriptors are both
 * active (have a nonzero priority) and have no pending delays.
 *----------------------------------------------------------------------------*/
void ct_patch_decode(CorePatch_t *pPatch)
{
	CoreChannel_t *pChannel = pPatch->pChannel;
	uint8_t *pScript;
	uint8_t ucByte;

	uint32_t uiX,uiY;

	/**
	 * We'll only decode the next command in a patch script if the channel is
	 * actually enabled (nonzero priority) and has no currently active delays.
	 */
	pScript = pPatch->pScript;
	while((0 != pPatch->iPriority) && (0 == pPatch->uiDel))
	{
		ucByte = pScript[pPatch->uiOffset];
		
		if(ucByte & CORETONE_PATCH_WAIT)
		{
			/**
			 * Delay commands are a special case and operate similarly to MIDI's
			 * varlength delays. Any command byte with its MSB set is interpreted
			 * as the start of a delay string which continues until a command
			 * with its MSB clear is encountered.
			 *
			 * The seven remaining bits of each byte in the delay string are used
			 * as the delay count itself, and are shifted into bits 7-0, 14-8,
			 * 21-15, or 28-22 of the accumulated delay count.
			 */
			for((uiX = 0, uiY = 0);
				((ucByte & CORETONE_PATCH_WAIT) && (uiY < sizeof(uint32_t)));
				(uiX += 7, uiY++))
			{
				pPatch->uiDel |= ((ucByte & CORETONE_PATCH_WAIT_MASK) << uiX);

				pPatch->uiOffset++;
				ucByte = pScript[pPatch->uiOffset];
			}
		}
		else
		{
			/**
			 * Normal commands are just called directly after
			 * incrementing the decoder offset.
			 */
			pPatch->uiOffset++;
			if(ucByte < CORETONE_MUSIC_FOOTER)
			{
				(*(aPatchComs[ucByte]))(pPatch, pChannel);
			}
			else
			{
				pChannel->eMode = eCHANNEL_MODE_OFF;
				pPatch->iPriority = 0;
			}
		}
	}

	if(0 != pPatch->iPriority)
	{
		pPatch->uiDel--;
	}
}




/******************************************************************************
 * !!!!----                 SOUND EFFECT MANAGEMENT                  ----!!!!
 ******************************************************************************/
/* void ct_sfx_dispatch(uint8_t *pSFX, int8_t cPriority, int8_t cVol_Left, int8_t cVol_Right)
 *  Dispatch the sound effect pSFX with the supplied priority and panning on
 * any channels which are currently idle or occupied by a less important patch.
 *----------------------------------------------------------------------------*/
void ct_sfx_dispatch(uint8_t *pSFX, int8_t cPriority, int8_t cVol_Left, int8_t cVol_Right)
{
	CoreChannel_t *pChannel;
	CorePatch_t *pPatch;

	uint32_t *pDir;
	uint8_t *pScript;

	uint32_t uiChannels;
	uint32_t uiSample,uiLen;
	uint32_t uiX,uiY;

	/**
	 * Ensure the sound effect actually has a valid header and channel count
	 * larger than zero before proceeding...
	 */
	for(uiX = 0; uiX < CORETONE_SFXPAK_HEAD_MAGICLEN; uiX++)
	{
		if(szCoreSfx_Magic[uiX] != pSFX[uiX]) return;
	}

	uiChannels = *((uint32_t*)(pSFX + CORETONE_SFXPAK_HEAD_COUNT));
	if(0 == uiChannels) return;

	/**
	 * As instruments are usually played upward from channel zero, we'll be
	 * dispatching sound effects downward from the last channel. The available
	 * channel scan is broken into three passes :
	 *
	 * 1) Channels which are idle and have no associated music tracks.
	 * 2) Channels with sound effects of lesser importance.
	 * 3) Channels with any patch of lesser importance.
	 *
	 * Essentially, we're trying our best to not interfere with any music
	 * unless it is absolutely necessary.
	 */
	pDir = (uint32_t*)(pSFX + CORETONE_SFXPAK_DIR_BASE);
	uiX = 0;
	while(uiX < uiChannels)
	{
		for(uiY = (CORETONE_CHANNELS - 1); uiY != 0xFFFFFFFF; uiY--)
		{
			if((0 == aCorePatches[uiY].iPriority)
				&& (0 == aCoreTracks[uiY].iPriority)) goto dispatch;
		}
		for(uiY = (CORETONE_CHANNELS - 1); uiY != 0xFFFFFFFF; uiY--)
		{
			if((cPriority > aCorePatches[uiY].iPriority)
				&& (0 == aCoreTracks[uiY].iPriority)) goto dispatch;
		}
		for(uiY = (CORETONE_CHANNELS - 1); uiY != 0xFFFFFFFF; uiY--)
		{
			if(cPriority > aCorePatches[uiY].iPriority) goto dispatch;
		}

		/**
		 * If all three of the searches above have failed, the channels are
		 * completely saturated and there's no point in trying to enqueue
		 * any additional patches this sound effect may have.
		 */
		break;

		/**
		 * However, dispatching means there might be one channel left, so
		 * we'll let the search continue after taking over the slot.
		 */
	dispatch:
		pPatch = aCorePatches + uiY;
		pChannel = aCoreChannels + uiY;
		uiSample = pDir[CORETONE_SFXPAK_ENTRY_SAMPLE];
		pScript = pSFX + pDir[CORETONE_SFXPAK_ENTRY_SCRIPT];

		pPatch->iPriority = cPriority;
		pPatch->iInstrument = 0;
		pPatch->pScript = pScript;
		pPatch->uiNoteOff = 0;

		ct_sample_get(uiSample, &(pChannel->pSample), &uiLen);
		pChannel->usSampleLen = uiLen;
		ct_patch_keyOn(pPatch);
		pChannel->cVolLeft = cVol_Left;
		pChannel->cVolRight = cVol_Right;

		pDir += CORETONE_SFXPAK_ENTRY_LEN;
		uiX++;
	}
}

/* void ct_sfx_stop(int8_t cPriority)
 *  Cease playback of any currently decoding sound effects with the priority
 * cPriority, instruments with this priority will be left alone.
 *----------------------------------------------------------------------------*/
void ct_sfx_stop(int8_t cPriority)
{
	uint32_t uiX;

	for(uiX = 0; uiX < CORETONE_CHANNELS; uiX++)
	{
		if(!(aCorePatches[uiX].iInstrument)
			&& (cPriority == aCorePatches[uiX].iPriority))
		{
			ct_patch_keyOff(aCorePatches + uiX);
		}
	}
}
