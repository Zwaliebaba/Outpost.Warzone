/***************************************************************************/

#ifndef _TRACK_H_
#define _TRACK_H_

/***************************************************************************/
/* defines */

#ifndef MAX_STR
#define	MAX_STR			255
#endif

#define	SAMPLE_NOT_ALLOCATED	-1
#define	SAMPLE_NOT_FOUND		-3
#define	SAMPLE_COORD_INVALID	-5

#define	AUDIO_VOL_MIN			0L
#define	AUDIO_VOL_MAX			100L
#define	AUDIO_VOL_RANGE			(AUDIO_VOL_MAX-AUDIO_VOL_MIN)

/***************************************************************************/

/***************************************************************************/
/* enums */

/***************************************************************************/
/* forward definitions
 */

struct AUDIO_SAMPLE;

/***************************************************************************/
/* typedefs
 */

using AUDIO_CALLBACK = BOOL(*)(struct AUDIO_SAMPLE* psSample);

/***************************************************************************/
/* structs */

using AUDIO_SAMPLE = struct AUDIO_SAMPLE
{
  SDWORD iTrack;
  SDWORD iSample;
  SDWORD x, y, z;
  BOOL bRemove;
  AUDIO_CALLBACK pCallback;
  void* psObj;
  struct AUDIO_SAMPLE* psPrev;
  struct AUDIO_SAMPLE* psNext;
};

using TRACK = struct TRACK
{
  BOOL bLoop;
  SDWORD iVol;
  SDWORD iPriority; /* authored in audio.cfg; no reader since Phase 4 stole by distance */
  SDWORD iAudibleRadius;
  SDWORD iTime; /* duration in milliseconds */
  UDWORD iTimeLastFinished; /* time last finished in ms */
  BOOL bCompressed; /* compression data flag    */
  void* pMem; /* pointer to audio data    */
  STRING* pName; // resource name of the track
  UDWORD resID; // hashed name of the WAV
};

/***************************************************************************/
/* functions
 */

BOOL sound_Init();
BOOL sound_Shutdown();

void* sound_LoadTrackFromBuffer(UBYTE* pBuffer, UDWORD udwSize);
BOOL sound_SetTrackVals(TRACK* psTrack, BOOL bLoop, SDWORD iTrack, SDWORD iVol, SDWORD iPriority, SDWORD iAudibleRadius);
BOOL sound_ReleaseTrack(TRACK* psTrack);

void sound_StopTrack(AUDIO_SAMPLE* psSample);
void sound_CheckSample(AUDIO_SAMPLE* psSample);
void sound_CheckAllUnloaded(void);

BOOL sound_CheckTrack(SDWORD iTrack);

SDWORD sound_GetTrackAudibleRadius(SDWORD iTrack);
SDWORD sound_GetTrackVolume(SDWORD iTrack);
UDWORD sound_GetTrackHashName(SDWORD iTrack);

BOOL sound_TrackLooped(SDWORD iTrack);
SDWORD sound_TrackAudibleRadius(SDWORD iTrack);
void sound_SetCallbackFunction(void* fn);

BOOL sound_Play2DTrack(AUDIO_SAMPLE* psSample, BOOL bQueued);
BOOL sound_Play3DTrack(AUDIO_SAMPLE* psSample);
void sound_FinishedCallback(AUDIO_SAMPLE* psSample);

BOOL sound_GetSystemActive(void);
SDWORD sound_GetTrackID(TRACK* psTrack);
SDWORD sound_GetAvailableID(void);

SDWORD sound_GetGlobalVolume(void);
void sound_SetGlobalVolume(SDWORD iVol);

void sound_SetStoppedCallback(AUDIO_CALLBACK pStopTrackCallback);

UDWORD sound_GetTrackTimeLastFinished(SDWORD iTrack);
void sound_SetTrackTimeLastFinished(SDWORD iTrack, UDWORD iTime);

/***************************************************************************/

#endif	// _TRACK_H_

/***************************************************************************/
