#include "pch.h"

#include "Frame.h"
#include "GTime.h"
#include "TrackLib.h"
#include "Priority.h"
#include "Aud.h"
#include "Trig.h"

/***************************************************************************/
/* defines */

#define	NO_SAMPLE				-2

#define	MAX_SAME_SAMPLES		2

#define	LOWERED_VOL				AUDIO_VOL_MAX/4

/***************************************************************************/
/* structs */

using SDWVEC3D = struct SDWVEC3D
{
  SDWORD x, y, z;
};

/***************************************************************************/
/* externs */

/***************************************************************************/
/* static functions */

/***************************************************************************/
/* global variables */

/* Game-thread only. QMixer delivered completion callbacks on its own thread,
 * which is what the critical section that used to guard these lists was for;
 * since Phase 4 the backend confines the audio thread to a completion mask it
 * drains in sound_Update, so everything here runs on one thread.
 */
static AUDIO_SAMPLE* g_psSampleList = nullptr;
static AUDIO_SAMPLE* g_psSampleQueue = nullptr;

static BOOL g_bAudioEnabled = FALSE;
static BOOL g_bAudioPaused = FALSE;
static BOOL g_bStopAll = FALSE;

static AUDIO_SAMPLE g_sPreviousSample = {NO_SAMPLE, SAMPLE_COORD_INVALID, SAMPLE_COORD_INVALID, SAMPLE_COORD_INVALID};

static SDWORD g_i3DVolume = AUDIO_VOL_MAX;

/***************************************************************************/

BOOL audio_Disabled(void) { return !g_bAudioEnabled; }

/***************************************************************************/

BOOL audio_Init(BOOL bEnabled, AUDIO_CALLBACK pStopTrackCallback)
{
  /* if audio not enabled return TRUE to carry on game without audio */
  if (bEnabled == FALSE)
  {
    g_bAudioEnabled = FALSE;
    return TRUE;
  }

  /* init audio system */
  g_bAudioEnabled = sound_Init();

  if (g_bAudioEnabled == TRUE)
  {
    sound_SetStoppedCallback(pStopTrackCallback);

    return TRUE;
  }
  return FALSE;
}

/***************************************************************************/

BOOL audio_Shutdown()
{
  AUDIO_SAMPLE *psSample = nullptr, *psSampleTemp = nullptr;
  BOOL bOK;

  /* if audio not enabled return TRUE to carry on game without audio */
  if (g_bAudioEnabled == FALSE)
    return TRUE;

  sound_StopAll();

  bOK = sound_Shutdown();

  /* empty sample list */
  psSample = g_psSampleList;
  while (psSample != nullptr)
  {
    psSampleTemp = psSample->psNext;
    delete psSample;
    psSample = psSampleTemp;
  }

  /* empty sample queue */
  psSample = g_psSampleQueue;
  while (psSample != nullptr)
  {
    psSampleTemp = psSample->psNext;
    delete psSample;
    psSample = psSampleTemp;
  }

  g_psSampleList = nullptr;
  g_psSampleQueue = nullptr;

  return bOK;
}

/***************************************************************************/

BOOL audio_GetPreviousQueueTrackPos(SDWORD* iX, SDWORD* iY, SDWORD* iZ)
{
  if (g_sPreviousSample.x != SAMPLE_COORD_INVALID && g_sPreviousSample.y != SAMPLE_COORD_INVALID && g_sPreviousSample.z !=
    SAMPLE_COORD_INVALID)
  {
    *iX = g_sPreviousSample.x;
    *iY = g_sPreviousSample.y;
    *iZ = g_sPreviousSample.z;
    return TRUE;
  }

  /* this used to read the caller's uninitialised *iZ into the other two */
  *iX = *iY = *iZ = 0;
  return FALSE;
}

/***************************************************************************/

static void audio_AddSampleToHead(AUDIO_SAMPLE** ppsSampleList, AUDIO_SAMPLE* psSample)
{
  psSample->psNext = (*ppsSampleList);
  psSample->psPrev = nullptr;
  if ((*ppsSampleList) != nullptr)
    (*ppsSampleList)->psPrev = psSample;
  (*ppsSampleList) = psSample;
}

/***************************************************************************/

static void audio_AddSampleToTail(AUDIO_SAMPLE** ppsSampleList, AUDIO_SAMPLE* psSample)
{
  AUDIO_SAMPLE* psSampleTail = nullptr;

  if ((*ppsSampleList) == nullptr)
    (*ppsSampleList) = psSample;
  else
  {
    psSampleTail = (*ppsSampleList);
    while (psSampleTail->psNext != nullptr) { psSampleTail = psSampleTail->psNext; }
    psSampleTail->psNext = psSample;
    psSample->psPrev = psSampleTail;
    psSample->psNext = nullptr;
  }
}

