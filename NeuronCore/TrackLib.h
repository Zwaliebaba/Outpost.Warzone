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

/* The mixer's XAudio2 engine, for code that needs a voice of its own.
 *
 * FMV is the only caller. It used to create a whole second audio object for
 * itself -- DirectSound, because QMixer's had gone and there was nothing to
 * borrow -- which meant the movie soundtrack ignored the game's volume and
 * mixed outside its graph. Handing out the engine costs nothing and puts the
 * two back together. Null before sound_InitLibrary or after its shutdown, and
 * a caller that gets null plays its movie silently rather than failing.
 */
struct IXAudio2;
IXAudio2* sound_GetEngine(void);

BOOL sound_ReadTrackFromBuffer(TRACK* psTrack, void* pBuffer, UDWORD udwSize);
void sound_FreeTrack(TRACK* psTrack);

BOOL sound_Play2DSample(TRACK* psTrack, AUDIO_SAMPLE* psSample, BOOL bQueued);
BOOL sound_Play3DSample(TRACK* psTrack, AUDIO_SAMPLE* psSample);
BOOL sound_PlayStream(AUDIO_SAMPLE* psSample, char szFileName[], SDWORD iVol);
void sound_StopSample(SDWORD iSample);
void sound_StopAll(void);

int sound_GetMaxVolume(void);

/* Music, which is what replaces the CD. It has a slot of its own, so a
 * briefing playing on the stream slot neither stops it nor un-pauses it, and
 * it takes no AUDIO_SAMPLE because nothing above waits on it finishing.
 */
BOOL sound_PlayMusic(char szFileName[], SDWORD iVol, BOOL bLoop);
void sound_StopMusic(void);
void sound_PauseMusic(void);
void sound_ResumeMusic(void);

/* The two volume sliders. Both used to be lines on the Windows mixer, which
 * meant moving the whole system's volume; they are the XAudio2 graph now --
 * the master volume for one, the music slot's for the other.
 */
SDWORD sound_GetMusicVolume(void);
void sound_SetMusicVolume(SDWORD iVol);

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
