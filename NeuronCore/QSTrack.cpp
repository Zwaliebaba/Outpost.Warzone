#include "pch.h"
/***************************************************************************/
/*
 * QSound library-specific functions
 */
/***************************************************************************/

#include <windows.h>
#include <mmreg.h>

#include "Frame.h"
#include "TrackLib.h"
#include "Audio.h"

#define	PI	 3.14159265359

#define QMIXER	1

#if QMIXER
#include "QMIXER.H"
#else
#include "qmdx.h"
#endif

/***************************************************************************/

#if QMIXER
#define QSOUND(name) QSWaveMix ## name
#else
#define QSOUND(name) QMDX_ ## name
#endif

#define	QS_SAMPLINGRATE			KHZ22

#define	QS_CHANNELS				200
#define QS_QSOUND_CHANNELS		20

#define	QS_2D_CHANNELS			3
#define	QS_CHANNEL_QUEUE		0
#define	QS_CHANNEL_STREAM		1
#define	QS_CHANNEL_FX			2

#define	QS_MIN_DISTANCE			(300.0f)
#define	QS_SCALE				(1.5f)

#define	DYNAMIC_SHEDULING		1

/***************************************************************************/

static HQMIXER g_hQMixer;
static QMIXCONFIG g_qMixConfig;

static char g_szErrMsg[MAX_STR];
static UINT g_uiRet;
static SDWORD g_iError;

static UDWORD g_iDynamicChannel = QS_2D_CHANNELS;

using RIFFDATA = struct RIFFDATA
{
  WAVEFORMATEX* pWaveFormat;
  UBYTE* pubData;
  LPMIXWAVE psMixWave;
};

/***************************************************************************/

#define	MONO	 1
#define	STEREO	 2
#define	BIT8	 8
#define	BIT16	16

#define	MONO_16BIT_BLOCKALIGN	MONO*BIT16/8
#define	STEREO_16BIT_BLOCKALIGN	STEREO*BIT16/8

#define	AVERAGEBYTERATE_MONO16		QS_SAMPLINGRATE*MONO_16BIT_BLOCKALIGN
#define	AVERAGEBYTERATE_STEREO16	QS_SAMPLINGRATE*STEREO_16BIT_BLOCKALIGN

/***************************************************************************/

static void sound_SetSamplePan(AUDIO_SAMPLE* psSample, int iPan);
static void sound_SetSampleVol(AUDIO_SAMPLE* psSample, SDWORD iVol, BOOL bScale3D);

/***************************************************************************/

BOOL sound_InitLibrary(void)
{
  static WAVEFORMATEX wFormatMonoPCM16 = {WAVE_FORMAT_PCM, 1, QS_SAMPLINGRATE, AVERAGEBYTERATE_MONO16, MONO_16BIT_BLOCKALIGN, BIT16, 0},
                      wFormatStereoPCM16 = {
                        WAVE_FORMAT_PCM, 2, QS_SAMPLINGRATE, AVERAGEBYTERATE_STEREO16, STEREO_16BIT_BLOCKALIGN, BIT16, 0
                      };

  /* specify number of dynamic channels to be as many hardware as possible
   * and QS_QSOUND_CHANNELS software channels
   */
  QMIX_CHANNELTYPES channelTypes[] = {
    {QMIX_CHANNELTYPE_QSOUND | QMIX_CHANNELTYPE_2D, QS_2D_CHANNELS},
    //		{ QMIX_CHANNELTYPE_STREAMING | QMIX_CHANNELTYPE_3D, -1 },
    {QMIX_CHANNELTYPE_QSOUND | QMIX_CHANNELTYPE_3D, QS_QSOUND_CHANNELS}, {0, 0}
  };

  /* init config struct */
  memset(&g_qMixConfig, 0, sizeof(g_qMixConfig));
  g_qMixConfig.dwSize = sizeof(g_qMixConfig);
  g_qMixConfig.dwSamplingRate = QS_SAMPLINGRATE;
  g_qMixConfig.iChannels = QS_CHANNELS;

  g_hQMixer = QSOUND(InitEx( &g_qMixConfig ));
  if (!g_hQMixer)
    goto initError;

  /* open all channels */
  g_uiRet = QSOUND(OpenChannel( g_hQMixer, 0, QMIX_OPENALL ));
  if (g_uiRet == -1)
    goto initError;

  /* configure fixed 2D speech queue channel */
  g_uiRet = QSOUND(ConfigureChannel( g_hQMixer, QS_CHANNEL_QUEUE, QMIX_CHANNELTYPE_2D, &wFormatMonoPCM16, nullptr ))  ;
  if (g_uiRet < 0)
    goto initError;

  /* configure fixed 2D streaming channel */
  g_uiRet = QSOUND(ConfigureChannel( g_hQMixer, QS_CHANNEL_STREAM, QMIX_CHANNELTYPE_2D, &wFormatStereoPCM16, nullptr ))  ;
  if (g_uiRet < 0)
    goto initError;

  /* configure fixed 2D fx channel */
  g_uiRet = QSOUND(ConfigureChannel( g_hQMixer, QS_CHANNEL_FX, QMIX_CHANNELTYPE_2D, &wFormatMonoPCM16, nullptr ))  ;
  if (g_uiRet < 0)
    goto initError;

#if DYNAMIC_SHEDULING
  /* configure dynamic allocation channels */
  g_uiRet = QSOUND(PrioritizeChannels( g_hQMixer, channelTypes, QMIX_FINDCHANNEL_DISTANCE, nullptr ))  ;
  if (g_uiRet < 0)
    goto initError;
#endif

  /* start system */
  g_uiRet = QSOUND(Activate( g_hQMixer, TRUE ));
  if (g_uiRet < 0)
    goto initError;

  return TRUE;

initError:

  g_iError = QSOUND(GetLastError());
  QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
  Neuron::DebugTrace("sound_InitLibrary: {}", g_szErrMsg);
  return FALSE;
}