/***************************************************************************/
/*
 * audio_RemoveSample
 *
 * Removes sample from list but doesn't free its memory
 */
/***************************************************************************/

static void audio_RemoveSample(AUDIO_SAMPLE** ppsSampleList, AUDIO_SAMPLE* psSample)
{
  if (psSample == nullptr)
    return;

  if (psSample == (*ppsSampleList))
  {
    /* first sample in list */
    (*ppsSampleList) = psSample->psNext;
  }
  else
  {
    if (psSample->psPrev != nullptr)
      psSample->psPrev->psNext = psSample->psNext;
    if (psSample->psNext != nullptr)
      psSample->psNext->psPrev = psSample->psPrev;
  }

  /* set sample pointers NULL for safety */
  psSample->psPrev = nullptr;
  psSample->psNext = nullptr;
}

/***************************************************************************/

static BOOL audio_CheckSameQueueTracksPlaying(SDWORD iTrack)
{
  SDWORD iCount;
  AUDIO_SAMPLE* psSample = nullptr;
  BOOL bOK = TRUE;

  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE || g_bAudioPaused == TRUE)
    return TRUE;

  iCount = 0;

  /* loop through queue sounds and check whether too many already in it */
  psSample = g_psSampleQueue;
  while (psSample != nullptr)
  {
    if (psSample->iTrack == iTrack)
      iCount++;

    if (iCount > MAX_SAME_SAMPLES)
    {
      bOK = FALSE;
      break;
    }

    psSample = psSample->psNext;
  }

  return bOK;
}

/***************************************************************************/

AUDIO_SAMPLE* audio_QueueSample(SDWORD iTrack)
{
  AUDIO_SAMPLE* psSample = nullptr;

  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE || g_bAudioPaused == TRUE || g_bStopAll == TRUE)
    return nullptr;

  DEBUG_ASSERT_TEXT(sound_CheckTrack( iTrack ) == TRUE, "audio_QueueSample: track {} outside limits\n", iTrack);

  /* reject track if too many of same ID already in queue */
  if (audio_CheckSameQueueTracksPlaying(iTrack) == FALSE)
    return nullptr;

  psSample = new (std::nothrow) AUDIO_SAMPLE;

  if (psSample != nullptr)
  {
    memset(psSample, 0, sizeof(AUDIO_SAMPLE));
    psSample->iTrack = iTrack;
    psSample->x = SAMPLE_COORD_INVALID;
    psSample->y = SAMPLE_COORD_INVALID;
    psSample->z = SAMPLE_COORD_INVALID;
    psSample->bRemove = FALSE;

    /* add to queue */
    audio_AddSampleToTail(&g_psSampleQueue, psSample);
  }

  return psSample;
}

/***************************************************************************/

void audio_QueueTrack(SDWORD iTrack)
{
  AUDIO_SAMPLE* psSample = nullptr;

  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE || g_bAudioPaused == TRUE || g_bStopAll == TRUE)
    return;

  psSample = audio_QueueSample(iTrack);
}

/***************************************************************************/
/*
 * audio_QueueTrackMinDelay
 *
 * Will only play track if iMinDelay has elapsed since track last finished
 */
/***************************************************************************/

void audio_QueueTrackMinDelay(SDWORD iTrack, UDWORD iMinDelay)
{
  AUDIO_SAMPLE* psSample = nullptr;
  UDWORD iDelay;

  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE || g_bAudioPaused == TRUE)
    return;

  iDelay = sound_GetGameTime() - sound_GetTrackTimeLastFinished(iTrack);

  if (iDelay > iMinDelay)
  {
    psSample = audio_QueueSample(iTrack);

    if (psSample != nullptr)
      sound_SetTrackTimeLastFinished(iTrack, sound_GetGameTime());
  }
}

/***************************************************************************/

void audio_QueueTrackMinDelayPos(SDWORD iTrack, UDWORD iMinDelay, SDWORD iX, SDWORD iY, SDWORD iZ)
{
  AUDIO_SAMPLE* psSample = nullptr;
  UDWORD iDelay;

  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE || g_bAudioPaused == TRUE)
    return;

  iDelay = sound_GetGameTime() - sound_GetTrackTimeLastFinished(iTrack);

  if (iDelay > iMinDelay)
  {
    psSample = audio_QueueSample(iTrack);

    if (psSample != nullptr)
    {
      sound_SetTrackTimeLastFinished(iTrack, sound_GetGameTime());
      psSample->x = iX;
      psSample->y = iY;
      psSample->z = iZ;
    }
  }
}

/***************************************************************************/

