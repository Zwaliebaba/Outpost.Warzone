/***************************************************************************/

#ifndef _AUDIO_H_
#define _AUDIO_H_

/***************************************************************************/

#include "Track.h"

/***************************************************************************/

extern BOOL audio_Init(HWND hWnd, BOOL bEnabled, AUDIO_CALLBACK pStopTrackCallback);
extern BOOL audio_Update();
extern BOOL audio_Shutdown();
extern BOOL audio_Disabled(void);

extern void* audio_LoadTrackFromBuffer(UBYTE* pBuffer, UDWORD udwSize);
extern BOOL audio_SetTrackVals(char szFileName[], BOOL bLoop, int* piID, int iVol, int iPriority, int iAudibleRadius, int VagID);
extern BOOL audio_SetTrackValsHashName(UDWORD hash, BOOL bLoop, int iTrack, int iVol, int iPriority, int iAudibleRadius, int VagID);
extern void audio_ReleaseTrack(TRACK* psTrack);

extern BOOL audio_PlayStaticTrack(SDWORD iX, SDWORD iY, int iTrack);
extern BOOL audio_PlayObjStaticTrack(void* psObj, int iTrack);
extern BOOL audio_PlayObjStaticTrackCallback(void* psObj, int iTrack, AUDIO_CALLBACK pUserCallback);
extern BOOL audio_PlayObjDynamicTrack(void* psObj, int iTrack, AUDIO_CALLBACK pUserCallback);
extern void audio_StopObjTrack(void* psObj, int iTrack);
extern void audio_PlayTrack(int iTrack);
extern BOOL audio_PlayStream(char szFileName[], SDWORD iVol, AUDIO_CALLBACK pUserCallback);
extern void audio_QueueTrack(SDWORD iTrack);
extern void audio_QueueTrackMinDelay(SDWORD iTrack, UDWORD iMinDelay);
extern void audio_QueueTrackMinDelayPos(SDWORD iTrack, UDWORD iMinDelay, SDWORD iX, SDWORD iY, SDWORD iZ);
extern void audio_QueueTrackPos(SDWORD iTrack, SDWORD iX, SDWORD iY, SDWORD iZ);
extern void audio_PlayPreviousQueueTrack(void);
extern BOOL audio_GetPreviousQueueTrackPos(SDWORD* iX, SDWORD* iY, SDWORD* iZ);

extern void audio_StopAll(void);
extern void audio_CheckAllUnloaded(void);

extern SDWORD audio_GetTrackID(char szFileName[]);
extern SDWORD audio_GetTrackIDFromHash(UDWORD hash);
extern SDWORD audio_GetAvailableID(void);
extern SDWORD audio_GetMixVol(SDWORD iVol);
extern SDWORD audio_GetSampleMixVol(AUDIO_SAMPLE* psSample, SDWORD iVol, BOOL bScale3D);

extern SDWORD audio_Get3DVolume(void);
extern void audio_Set3DVolume(SDWORD iVol);

/***************************************************************************/

#endif	// _AUDIO_H_

/***************************************************************************/