/***************************************************************************/

void sound_ShutdownLibrary(void)
{
  if (QSOUND(CloseChannel( g_hQMixer, -1, QMIX_ALL )) != 0 || QSOUND(CloseSession( g_hQMixer )) != 0)
  {
    g_iError = QSOUND(GetLastError());
    QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
    Neuron::DebugTrace("sound_ShutdownLibrary: {}", g_szErrMsg);
  }
}

/***************************************************************************/

void sound_Update(void) {}

/***************************************************************************/

BOOL sound_QueueSamplePlaying(void) { return !QSOUND(IsChannelDone( g_hQMixer, QS_CHANNEL_QUEUE )); }

/***************************************************************************/

static void sound_SaveTrackData(TRACK* psTrack, QMIXWAVEPARAMS* psQMixParams, LPMIXWAVE psMixWave)
{
  RIFFDATA* psRiffData;

  psTrack->iTime = psMixWave->wh.dwBufferLength * 1000 / g_qMixConfig.dwSamplingRate + 1;

  /* add to riff data list */
  psRiffData = new (std::nothrow) RIFFDATA[1];
  psRiffData->pWaveFormat = (WAVEFORMATEX*)psQMixParams->Resident.Format;
  psRiffData->pubData = (UBYTE*)psQMixParams->Resident.Data;
  psRiffData->psMixWave = psMixWave;

  /* save data pointer in track */
  psTrack->pMem = psRiffData;
}

/***************************************************************************/