void audio_QueueTrackPos(SDWORD iTrack, SDWORD iX, SDWORD iY, SDWORD iZ)
{
  AUDIO_SAMPLE* psSample = nullptr;

  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE || g_bAudioPaused == TRUE)
    return;

  psSample = audio_QueueSample(iTrack);
  if (psSample != nullptr)
  {
    psSample->x = iX;
    psSample->y = iY;
    psSample->z = iZ;
  }
}

/***************************************************************************/

void audio_UpdateQueue(void)
{
  AUDIO_SAMPLE* psSample = nullptr;

  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE || g_bAudioPaused == TRUE)
    return;

  if (sound_QueueSamplePlaying() == TRUE)
  {
    /* lower volume whilst playing queue audio */
    audio_Set3DVolume(LOWERED_VOL);
  }
  else
  {
    /* set full global volume */
    audio_Set3DVolume(AUDIO_VOL_MAX);

    /* check queue for members */
    if (g_psSampleQueue != nullptr)
    {
      /* remove queue head */
      psSample = g_psSampleQueue;
      audio_RemoveSample(&g_psSampleQueue, psSample);

      /* add sample to list if able to play */
      if (sound_Play2DTrack(psSample, TRUE) == TRUE)
      {
        audio_AddSampleToHead(&g_psSampleList, psSample);

        /* update last queue sound coords */
        if (psSample->x != SAMPLE_COORD_INVALID && psSample->y != SAMPLE_COORD_INVALID && psSample->z != SAMPLE_COORD_INVALID)
        {
          g_sPreviousSample.x = psSample->x;
          g_sPreviousSample.y = psSample->y;
          g_sPreviousSample.z = psSample->z;
        }
      }
      else
      {
        Neuron::DebugTrace("audio_UpdateQueue: couldn't play sample\n");
        delete psSample;
      }
    }
  }
}

/***************************************************************************/

BOOL audio_Update(void)
{
  SDWVEC3D vecPlayer;
  SDWORD iA;
  AUDIO_SAMPLE *psSample, *psSampleTemp;

  /* if audio not enabled return TRUE to carry on game without audio */
  if (g_bAudioEnabled == FALSE)
    return TRUE;

  audio_UpdateQueue();

  /* get player position */
  if (audio_Display3D() == TRUE)
    audio_Get3DPlayerPos(&vecPlayer.x, &vecPlayer.y, &vecPlayer.z);
  else
    audio_Get2DPlayerPos(&vecPlayer.x, &vecPlayer.y, &vecPlayer.z);

  sound_SetPlayerPos(vecPlayer.x, vecPlayer.y, vecPlayer.z);

  audio_Get3DPlayerRotAboutVerticalAxis(&iA);
  sound_SetPlayerOrientation(0, 0, iA);

  /* loop through 3D sounds and remove if finished or update position */
  psSample = g_psSampleList;
  while (psSample != nullptr)
  {
    /* remove finished samples from list */
    if (psSample->bRemove == TRUE)
    {
      audio_RemoveSample(&g_psSampleList, psSample);
      psSampleTemp = psSample->psNext;
      delete psSample;
      psSample = psSampleTemp;
    }
    /* check looping sound callbacks for finished condition */
    else
    {
      if (psSample->psObj != nullptr)
      {
        if (audio_ObjectDead(psSample->psObj) || (psSample->pCallback != nullptr && (psSample->pCallback)(psSample) == FALSE))
        {
          sound_StopTrack(psSample);
          psSample->psObj = nullptr;
        }
        else
        {
          /* update sample position */
          {
            audio_GetObjectPos(psSample->psObj, &psSample->x, &psSample->y, &psSample->z);
            sound_SetObjectPosition(psSample->iSample, psSample->x, psSample->y, psSample->z);
          }
        }
      }

      /* next sample */
      psSample = psSample->psNext;
    }
  }

  sound_Update();

  return TRUE;
}

/***************************************************************************/

void* audio_LoadTrackFromBuffer(UBYTE* pBuffer, UDWORD udwSize)
{
  /* if audio not enabled return TRUE to carry on game without audio */
  if (g_bAudioEnabled == FALSE)
    return nullptr;

  return sound_LoadTrackFromBuffer(pBuffer, udwSize);
}

/***************************************************************************/

