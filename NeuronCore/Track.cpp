#include "pch.h"
#include "Frame.h"

#include "TrackLib.h"
#include "Priority.h"

/***************************************************************************/
/* defines */

#define	MAX_TRACKS			(600)

/***************************************************************************/
/* static global variables */

/* array of pointers to sound effects */
static TRACK** g_apTrack;

/* number of tracks loaded */
static SDWORD g_iCurTracks = 0;

static SDWORD g_iSamples = 0;
static SDWORD g_iMaxSamples;
static SDWORD g_iMaxSameSamples;

/* flag set when system is active (for callbacks etc) */
static BOOL g_bSystemActive = FALSE;

static BOOL g_bDevVolume = FALSE;

static AUDIO_CALLBACK g_pStopTrackCallback = nullptr;

/***************************************************************************/

BOOL sound_CheckDevice(void)
{
  WAVEOUTCAPS waveCaps;
  MMRESULT mmRes;

  /* check wave out device(s) present */
  if (waveOutGetNumDevs() == 0)
  {
    Neuron::DebugTrace("sound_CheckDevice: error in waveOutGetNumDevs\n");
    return FALSE;
  }

  /* default to using first device: check volume control caps */
  mmRes = waveOutGetDevCaps(0, &waveCaps, sizeof(WAVEOUTCAPS));
  if (mmRes != MMSYSERR_NOERROR)
  {
    Neuron::DebugTrace("sound_CheckDevice: error in waveOutGetDevCaps\n");
    return FALSE;
  }

  /* verify device supports volume changes */
  if (waveCaps.dwSupport & WAVECAPS_VOLUME)
    return TRUE;
  Neuron::DebugTrace("sound_CheckDevice: wave out device doesn't support volume changes\n");
  return FALSE;
}

/***************************************************************************/

BOOL sound_Init(HWND hWnd, SDWORD iMaxSameSamples)
{
  SDWORD i;

  hWnd;
  g_iMaxSameSamples = iMaxSameSamples;

  g_iCurTracks = 0;

  g_bDevVolume = sound_CheckDevice();

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
/*
 * Vag ID is just used on PSX
 * szFileName just used on PC
 */
/***************************************************************************/

BOOL sound_SetTrackVals(TRACK* psTrack, BOOL bLoop, SDWORD iTrack, SDWORD iVol, SDWORD iPriority, SDWORD iAudibleRadius, SDWORD VagID)
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
    psTrack->iNumPlaying = 0;

    VagID;

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
  pTrack->bMemBuffer = TRUE;
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

SDWORD sound_GetNumPlaying(SDWORD iTrack)
{
  sound_CheckTrack(iTrack);

  return g_apTrack[iTrack]->iNumPlaying;
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

SDWORD sound_GetTrackTime(SDWORD iTrack)
{
  sound_CheckTrack(iTrack);

  return g_apTrack[iTrack]->iTime;
}

/***************************************************************************/

SDWORD sound_GetTrackPriority(SDWORD iTrack)
{
  sound_CheckTrack(iTrack);

  return g_apTrack[iTrack]->iPriority;
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

char* sound_GetTrackName(SDWORD iTrack)
{
  DEBUG_ASSERT_TEXT(g_apTrack[iTrack] != NULL, "sound_GetTrackName: unallocated track");
  return g_apTrack[iTrack]->pName;
}

/***************************************************************************/

UDWORD sound_GetTrackHashName(SDWORD iTrack)
{
  DEBUG_ASSERT_TEXT(g_apTrack[iTrack] != NULL, "sound_GetTrackName: unallocated track");
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

  /* update number of samples playing */
  g_iSamples--;
}

/***************************************************************************/

void sound_PauseTrack(AUDIO_SAMPLE* psSample)
{
  if (psSample->iSample != SAMPLE_NOT_ALLOCATED)
    sound_StopSample(psSample->iSample);
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

SDWORD sound_GetGlobalVolume(void)
{
  MMRESULT mmRes;
  SDWORD iVol;
  SDWORD iGlobVol = AUDIO_VOL_MAX;

  if (g_bDevVolume == TRUE)
  {
    mmRes = waveOutGetVolume(nullptr, (LPDWORD)&iVol);
    if (mmRes == MMSYSERR_NOERROR)
      iGlobVol = static_cast<SDWORD>(LOWORD(iVol)) * AUDIO_VOL_MAX / 0xFFFF;
    else
      Neuron::DebugTrace("sound_GetGlobalVolume: waveOutGetVolume failed\n");
  }

  return iGlobVol;
}

/***************************************************************************/

void sound_SetGlobalVolume(SDWORD iVol)
{
  MMRESULT mmRes;
  SDWORD iNewVol, iWinVol;

  if (g_bDevVolume == TRUE)
  {
    iWinVol = iVol * 0xFFFF / AUDIO_VOL_MAX;
    iNewVol = (iWinVol << 16) | iWinVol;

    mmRes = waveOutSetVolume(nullptr, iNewVol);
    if (mmRes != MMSYSERR_NOERROR)
      Neuron::DebugTrace("sound_GetGlobalVolume: waveOutSetVolume failed\n");
  }
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