static BOOL sound_ReadRiffMemResFile(QMIXWAVEPARAMS* pQMixParams, void* pBuffer, UDWORD udwSize, BOOL* pbCompressed)
{
  MMIOINFO mmioInfo;
  MMCKINFO waveChunk, factChunk, formatChunk, dataChunk;
  HMMIO hmmio;

  memset(pQMixParams, 0, sizeof(QMIXWAVEPARAMS));

  memset(&mmioInfo, 0, sizeof(MMIOINFO));
  mmioInfo.fccIOProc = FOURCC_MEM;
  mmioInfo.cchBuffer = udwSize;
  mmioInfo.pchBuffer = static_cast<HPSTR>(pBuffer);

  hmmio = mmioOpen(nullptr, &mmioInfo, MMIO_READ);
  if (!hmmio)
    return FALSE;

  waveChunk.fccType = mmioFOURCC('W', 'A', 'V', 'E');
  if (mmioDescend(hmmio, &waveChunk, nullptr, MMIO_FINDRIFF))
    return FALSE;

  /* Look for 'fact' chunk in case we have a compressed file
	 * and set flag accordingly
	 */
  factChunk.ckid = mmioFOURCC('f', 'a', 'c', 't');
  if (mmioDescend(hmmio, &factChunk, &waveChunk, MMIO_FINDCHUNK) == 0 && factChunk.cksize >= sizeof(DWORD))
  {
    mmioRead(hmmio, (char*)&pQMixParams->Resident.Samples, sizeof(DWORD));
    *pbCompressed = TRUE;
  }
  else
    *pbCompressed = FALSE;

  mmioSeek(hmmio, 0, SEEK_SET);
  mmioDescend(hmmio, &waveChunk, nullptr, MMIO_FINDRIFF);

  //
  // Read wave format.
  //
  formatChunk.ckid = mmioFOURCC('f', 'm', 't', ' ');
  if (mmioDescend(hmmio, &formatChunk, &waveChunk, MMIO_FINDCHUNK))
    return FALSE;

  // the format chunk is WAVEFORMATEX plus a variable tail, so it is read as bytes
  pQMixParams->Resident.Format = (WAVEFORMATEX*)new (std::nothrow) UBYTE[formatChunk.cksize];
  if (mmioRead(hmmio, (char*)pQMixParams->Resident.Format, formatChunk.cksize) != static_cast<LONG>(formatChunk.cksize))
    return FALSE;

  mmioAscend(hmmio, &formatChunk, 0);

  //
  // Read wave data.
  //
  dataChunk.ckid = mmioFOURCC('d', 'a', 't', 'a');
  if (mmioDescend(hmmio, &dataChunk, &waveChunk, MMIO_FINDCHUNK))
    return FALSE;

  pQMixParams->Resident.Bytes = dataChunk.cksize;
  pQMixParams->Resident.Data = (HPSTR)new (std::nothrow) UBYTE[dataChunk.cksize];

  if (mmioRead(hmmio, pQMixParams->Resident.Data, dataChunk.cksize) != static_cast<LONG>(dataChunk.cksize))
    return FALSE;

  mmioAscend(hmmio, &dataChunk, 0);

  mmioClose(hmmio, 0);

  return TRUE;
}

/***************************************************************************/

BOOL sound_ReadTrackFromBuffer(TRACK* psTrack, void* pBuffer, UDWORD udwSize)
{
  QMIXWAVEPARAMS sQMixParams;
  LPMIXWAVE psMixWave;

  if (!sound_ReadRiffMemResFile(&sQMixParams, pBuffer, udwSize, &psTrack->bCompressed))
    return FALSE;

  psMixWave = QSOUND(OpenWaveEx( g_hQMixer, &sQMixParams, QMIX_RESIDENT ));

  if (psMixWave)
  {
    sound_SaveTrackData(psTrack, &sQMixParams, psMixWave);
    return TRUE;
  }
  g_iError = QSOUND(GetLastError());
  QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
  Neuron::Fatal("sound_ReadTrackFromBuffer: {}", g_szErrMsg);
  return FALSE;
}

/***************************************************************************/

void sound_FreeTrack(TRACK* psTrack)
{
  auto psRiffData = static_cast<RIFFDATA*>(psTrack->pMem);

  if (psRiffData == nullptr)
  {
    Neuron::DebugTrace("sound_FreeTrack: invalid data pointer");
    return;
  }

  g_uiRet = QSOUND(FreeWave( g_hQMixer, psRiffData->psMixWave ));
  if (g_uiRet != 0)
  {
    g_iError = QSOUND(GetLastError());
    QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
    Neuron::DebugTrace("sound_FreeTrack: {}", g_szErrMsg);
  }

  if (psRiffData->pWaveFormat != nullptr) { delete[] (UBYTE*)psRiffData->pWaveFormat; }

  if (psRiffData->pubData != nullptr) { delete[] psRiffData->pubData; }

  delete[] psRiffData;
  psRiffData = nullptr;
}

/***************************************************************************/

int sound_GetMaxVolume(void) { return 32767; }

/***************************************************************************/

void CALLBACK sound_QSoundFinishedCallback(int iChannel, LPMIXWAVE lpWave, DWORD dwUser)
{
  if (sound_GetSystemActive() == TRUE)
  {
    iChannel;
    lpWave;
    sound_FinishedCallback((AUDIO_SAMPLE*)dwUser);
  }
}

/***************************************************************************/

static void sound_SetupChannel(AUDIO_SAMPLE* psSample, QMIXPLAYPARAMS* pParams, SDWORD* piLoops)
{
  if (sound_TrackLooped(psSample->iTrack) == TRUE)
    *piLoops = -1;
  else
    *piLoops = 0;

  /* set up callback */
  memset(pParams, 0, sizeof(QMIXPLAYPARAMS));
  pParams->dwSize = sizeof(QMIXPLAYPARAMS);
  pParams->callback = sound_QSoundFinishedCallback;
  pParams->dwUser = (DWORD)psSample;
}