// Routine to convert wav filename into a track number
//
//  ... This is really not going to be practical on the PSX is it?
//
//    What is the point of all the scripts storing .WAV file names like: "Incoming Intelligence Report-22hz Mon.wav"
//	when all that is required is a single byte saying which sound effect is required.
//
//  A typical example of using 50 times the amount of memory that is needed.
//   ... bloody PC programmers they're spoiled !
//
//  
//
BOOL audio_SetTrackVals(char szFileName[], BOOL bLoop, int* piID, int iVol, int iPriority, int iAudibleRadius)
{
  TRACK* psTrack;

  /* if audio not enabled return TRUE to carry on game without audio */
  if (g_bAudioEnabled == FALSE)
    return TRUE;

  /* get track pointer from resource */
  psTrack = static_cast<TRACK*>(resGetData("WAV", szFileName));

  if (psTrack == nullptr)
  {
    Neuron::DebugTrace("audio_SetTrackVals: track {} resource not found\n", szFileName);
    return FALSE;
  }
  /* get current ID or spare one */
  if (audio_GetIDFromStr(szFileName, piID) == FALSE)
    *piID = sound_GetAvailableID();

  if (*piID == SAMPLE_NOT_ALLOCATED)
  {
    Neuron::DebugTrace("audio_SetTrackVals: couldn't get spare track ID\n");
    return FALSE;
  }
  return sound_SetTrackVals(psTrack, bLoop, *piID, iVol, iPriority, iAudibleRadius);
}

/***************************************************************************/

BOOL audio_SetTrackValsHashName(UDWORD hash, BOOL bLoop, int iTrack, int iVol, int iPriority, int iAudibleRadius)
{
  TRACK* psTrack;

  /* if audio not enabled return TRUE to carry on game without audio */
  if (g_bAudioEnabled == FALSE)
    return TRUE;

  /* get track pointer from resource */
  psTrack = static_cast<TRACK*>(resGetDataFromHash("WAV", hash));

  if (psTrack == nullptr)
    return FALSE;
  return sound_SetTrackVals(psTrack, bLoop, iTrack, iVol, iPriority, iAudibleRadius);
}

/***************************************************************************/

void audio_ReleaseTrack(TRACK* psTrack)
{
  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE)
    return;

  sound_ReleaseTrack(psTrack);
}

/***************************************************************************/
/*
 * audio_CheckSame3DTracksPlaying
 *
 * Reject samples if too many already playing in same area
 */
/***************************************************************************/

static BOOL audio_CheckSame3DTracksPlaying(SDWORD iTrack, SDWORD iX, SDWORD iY, SDWORD iZ)
{
  SDWORD iCount, iDx, iDy, iDz, iDistSq, iMaxDistSq, iRad;
  AUDIO_SAMPLE* psSample = nullptr;
  BOOL bOK = TRUE;

  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE || g_bAudioPaused == TRUE)
    return TRUE;

  iCount = 0;

  /* loop through 3D sounds and check whether too many already in earshot */
  psSample = g_psSampleList;
  while (psSample != nullptr)
  {
    if (psSample->iTrack == iTrack)
    {
      iDx = iX - psSample->x;
      iDy = iY - psSample->y;
      iDz = iZ - psSample->z;
      iDistSq = (iDx * iDx) + (iDy * iDy) + (iDz * iDz);
      iRad = sound_GetTrackAudibleRadius(iTrack);
      iMaxDistSq = iRad * iRad;

      if (iDistSq < iMaxDistSq)
        iCount++;

      if (iCount > MAX_SAME_SAMPLES)
      {
        bOK = FALSE;
        break;
      }
    }

    psSample = psSample->psNext;
  }

  return bOK;
}

/***************************************************************************/

static BOOL audio_Play3DTrack(SDWORD iX, SDWORD iY, SDWORD iZ, int iTrack, void* psObj, AUDIO_CALLBACK pUserCallback)
{
  AUDIO_SAMPLE* psSample;

  /* if audio not enabled return TRUE to carry on game without audio */
  if (g_bAudioEnabled == FALSE || g_bAudioPaused == TRUE || g_bStopAll == TRUE)
    return FALSE;

  if (audio_CheckSame3DTracksPlaying(iTrack, iX, iY, iZ) == FALSE)
    return FALSE;

  psSample = new (std::nothrow) AUDIO_SAMPLE;
  if (psSample == nullptr)
    return FALSE;
  /* setup sample */
  memset(psSample, 0, sizeof(AUDIO_SAMPLE));
  psSample->iTrack = iTrack;
  psSample->x = iX;
  psSample->y = iY;
  psSample->z = iZ;
  psSample->bRemove = FALSE;
  psSample->psObj = psObj;
  psSample->pCallback = pUserCallback;

  /* add sample to list if able to play */
  if (sound_Play3DTrack(psSample) == TRUE)
  {
    audio_AddSampleToHead(&g_psSampleList, psSample);
    return TRUE;
  }
  Neuron::DebugTrace("audio_Play3DTrack: couldn't play sample\n");
  delete psSample;
  return FALSE;
}

/***************************************************************************/

BOOL audio_PlayStaticTrack(SDWORD iMapX, SDWORD iMapY, int iTrack)
{
  SDWORD iX, iY, iZ;

  /* if audio not enabled return TRUE to carry on game without audio */
  if (g_bAudioEnabled == FALSE)
    return FALSE;

  audio_GetStaticPos(iMapX, iMapY, &iX, &iY, &iZ);
  return audio_Play3DTrack(iX, iY, iZ, iTrack, nullptr, nullptr);
}

