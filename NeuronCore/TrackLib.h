/***************************************************************************/
/*
 * Library-specific sound library functions;
 * these need to be re-written for each library.
 */
/***************************************************************************/

#ifndef _TRACKLIB_H_
#define _TRACKLIB_H_

/***************************************************************************/

#include "Track.h"

/***************************************************************************/

#define	KHZ22					(22050L)
#define	KHZ11					(11025L)

/***************************************************************************/
/* The interface the audio backend has to implement. Everything here is
 * reached from Track.cpp or Audio.cpp; the declarations that were not - five
 * that had no definition anywhere, and five more with no caller outside the
 * backend - have gone, along with the compressed-speech option, which was
 * switched off and has no compressed asset in the game to play.
 */

BOOL sound_InitLibrary(void);
void sound_ShutdownLibrary(void);

BOOL sound_ReadTrackFromBuffer(TRACK* psTrack, void* pBuffer, UDWORD udwSize);
void sound_FreeTrack(TRACK* psTrack);

BOOL sound_Play2DSample(TRACK* psTrack, AUDIO_SAMPLE* psSample, BOOL bQueued);
BOOL sound_Play3DSample(TRACK* psTrack, AUDIO_SAMPLE* psSample);
BOOL sound_PlayStream(AUDIO_SAMPLE* psSample, char szFileName[], SDWORD iVol);
void sound_StopSample(SDWORD iSample);
void sound_StopAll(void);

int sound_GetMaxVolume(void);

BOOL sound_QueueSamplePlaying(void);

void sound_SetPlayerPos(SDWORD iX, SDWORD iY, SDWORD iZ);
void sound_SetPlayerOrientation(SDWORD iX, SDWORD iY, SDWORD iZ);
void sound_SetObjectPosition(SDWORD iSample, SDWORD iX, SDWORD iY, SDWORD iZ);

/* Called once a frame from audio_Update. Completion notifications reach the
 * game thread here.
 */
void sound_Update(void);

/* Not the backend's: Outpost/Aud.cpp returns gameTime. */
UDWORD sound_GetGameTime(void);

/***************************************************************************/

#endif	// _TRACKLIB_H_

/***************************************************************************/