/***************************************************************************/

BOOL sound_Play2DSample(TRACK* psTrack, AUDIO_SAMPLE* psSample, BOOL bQueued)
{
  SDWORD iLoops, iPan;
  QMIXPLAYPARAMS params;
  auto psRiffData = static_cast<RIFFDATA*>(psTrack->pMem);

  sound_SetupChannel(psSample, &params, &iLoops);

  if (bQueued == TRUE)
    psSample->iSample = QS_CHANNEL_QUEUE;
  else
    psSample->iSample = QS_CHANNEL_FX;

  sound_SetSampleVol(psSample, AUDIO_VOL_MAX, FALSE);

  iPan = (AUDIO_PAN_MAX - AUDIO_PAN_MIN) / 2;
  sound_SetSamplePan(psSample, iPan);

  g_uiRet = QSOUND(PlayEx( g_hQMixer, psSample->iSample, QMIX_QUEUEWAVE | QMIX_PLAY_NOTIFYSTOP, psRiffData->psMixWave, iLoops, &params ))  ;
  if (g_uiRet != 0)
    goto playError;

  return TRUE;

playError:

  g_iError = QSOUND(GetLastError());
  QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
  Neuron::DebugTrace("sound_Play2DSample: {}", g_szErrMsg);
  return FALSE;
}

/***************************************************************************/

BOOL sound_Play3DSample(TRACK* psTrack, AUDIO_SAMPLE* psSample)
{
  SDWORD iLoops;
  QMIXPLAYPARAMS params;
  QMIX_DISTANCES sDistances;
  auto psRiffData = static_cast<RIFFDATA*>(psTrack->pMem);

  sound_SetupChannel(psSample, &params, &iLoops);

  psSample->iSample = QSOUND(FindChannel( g_hQMixer, QMIX_FINDCHANNEL_DISTANCE, nullptr ));
  if (psSample->iSample < 0)
    goto playError;

  /* set distance mapping for 3D */
  sDistances.cbSize = sizeof(QMIX_DISTANCES);
  sDistances.minDistance = QS_MIN_DISTANCE;
  sDistances.maxDistance = static_cast<float>(sound_TrackAudibleRadius(psSample->iTrack));
  sDistances.scale = QS_SCALE;

  g_uiRet = QSOUND(SetDistanceMapping( g_hQMixer, psSample->iSample, 0, &sDistances ))  ;

  sound_SetObjectPosition(psSample->iSample, psSample->x, psSample->y, psSample->z);

  sound_SetSampleVol(psSample, AUDIO_VOL_MAX, TRUE);

  /* clear queue on channel and play sound */
  g_uiRet = QSOUND(PlayEx( g_hQMixer, psSample->iSample, QMIX_CLEARQUEUE | QMIX_PLAY_NOTIFYSTOP, psRiffData->psMixWave, iLoops, &params ))  ;
  if (g_uiRet < 0)
    goto playError;

  return TRUE;

playError:

  g_iError = QSOUND(GetLastError());
  QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
  return FALSE;
}

/***************************************************************************/

BOOL sound_PlayStream(AUDIO_SAMPLE* psSample, char szFileName[], SDWORD iVol)
{
  QMIXPLAYPARAMS params;
  LPMIXWAVE psMixWave;
  SDWORD iPan;

  psMixWave = QSOUND(OpenWave( g_hQMixer, szFileName, nullptr, QMIX_FILESTREAM ));
  if (psMixWave == nullptr)
    goto streamError;

  memset(&params, 0, sizeof(params));
  params.dwSize = sizeof(params);
  params.callback = sound_QSoundFinishedCallback;
  params.dwUser = (DWORD)psSample;

  /* play on stream channel */
  psSample->iSample = QS_CHANNEL_STREAM;

  sound_SetSampleVol(psSample, AUDIO_VOL_MAX, FALSE);
  iPan = (AUDIO_PAN_MAX - AUDIO_PAN_MIN) / 2;
  sound_SetSamplePan(psSample, iPan);

  if (g_uiRet != 0)
    goto streamError;

  g_uiRet = QSOUND(PlayEx( g_hQMixer, psSample->iSample, QMIX_CLEARQUEUE, psMixWave, 0, &params ))  ;
  if (g_uiRet != 0)
    goto streamError;

  return TRUE;

streamError:

  g_iError = QSOUND(GetLastError());
  QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
  Neuron::DebugTrace("sound_PlayStream: {}", g_szErrMsg);
  return FALSE;
}