/***************************************************************************/

BOOL audio_PlayObjStaticTrack(void* psObj, int iTrack)
{
  SDWORD iX, iY, iZ;

  /* if audio not enabled return TRUE to carry on game without audio */
  if (g_bAudioEnabled == FALSE)
    return FALSE;

  audio_GetObjectPos(psObj, &iX, &iY, &iZ);
  return audio_Play3DTrack(iX, iY, iZ, iTrack, psObj, nullptr);
}

/***************************************************************************/

BOOL audio_PlayObjStaticTrackCallback(void* psObj, int iTrack, AUDIO_CALLBACK pUserCallback)
{
  SDWORD iX, iY, iZ;

  /* if audio not enabled return TRUE to carry on game without audio */
  if (g_bAudioEnabled == FALSE)
    return FALSE;

  audio_GetObjectPos(psObj, &iX, &iY, &iZ);
  return audio_Play3DTrack(iX, iY, iZ, iTrack, psObj, pUserCallback);
}

/***************************************************************************/

BOOL audio_PlayObjDynamicTrack(void* psObj, int iTrack, AUDIO_CALLBACK pUserCallback)
{
  SDWORD iX, iY, iZ;

  /* if audio not enabled return TRUE to carry on game without audio */
  if (g_bAudioEnabled == FALSE)
    return FALSE;

  audio_GetObjectPos(psObj, &iX, &iY, &iZ);
  return audio_Play3DTrack(iX, iY, iZ, iTrack, psObj, pUserCallback);
}

/***************************************************************************/

BOOL audio_PlayStream(char szFileName[], SDWORD iVol, AUDIO_CALLBACK pUserCallback)
{
  AUDIO_SAMPLE* psSample;

  /* if audio not enabled return TRUE to carry on game without audio */
  if (g_bAudioEnabled == FALSE)
    return FALSE;

  psSample = new (std::nothrow) AUDIO_SAMPLE;

  if (psSample == nullptr)
    return FALSE;

  memset(psSample, 0, sizeof(AUDIO_SAMPLE));
  psSample->pCallback = pUserCallback;
  psSample->bRemove = FALSE;

  audio_Set3DVolume(AUDIO_VOL_MAX);

  if (sound_PlayStream(psSample, szFileName, iVol) == FALSE)
  {
    delete psSample;
    return FALSE;
  }

  /* The sample has to be on the list or nothing ever frees it: the backend
   * reports the stream finished by setting bRemove, and audio_Update only
   * looks at samples it can see. It used to be allocated and dropped.
   */
  audio_AddSampleToHead(&g_psSampleList, psSample);

  return TRUE;
}

/***************************************************************************/
/*
 * Music, which is what replaces the CD audio. It has its own slot in the
 * backend and no sample bookkeeping, so these are thin.
 */
/***************************************************************************/

BOOL audio_PlayMusic(char szFileName[], SDWORD iVol, BOOL bLoop)
{
  if (g_bAudioEnabled == FALSE)
    return FALSE;

  return sound_PlayMusic(szFileName, iVol, bLoop);
}

/***************************************************************************/

void audio_StopMusic(void)
{
  if (g_bAudioEnabled == TRUE)
    sound_StopMusic();
}

/***************************************************************************/

void audio_PauseMusic(void)
{
  if (g_bAudioEnabled == TRUE)
    sound_PauseMusic();
}

/***************************************************************************/

void audio_ResumeMusic(void)
{
  if (g_bAudioEnabled == TRUE)
    sound_ResumeMusic();
}

/***************************************************************************/

SDWORD audio_GetFXVolume(void) { return sound_GetGlobalVolume(); }

/***************************************************************************/

void audio_SetFXVolume(SDWORD iVol) { sound_SetGlobalVolume(iVol); }

/***************************************************************************/

SDWORD audio_GetMusicVolume(void) { return sound_GetMusicVolume(); }

/***************************************************************************/

void audio_SetMusicVolume(SDWORD iVol) { sound_SetMusicVolume(iVol); }

/***************************************************************************/

void audio_StopObjTrack(void* psObj, int iTrack)
{
  AUDIO_SAMPLE* psSample;

  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE || g_bStopAll == TRUE)
    return;

  /* find sample */
  psSample = g_psSampleList;
  while (psSample != nullptr)
  {
    if (psSample->psObj == psObj && psSample->iTrack == iTrack)
      break;

    /* get next sample from hash table */
    psSample = psSample->psNext;
  }

  if (psSample != nullptr)
    sound_StopTrack(psSample);
}

/***************************************************************************/
/*
 * audio_PlayTrack
 *
 * Play immediate 2D FX track
 */
/***************************************************************************/

void audio_PlayTrack(int iTrack)
{
  AUDIO_SAMPLE* psSample;

  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE || g_bAudioPaused == TRUE || g_bStopAll == TRUE)
    return;

  psSample = new (std::nothrow) AUDIO_SAMPLE;
  if (psSample != nullptr)
  {
    /* setup sample */
    memset(psSample, 0, sizeof(AUDIO_SAMPLE));
    psSample->iTrack = iTrack;
    psSample->bRemove = FALSE;

    /* add sample to list if able to play */
    if (sound_Play2DTrack(psSample, FALSE) == TRUE)
      audio_AddSampleToHead(&g_psSampleList, psSample);
    else
    {
      Neuron::DebugTrace("audio_PlayTrack: couldn't play sample\n");
      delete psSample;
    }
  }
}

/***************************************************************************/

void audio_StopAll(void)
{
  AUDIO_SAMPLE *psSample, *psSampleTemp;

  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE)
    return;

  Neuron::DebugTrace("audio_StopAll called\n");

  g_bStopAll = TRUE;

  /* empty list - audio_Update will free samples
   * because callbacks have to come in first
   */
  psSample = g_psSampleList;
  while (psSample != nullptr)
  {
    sound_StopTrack(psSample);
    psSample = psSample->psNext;
  }

  /* empty sample queue */
  psSample = g_psSampleQueue;
  while (psSample != nullptr)
  {
    psSampleTemp = psSample->psNext;
    delete psSample;
    psSample = psSampleTemp;
  }
  g_psSampleQueue = nullptr;

  g_bStopAll = FALSE;

  Neuron::DebugTrace("audio_StopAll done\n");
}

/***************************************************************************/

void audio_CheckAllUnloaded() { sound_CheckAllUnloaded(); }

/***************************************************************************/

SDWORD audio_GetTrackID(char szFileName[])
{
  TRACK* psTrack;
  SDWORD iID;

  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE)
    return SAMPLE_NOT_FOUND;
  psTrack = static_cast<TRACK*>(resGetData("WAV", szFileName));

  if (psTrack == nullptr)
    return SAMPLE_NOT_FOUND;
  iID = sound_GetTrackID(psTrack);
  return iID;
}

/***************************************************************************/

SDWORD audio_GetTrackIDFromHash(UDWORD hash)
{
  TRACK* psTrack;
  SDWORD iID;

  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE)
    return SAMPLE_NOT_FOUND;
  psTrack = static_cast<TRACK*>(resGetDataFromHash("WAV", hash));

  if (psTrack == nullptr)
    return SAMPLE_NOT_FOUND;
  iID = sound_GetTrackID(psTrack);
  return iID;
}

/***************************************************************************/

SDWORD audio_GetAvailableID(void)
{
  /* return if audio not enabled */
  if (g_bAudioEnabled == FALSE)
    return 0;
  return sound_GetAvailableID();
}

/***************************************************************************/

SDWORD audio_Get3DVolume(void) { return g_i3DVolume; }

/***************************************************************************/

void audio_Set3DVolume(SDWORD iVol) { g_i3DVolume = iVol; }

/***************************************************************************/
/*
 * audio_GetSampleMixVol
 *
 * iVol, audio_Get3DVolume and sound_GetTrackVolume all need to be scaled
 * by AUDIO_VOL_RANGE
 */
/***************************************************************************/

SDWORD audio_GetSampleMixVol(AUDIO_SAMPLE* psSample, SDWORD iVol, BOOL bScale3D)
{
  SDWORD iMixVol;

  iMixVol = iVol * sound_GetMaxVolume() / AUDIO_VOL_RANGE;
  iMixVol = iMixVol * sound_GetTrackVolume(psSample->iTrack) / AUDIO_VOL_RANGE;
  if (bScale3D)
    iMixVol = iMixVol * audio_Get3DVolume() / AUDIO_VOL_RANGE;

  return iMixVol;
}

/***************************************************************************/
/*
 * The track table, folded in from Track.cpp: the ID-indexed registry the
 * sample lifecycle above and the backend below both consult. It was a file of
 * its own when the backend was swappable; with exactly one backend the split
 * carried nothing.
 */
/***************************************************************************/

#define	MAX_TRACKS			(600)

/* array of pointers to sound effects */
static TRACK** g_apTrack;

/* number of tracks loaded */
static SDWORD g_iCurTracks = 0;

/* flag set when system is active (for callbacks etc) */
static BOOL g_bSystemActive = FALSE;

static AUDIO_CALLBACK g_pStopTrackCallback = nullptr;

/***************************************************************************/