/***************************************************************************/

void sound_StopSample(SDWORD iSample)
{
  g_uiRet = QSOUND(FlushChannel( g_hQMixer, iSample, 0 ));

  if (g_uiRet != 0)
  {
    g_iError = QSOUND(GetLastError());
    QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
    Neuron::DebugTrace("sound_StopSample: {}", g_szErrMsg);
  }
}

/***************************************************************************/

static void sound_SetSamplePan(AUDIO_SAMPLE* psSample, int iPan)
{
  g_uiRet = QSOUND(SetPan( g_hQMixer, psSample->iSample, 0, iPan*30/AUDIO_PAN_RANGE ))  ;

  if (g_uiRet != 0)
  {
    g_iError = QSOUND(GetLastError());
    QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
    Neuron::DebugTrace("sound_SetSamplePan: {}", g_szErrMsg);
  }
}

/***************************************************************************/

static void sound_SetSampleVol(AUDIO_SAMPLE* psSample, SDWORD iVol, BOOL bScale3D)
{
  g_uiRet = QSOUND(SetVolume( g_hQMixer, psSample->iSample, 0, audio_GetSampleMixVol(psSample,iVol,bScale3D) ))  ;
  if (g_uiRet != 0)
  {
    g_iError = QSOUND(GetLastError());
    QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
    Neuron::DebugTrace("sound_SetSampleVol: sample {} {}\n", psSample->iSample, g_szErrMsg);
  }
}

/***************************************************************************/

void sound_SetPlayerPos(SDWORD iX, SDWORD iY, SDWORD iZ)
{
  QSVECTOR vPosition;

  vPosition.x = static_cast<float>(iX);
  vPosition.y = static_cast<float>(iY);
  vPosition.z = static_cast<float>(iZ);

  g_uiRet = QSOUND(SetListenerPosition( g_hQMixer, &vPosition, 0 ));
  if (g_uiRet != 0)
  {
    g_iError = QSOUND(GetLastError());
    QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
    Neuron::DebugTrace("sound_SetPlayerPos: {}", g_szErrMsg);
  }
}

/***************************************************************************/
/*
 * sound_SetPlayerOrientation
 *
 * Orientation given as angles in degrees: QSound expects position vector
 */
/***************************************************************************/

void sound_SetPlayerOrientation(SDWORD iX, SDWORD iY, SDWORD iZ)
{
  QSVECTOR vUp = {0.0f, 0.0f, 1.0f}, vDirection;

  vDirection.x = static_cast<float>(-sin(((float)iZ) * PI / 180.0f));
  vDirection.y = static_cast<float>(cos(((float)iZ) * PI / 180.0f));
  vDirection.z = 0.0f;

  g_uiRet = QSOUND(SetListenerOrientation( g_hQMixer, &vDirection, &vUp, 0 ));
  if (g_uiRet != 0)
  {
    g_iError = QSOUND(GetLastError());
    QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
    Neuron::DebugTrace("sound_SetPlayerOrientation: {}", g_szErrMsg);
  }
}

/***************************************************************************/

void sound_SetObjectPosition(SDWORD iSample, SDWORD iX, SDWORD iY, SDWORD iZ)
{
  QSVECTOR vPosition;

  vPosition.x = static_cast<float>(iX);
  vPosition.y = static_cast<float>(iY);
  vPosition.z = static_cast<float>(iZ);

  g_uiRet = QSOUND(SetSourcePosition( g_hQMixer, iSample, 0, &vPosition ))  ;
  if (g_uiRet != 0)
  {
    g_iError = QSOUND(GetLastError());
    QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
    Neuron::DebugTrace("sound_SetObjectPosition: sample {} {}\n", iSample, g_szErrMsg);
  }
}

/***************************************************************************/

void sound_StopAll(void)
{
  g_uiRet = QSOUND(FlushChannel( g_hQMixer, 0, QMIX_ALL ));

  if (g_uiRet != 0)
  {
    g_iError = QSOUND(GetLastError());
    QSOUND(GetErrorText( g_iError, g_szErrMsg, MAX_STR ));
    Neuron::DebugTrace("sound_StopAll: {}", g_szErrMsg);
  }
}

/***************************************************************************/