BOOL sound_Init()
{
  SDWORD i;

  g_iCurTracks = 0;

  if (sound_InitLibrary() == FALSE)
  {
    Neuron::DebugTrace("Cannot init sound library\n");
    return FALSE;
  }

  /* init audio array */
  g_apTrack = new (std::nothrow) TRACK*[MAX_TRACKS];
  for (i = 0; i < MAX_TRACKS; i++)
    g_apTrack[i] = nullptr;

  /* set system active flag for callbacks */
  g_bSystemActive = TRUE;

  return TRUE;
}

/***************************************************************************/

BOOL sound_Shutdown()
{
  delete[] g_apTrack;
  g_apTrack = nullptr;

  /* set inactive flag to prevent callbacks coming after shutdown */
  g_bSystemActive = FALSE;

  sound_ShutdownLibrary();

  return TRUE;
}

/***************************************************************************/

BOOL sound_GetSystemActive(void) { return g_bSystemActive; }

/***************************************************************************/

BOOL sound_SetTrackVals(TRACK* psTrack, BOOL bLoop, SDWORD iTrack, SDWORD iVol, SDWORD iPriority, SDWORD iAudibleRadius)
{
  DEBUG_ASSERT_TEXT(iPriority>=LOW_PRIORITY && iPriority<=HIGH_PRIORITY, "sound_CreateTrack: priority {} out of bounds\n", iPriority);

  /* add to sound array */
  if (iTrack < MAX_TRACKS)
  {
    if (g_apTrack[iTrack] != nullptr)
    {
      Neuron::Fatal("sound_SetTrackVals: track {} already set\n", iTrack );
      return FALSE;
    }

    /* set track members */
    psTrack->bLoop = bLoop;
    psTrack->iVol = iVol;
    psTrack->iPriority = iPriority;
    psTrack->iAudibleRadius = iAudibleRadius;
    psTrack->iTimeLastFinished = 0;

    /* set global */
    g_apTrack[iTrack] = psTrack;

    /* increment current sound */
    g_iCurTracks++;

    return TRUE;
  }

  return FALSE;
}

/***************************************************************************/

void* sound_LoadTrackFromBuffer(UBYTE* pBuffer, UDWORD udwSize)
{
  TRACK* pTrack;

  /* allocate track */
  pTrack = new (std::nothrow) TRACK[1];

  if (pTrack == nullptr)
  {
    Neuron::Fatal("sound_LoadTrackFromBuffer: couldn't allocate memory\n");
    return nullptr;
  }
  pTrack->pName = new (std::nothrow) STRING[strlen(GetLastResourceFilename()) + 1];
  if (pTrack->pName == nullptr)
  {
    Neuron::Fatal("sound_LoadTrackFromBuffer: couldn't allocate memory\n");
    delete[] pTrack;
    pTrack = nullptr;
    return nullptr;
  }
  strcpy(pTrack->pName, GetLastResourceFilename());
  pTrack->resID = GetLastHashName();

  if (sound_ReadTrackFromBuffer(pTrack, pBuffer, udwSize) == FALSE)
    return nullptr;

  /* flag compressed audio load */
  if (pTrack->bCompressed == TRUE) { Neuron::DebugTrace("sound_LoadTrackFromBuffer: {} is compressed!\n", pTrack->pName ); }

  return pTrack;
}

/***************************************************************************/

BOOL sound_ReleaseTrack(TRACK* psTrack)
{
  SDWORD iTrack;

  if (psTrack->pName != nullptr) { delete[] psTrack->pName; }

  for (iTrack = 0; iTrack < g_iCurTracks; iTrack++)
  {
    if (g_apTrack[iTrack] == psTrack)
      g_apTrack[iTrack] = nullptr;
  }

  sound_FreeTrack(psTrack);

  return TRUE;
}

/***************************************************************************/

void sound_CheckAllUnloaded(void)
{
  SDWORD iTrack;

  for (iTrack = 0; iTrack < MAX_TRACKS; iTrack++)
  {
    DEBUG_ASSERT_TEXT(g_apTrack[iTrack] == NULL, "sound_CheckAllUnloaded: check audio.cfg for duplicate IDs\n");
  }
}

/***************************************************************************/

BOOL sound_TrackLooped(SDWORD iTrack)
{
  sound_CheckTrack(iTrack);

  return g_apTrack[iTrack]->bLoop;
}

/***************************************************************************/

SDWORD sound_TrackAudibleRadius(SDWORD iTrack)
{
  sound_CheckTrack(iTrack);

  return g_apTrack[iTrack]->iAudibleRadius;
}

/***************************************************************************/

void sound_CheckSample(AUDIO_SAMPLE* psSample)
{
  DEBUG_ASSERT_TEXT(psSample->iSample >=0 ||
    psSample->iSample == SAMPLE_NOT_ALLOCATED, "sound_CheckSample: sample {} out of range\n", psSample->iSample );

  psSample;
}

/***************************************************************************/

BOOL sound_CheckTrack(SDWORD iTrack)
{
  if (iTrack < 0 || iTrack > g_iCurTracks - 1)
  {
    Neuron::DebugTrace("sound_CheckTrack: track number {} outside max {}\n", iTrack, g_iCurTracks);
    return FALSE;
  }

  if (g_apTrack[iTrack] == nullptr)
  {
    Neuron::DebugTrace("sound_CheckTrack: track {} NULL\n", iTrack);
    return FALSE;
  }

  return TRUE;
}

/***************************************************************************/

SDWORD sound_GetTrackVolume(SDWORD iTrack)
{
  sound_CheckTrack(iTrack);

  return g_apTrack[iTrack]->iVol;
}

/***************************************************************************/

SDWORD sound_GetTrackAudibleRadius(SDWORD iTrack)
{
  sound_CheckTrack(iTrack);

  return g_apTrack[iTrack]->iAudibleRadius;
}

/***************************************************************************/

UDWORD sound_GetTrackHashName(SDWORD iTrack)
{
  DEBUG_ASSERT_TEXT(g_apTrack[iTrack] != NULL, "sound_GetTrackHashName: unallocated track");
  return g_apTrack[iTrack]->resID;
}

/***************************************************************************/

BOOL sound_Play2DTrack(AUDIO_SAMPLE* psSample, BOOL bQueued)
{
  TRACK* psTrack;

  psTrack = g_apTrack[psSample->iTrack];

  if (psTrack->bCompressed)
  {
    Neuron::DebugTrace("sound_PlayTrack: trying to play compressed speech {}!\n", psTrack->pName);
    return FALSE;
  }

  return sound_Play2DSample(psTrack, psSample, bQueued);
}

/***************************************************************************/

BOOL sound_Play3DTrack(AUDIO_SAMPLE* psSample)
{
  TRACK* psTrack;

  psTrack = g_apTrack[psSample->iTrack];

  if (psTrack->bCompressed)
  {
    Neuron::DebugTrace("sound_PlayTrack: trying to play compressed audio {}!\n", psTrack->pName);
    return FALSE;
  }

  return sound_Play3DSample(psTrack, psSample);
}

/***************************************************************************/

void sound_StopTrack(AUDIO_SAMPLE* psSample)
{
  sound_CheckSample(psSample);

  if (psSample->iSample != SAMPLE_NOT_ALLOCATED)
    sound_StopSample(psSample->iSample);

  /* do stopped callback */
  if (g_pStopTrackCallback != nullptr && psSample->psObj != nullptr)
    (g_pStopTrackCallback)(psSample);
}

/***************************************************************************/

void sound_FinishedCallback(AUDIO_SAMPLE* psSample)
{
  sound_CheckSample(psSample);

  if (g_apTrack[psSample->iTrack] != nullptr)
    g_apTrack[psSample->iTrack]->iTimeLastFinished = sound_GetGameTime();

  /* call user callback if specified */
  if (psSample->pCallback != nullptr)
    (psSample->pCallback)(psSample);

  /* set remove flag */
  psSample->bRemove = TRUE;
}

/***************************************************************************/

SDWORD sound_GetTrackID(TRACK* psTrack)
{
  SDWORD i = 0;

  /* find matching track */
  for (i = 0; i < MAX_TRACKS; i++)
  {
    if ((g_apTrack[i] != nullptr) && (g_apTrack[i] == psTrack))
      break;
  }

  /* if matching track found return it else find empty track */
  if (i < MAX_TRACKS)
    return i;
  return SAMPLE_NOT_FOUND;
}

/***************************************************************************/

SDWORD sound_GetAvailableID(void)
{
  SDWORD i;

  for (i = 0; i < MAX_TRACKS; i++)
  {
    if (g_apTrack[i] == nullptr)
      break;
  }

  DEBUG_ASSERT_TEXT(i<MAX_TRACKS, "sound_GetTrackID: unused track not found!\n");

  if (i < MAX_TRACKS)
    return i;
  return SAMPLE_NOT_ALLOCATED;
}

/***************************************************************************/

void sound_SetStoppedCallback(AUDIO_CALLBACK pStopTrackCallback) { g_pStopTrackCallback = pStopTrackCallback; }

/***************************************************************************/

UDWORD sound_GetTrackTimeLastFinished(SDWORD iTrack)
{
  sound_CheckTrack(iTrack);

  return g_apTrack[iTrack]->iTimeLastFinished;
}

/***************************************************************************/

void sound_SetTrackTimeLastFinished(SDWORD iTrack, UDWORD iTime)
{
  sound_CheckTrack(iTrack);

  g_apTrack[iTrack]->iTimeLastFinished = iTime;
}

/***************************************************************************/
