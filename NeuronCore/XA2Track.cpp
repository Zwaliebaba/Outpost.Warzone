#include "pch.h"
/***************************************************************************/
/*
 * XAudio2 implementation of the sound library interface in TrackLib.h.
 *
 * Replaces QSTrack.cpp, which was the QMixer one. The interface above it is
 * unchanged, so Track.cpp and Audio.cpp are untouched by the swap except
 * where they reached for something only QMixer had.
 *
 * Two things carry over deliberately, because the code above depends on them.
 * AUDIO_SAMPLE::iSample is this module's handle, and the first three voice
 * slots keep the meanings QMixer's fixed channels had: speech queues on one,
 * the single stream plays on another, and 2D effects share a third the way
 * they shared one channel. A fourth is music, which used to be the CD and so
 * was independent of the stream channel -- a briefing's audio must not stop
 * the music, and pausing the music must survive the briefing. The rest are
 * the pool 3D sounds are drawn from.
 */
/***************************************************************************/

#include <windows.h>
#include <mmreg.h>
#include <xaudio2.h>

#include "Frame.h"
#include "TrackLib.h"
#include "Audio.h"

/***************************************************************************/

#define	PI	 3.14159265359

/* Slot layout. The three fixed slots are the ones Audio.cpp routes to by
 * meaning rather than by allocation.
 */
#define	XA2_SLOT_QUEUE			0
#define	XA2_SLOT_STREAM			1
#define	XA2_SLOT_FX				2
#define	XA2_SLOT_MUSIC			3
#define	XA2_FIXED_SLOTS			4

/* QMixer was configured for 200 channels with 20 prioritised for 3D. The game
 * has never had anything like that many audible at once.
 */
#define	XA2_MAX_SLOTS			32

/* A sound queued behind another on a fixed slot. XAudio2 will queue buffers on
 * a voice by itself, but only in the voice's own format, and the game's WAVs
 * run from 11025 to 22050 Hz -- SetSourceSampleRate cannot be called while
 * buffers are still queued. So the queue is kept here and the next sound
 * starts when the voice reports the previous one done.
 */
#define	XA2_PENDING_MAX			16

/* QMixer's distance mapping, which every 3D sound was given: audible out to
 * the track's radius, no attenuation within the minimum, and a rolloff scale
 * between the two.
 */
#define	XA2_MIN_DISTANCE		(300.0f)
#define	XA2_ROLLOFF				(1.5f)

/* iSample carries a generation for pool slots, so a handle held past the end
 * of its sound cannot stop whatever has taken the slot over since. Generations
 * start at one, which keeps every pool handle above the fixed slot indices.
 * The fixed slots need none: stopping one is meant to flush the channel, which
 * is what QMixer's FlushChannel did.
 */
#define	XA2_SLOT_BITS			8
#define	XA2_SLOT_MASK			0xFF
#define	XA2_GENERATION_MAX		0x7FFFFF

/***************************************************************************/

/* What TRACK::pMem points at: the decoded samples, normalised at load to
 * 16 bit mono so that one pool of voices can play any of them.
 */
using TRACKDATA = struct TRACKDATA
{
  UBYTE* pSamples;
  UDWORD udwBytes;
  UDWORD udwRate;
};

using PENDING = struct PENDING
{
  TRACK* psTrack;
  AUDIO_SAMPLE* psSample;
  SDWORD iBaseVol;
  BOOL bLoop;
};

using VOICE_SLOT = struct VOICE_SLOT
{
  IXAudio2SourceVoice* pVoice;
  TRACK* psTrack; /* what is playing, for sound_FreeTrack   */
  AUDIO_SAMPLE* psSample; /* null when nothing is playing on it     */
  UDWORD udwGeneration;
  UDWORD udwRate; /* rate the voice is currently tuned to    */
  BOOL bBusy; /* playing, or holding queued sounds      */
  BOOL bPositional;
  BOOL bMusic;
  SDWORD iBaseVol; /* 0 -> sound_GetMaxVolume()              */
  SDWORD iRadius; /* audible radius of the track            */
  SDWORD x, y, z;
  PENDING aPending[XA2_PENDING_MAX];
  SDWORD iPending;
};

/***************************************************************************/

static IXAudio2* g_pXAudio2 = nullptr;
static IXAudio2MasteringVoice* g_pMasterVoice = nullptr;
static VOICE_SLOT g_aSlots[XA2_MAX_SLOTS];
static UDWORD g_udwOutputChannels = 2;

/* The stream and music voices are built per file, because unlike the pooled
 * voices they have to take the file's own channel count. Indexed by slot;
 * only those two entries are ever used.
 */
static UBYTE* g_apStreamData[XA2_FIXED_SLOTS] = {nullptr, nullptr, nullptr, nullptr};

static SDWORD g_iGlobalVolume = AUDIO_VOL_MAX;
static SDWORD g_iMusicVolume = AUDIO_VOL_MAX;

/* Listener position, and the unit vector pointing to the listener's right in
 * the audio plane, from which a sound's pan is taken.
 */
static SDWORD g_iListenerX = 0, g_iListenerY = 0, g_iListenerZ = 0;
static float g_fRightX = 1.0f, g_fRightY = 0.0f;

/* Completion crosses a thread boundary here and nowhere else. XAudio2 calls
 * OnStreamEnd on its own audio thread; all that does is set the slot's bit,
 * and sound_Update drains them on the game thread once a frame.
 */
static CRITICAL_SECTION g_csFinished;
static BOOL g_bFinishedInit = FALSE;
static UDWORD g_udwFinishedMask = 0;

/***************************************************************************/

static void xa2_ApplySlotOutput(SDWORD iSlot);
static void xa2_StopStreamSlot(SDWORD iSlot, BOOL bNotify);

/***************************************************************************/

class SlotCallback final : public IXAudio2VoiceCallback
{
public:
  SDWORD iSlot = -1;

  void STDMETHODCALLTYPE OnStreamEnd() override
  {
    if (iSlot < 0 || g_bFinishedInit == FALSE)
      return;

    EnterCriticalSection(&g_csFinished);
    g_udwFinishedMask |= 1u << iSlot;
    LeaveCriticalSection(&g_csFinished);
  }

  void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
  void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
  void STDMETHODCALLTYPE OnBufferStart(void*) override {}
  void STDMETHODCALLTYPE OnBufferEnd(void*) override {}
  void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
  void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
};

static SlotCallback g_aCallbacks[XA2_MAX_SLOTS];

/***************************************************************************/
/* handles */
/***************************************************************************/

static SDWORD xa2_Handle(SDWORD iSlot)
{
  if (iSlot < XA2_FIXED_SLOTS)
    return iSlot;

  return static_cast<SDWORD>((g_aSlots[iSlot].udwGeneration << XA2_SLOT_BITS) | static_cast<UDWORD>(iSlot));
}

/* Returns the slot a handle refers to, or -1 if the sound it was issued for
 * has since finished and the slot been reused.
 */
static SDWORD xa2_SlotFromHandle(SDWORD iSample)
{
  SDWORD iSlot;
  UDWORD udwGeneration;

  if (iSample < 0)
    return -1;

  if (iSample < XA2_FIXED_SLOTS)
    return iSample;

  iSlot = iSample & XA2_SLOT_MASK;
  udwGeneration = static_cast<UDWORD>(iSample) >> XA2_SLOT_BITS;

  if (iSlot < XA2_FIXED_SLOTS || iSlot >= XA2_MAX_SLOTS)
    return -1;

  if (g_aSlots[iSlot].udwGeneration != udwGeneration)
    return -1;

  return iSlot;
}

/***************************************************************************/
/* volume and panning */
/***************************************************************************/

int sound_GetMaxVolume(void) { return 32767; }

/***************************************************************************/

/* Distance attenuation, replacing QMixer's SetDistanceMapping: inverse
 * distance with the same minimum, maximum and rolloff it was given.
 */
static float xa2_Attenuation(const VOICE_SLOT* psSlot)
{
  const auto fdX = static_cast<float>(psSlot->x - g_iListenerX);
  const auto fdY = static_cast<float>(psSlot->y - g_iListenerY);
  const auto fdZ = static_cast<float>(psSlot->z - g_iListenerZ);
  const float fDistance = sqrtf(fdX * fdX + fdY * fdY + fdZ * fdZ);
  const auto fMaxDistance = static_cast<float>(psSlot->iRadius);

  if (fMaxDistance > 0.0f && fDistance >= fMaxDistance)
    return 0.0f;

  if (fDistance <= XA2_MIN_DISTANCE)
    return 1.0f;

  return XA2_MIN_DISTANCE / (XA2_MIN_DISTANCE + XA2_ROLLOFF * (fDistance - XA2_MIN_DISTANCE));
}

/***************************************************************************/

/* Constant power stereo pan from the emitter's bearing relative to the
 * listener's right, which is where QMixer's listener orientation went.
 */
static void xa2_Pan(const VOICE_SLOT* psSlot, float* pfLeft, float* pfRight)
{
  const auto fdX = static_cast<float>(psSlot->x - g_iListenerX);
  const auto fdY = static_cast<float>(psSlot->y - g_iListenerY);
  const float fLength = sqrtf(fdX * fdX + fdY * fdY);
  float fPan = 0.0f;

  if (fLength > 0.0f)
    fPan = (fdX * g_fRightX + fdY * g_fRightY) / fLength;

  if (fPan < -1.0f)
    fPan = -1.0f;
  else if (fPan > 1.0f)
    fPan = 1.0f;

  *pfLeft = sqrtf((1.0f - fPan) * 0.5f);
  *pfRight = sqrtf((1.0f + fPan) * 0.5f);
}

/***************************************************************************/

static void xa2_ApplySlotOutput(SDWORD iSlot)
{
  VOICE_SLOT* psSlot = &g_aSlots[iSlot];
  float afMatrix[XAUDIO2_MAX_AUDIO_CHANNELS];
  float fLeft, fRight;
  UDWORD i;

  if (psSlot->pVoice == nullptr)
    return;

  auto fVolume = static_cast<float>(psSlot->iBaseVol) / static_cast<float>(sound_GetMaxVolume());

  if (psSlot->bMusic == TRUE)
    fVolume = fVolume * static_cast<float>(g_iMusicVolume) / static_cast<float>(AUDIO_VOL_MAX);

  if (psSlot->bPositional == TRUE)
    fVolume *= xa2_Attenuation(psSlot);

  psSlot->pVoice->SetVolume(fVolume);

  /* Only positional sounds are panned; everything else keeps the default
   * matrix, which is what the 2D channels had.
   */
  if (psSlot->bPositional == FALSE || g_udwOutputChannels < 2 || g_udwOutputChannels > XAUDIO2_MAX_AUDIO_CHANNELS)
    return;

  xa2_Pan(psSlot, &fLeft, &fRight);

  for (i = 0; i < g_udwOutputChannels; i++)
    afMatrix[i] = 0.0f;

  /* Channels zero and one are front left and right in every layout XAudio2
   * reports, so a surround setup gets the sound across its front stage.
   */
  afMatrix[0] = fLeft;
  afMatrix[1] = fRight;

  psSlot->pVoice->SetOutputMatrix(nullptr, 1, g_udwOutputChannels, afMatrix);
}

/***************************************************************************/
/*
 * The master volume, which the FX slider and Loop.cpp's mute around video
 * playback move. It used to be waveOutSetVolume on one side and the Windows
 * mixer's wave line on the other, which are different controls -- so the game
 * saved one and restored the other. It is the mastering voice now, and the
 * two halves cannot disagree.
 */
/***************************************************************************/

SDWORD sound_GetGlobalVolume(void) { return g_iGlobalVolume; }

/***************************************************************************/

void sound_SetGlobalVolume(SDWORD iVol)
{
  if (iVol < AUDIO_VOL_MIN)
    iVol = AUDIO_VOL_MIN;
  else if (iVol > AUDIO_VOL_MAX)
    iVol = AUDIO_VOL_MAX;

  g_iGlobalVolume = iVol;

  if (g_pMasterVoice != nullptr)
    g_pMasterVoice->SetVolume(static_cast<float>(iVol) / static_cast<float>(AUDIO_VOL_MAX));
}

/***************************************************************************/

SDWORD sound_GetMusicVolume(void) { return g_iMusicVolume; }

/***************************************************************************/

void sound_SetMusicVolume(SDWORD iVol)
{
  if (iVol < AUDIO_VOL_MIN)
    iVol = AUDIO_VOL_MIN;
  else if (iVol > AUDIO_VOL_MAX)
    iVol = AUDIO_VOL_MAX;

  g_iMusicVolume = iVol;

  /* take effect on what is playing, not just on the next track */
  if (g_aSlots[XA2_SLOT_MUSIC].bBusy == TRUE)
    xa2_ApplySlotOutput(XA2_SLOT_MUSIC);
}

/***************************************************************************/
/* slots */
/***************************************************************************/

/* A voice is not clear the moment it is flushed: the buffer has to drain
 * before SetSourceSampleRate is legal on it again.
 */
static BOOL xa2_SlotIsClear(SDWORD iSlot)
{
  XAUDIO2_VOICE_STATE sState;

  if (g_aSlots[iSlot].pVoice == nullptr)
    return FALSE;

  g_aSlots[iSlot].pVoice->GetState(&sState, XAUDIO2_VOICE_NOSAMPLESPLAYED);

  return sState.BuffersQueued == 0 ? TRUE : FALSE;
}

/***************************************************************************/

/* Waits for a flushed voice to let go of the buffer it was given. XAudio2
 * removes buffers on its own thread, so a flush is not the moment the memory
 * stops being read -- and sound_FreeTrack is about to give it back.
 */
static void xa2_WaitForSlotClear(SDWORD iSlot)
{
  SDWORD iSpin;

  for (iSpin = 0; iSpin < 100 && xa2_SlotIsClear(iSlot) == FALSE; iSpin++)
    Sleep(1);
}

/***************************************************************************/

/* Stops a slot and hands its samples back. QMixer played everything with
 * QMIX_PLAY_NOTIFYSTOP, so a flushed sound reported completion just as a
 * finished one did, and Audio.cpp relies on that to free the sample.
 */
static void xa2_ReleaseSlot(SDWORD iSlot, BOOL bNotify)
{
  VOICE_SLOT* psSlot = &g_aSlots[iSlot];
  AUDIO_SAMPLE* psSample = psSlot->psSample;
  PENDING aPending[XA2_PENDING_MAX];
  const SDWORD iPending = psSlot->iPending;
  SDWORD i;

  if (psSlot->pVoice != nullptr)
  {
    psSlot->pVoice->Stop(0, XAUDIO2_COMMIT_NOW);
    psSlot->pVoice->FlushSourceBuffers();
  }

  for (i = 0; i < iPending; i++)
    aPending[i] = psSlot->aPending[i];

  /* the slot is clear before any callback runs, so one that starts a new
   * sound finds it free rather than half torn down
   */
  psSlot->iPending = 0;
  psSlot->psTrack = nullptr;
  psSlot->psSample = nullptr;
  psSlot->bBusy = FALSE;
  psSlot->bPositional = FALSE;
  psSlot->bMusic = FALSE;

  if (bNotify == FALSE || sound_GetSystemActive() == FALSE)
    return;

  if (psSample != nullptr)
    sound_FinishedCallback(psSample);

  for (i = 0; i < iPending; i++)
  {
    if (aPending[i].psSample != nullptr)
      sound_FinishedCallback(aPending[i].psSample);
  }
}

/***************************************************************************/

/* Submits a track to a slot and starts it. The slot must be free and clear. */
static BOOL xa2_StartSlot(SDWORD iSlot, TRACK* psTrack, AUDIO_SAMPLE* psSample, SDWORD iBaseVol, BOOL bLoop)
{
  VOICE_SLOT* psSlot = &g_aSlots[iSlot];
  auto psData = static_cast<TRACKDATA*>(psTrack->pMem);
  XAUDIO2_BUFFER sBuffer;

  if (psSlot->pVoice == nullptr || psData == nullptr || psData->pSamples == nullptr)
    return FALSE;

  if (xa2_SlotIsClear(iSlot) == FALSE)
    return FALSE;

  if (psSlot->udwRate != psData->udwRate)
  {
    if (FAILED(psSlot->pVoice->SetSourceSampleRate(psData->udwRate)))
    {
      Neuron::DebugTrace("xa2_StartSlot: SetSourceSampleRate {} failed on slot {}\n", psData->udwRate, iSlot);
      return FALSE;
    }
    psSlot->udwRate = psData->udwRate;
  }

  memset(&sBuffer, 0, sizeof(sBuffer));
  sBuffer.Flags = XAUDIO2_END_OF_STREAM;
  sBuffer.AudioBytes = psData->udwBytes;
  sBuffer.pAudioData = psData->pSamples;
  sBuffer.LoopCount = bLoop == TRUE ? XAUDIO2_LOOP_INFINITE : 0;

  if (FAILED(psSlot->pVoice->SubmitSourceBuffer(&sBuffer, nullptr)))
    return FALSE;

  psSlot->psTrack = psTrack;
  psSlot->psSample = psSample;
  psSlot->iBaseVol = iBaseVol;
  psSlot->bBusy = TRUE;

  xa2_ApplySlotOutput(iSlot);

  if (FAILED(psSlot->pVoice->Start(0, XAUDIO2_COMMIT_NOW)))
  {
    psSlot->pVoice->FlushSourceBuffers();
    psSlot->psTrack = nullptr;
    psSlot->psSample = nullptr;
    psSlot->bBusy = psSlot->iPending > 0 ? TRUE : FALSE;
    return FALSE;
  }

  return TRUE;
}

/***************************************************************************/

/* Play on a fixed slot, queueing behind whatever is on it. This is what
 * QMIX_QUEUEWAVE did on the speech and effects channels.
 */
static BOOL xa2_PlayOnFixedSlot(SDWORD iSlot, TRACK* psTrack, AUDIO_SAMPLE* psSample, SDWORD iBaseVol, BOOL bLoop)
{
  VOICE_SLOT* psSlot = &g_aSlots[iSlot];
  PENDING* psPending;

  if (psSlot->psSample == nullptr && psSlot->iPending == 0 && xa2_StartSlot(iSlot, psTrack, psSample, iBaseVol, bLoop) == TRUE)
    return TRUE;

  if (psSlot->iPending >= XA2_PENDING_MAX)
  {
    Neuron::DebugTrace("xa2_PlayOnFixedSlot: slot {} queue full, dropping {}\n", iSlot, psTrack->pName);
    return FALSE;
  }

  psPending = &psSlot->aPending[psSlot->iPending++];
  psPending->psTrack = psTrack;
  psPending->psSample = psSample;
  psPending->iBaseVol = iBaseVol;
  psPending->bLoop = bLoop;

  /* queued counts as on the slot: stopping it has to flush the queue too */
  psSlot->bBusy = TRUE;

  return TRUE;
}

/***************************************************************************/

/* Starts whatever is queued on a fixed slot, once it is free again. Called
 * every frame, because a slot can also fall idle without a completion -- an
 * immediate start refused because the voice had not finished draining.
 */
static void xa2_PumpFixedSlot(SDWORD iSlot)
{
  VOICE_SLOT* psSlot = &g_aSlots[iSlot];

  while (psSlot->psSample == nullptr && psSlot->iPending > 0)
  {
    const PENDING sNext = psSlot->aPending[0];
    SDWORD i;

    for (i = 1; i < psSlot->iPending; i++)
      psSlot->aPending[i - 1] = psSlot->aPending[i];
    psSlot->iPending--;

    if (xa2_StartSlot(iSlot, sNext.psTrack, sNext.psSample, sNext.iBaseVol, sNext.bLoop) == TRUE)
      return;

    /* The voice is not ready yet, so put it back and try next frame. */
    if (xa2_SlotIsClear(iSlot) == FALSE)
    {
      for (i = psSlot->iPending; i > 0; i--)
        psSlot->aPending[i] = psSlot->aPending[i - 1];
      psSlot->aPending[0] = sNext;
      psSlot->iPending++;
      return;
    }

    /* It will never play, so report it done or Audio.cpp keeps the sample. */
    if (sNext.psSample != nullptr && sound_GetSystemActive() == TRUE)
      sound_FinishedCallback(sNext.psSample);
  }

  psSlot->bBusy = psSlot->psSample != nullptr || psSlot->iPending > 0 ? TRUE : FALSE;
}

/***************************************************************************/

/* Takes a slot from the pool for a 3D sound: a free one if there is one, else
 * the sound furthest from the listener, which is what QMixer's
 * QMIX_FINDCHANNEL_DISTANCE picked.
 */
static SDWORD xa2_AcquirePoolSlot(void)
{
  SDWORD i, iFurthest = -1;
  float fFurthest = -1.0f;

  for (i = XA2_FIXED_SLOTS; i < XA2_MAX_SLOTS; i++)
  {
    if (g_aSlots[i].bBusy == FALSE && xa2_SlotIsClear(i) == TRUE)
      return i;
  }

  for (i = XA2_FIXED_SLOTS; i < XA2_MAX_SLOTS; i++)
  {
    if (g_aSlots[i].bPositional == FALSE)
      continue;

    const auto fdX = static_cast<float>(g_aSlots[i].x - g_iListenerX);
    const auto fdY = static_cast<float>(g_aSlots[i].y - g_iListenerY);
    const auto fdZ = static_cast<float>(g_aSlots[i].z - g_iListenerZ);
    const float fDistance = fdX * fdX + fdY * fdY + fdZ * fdZ;

    if (fDistance > fFurthest)
    {
      fFurthest = fDistance;
      iFurthest = i;
    }
  }

  if (iFurthest < 0)
    return -1;

  xa2_ReleaseSlot(iFurthest, TRUE);

  /* a flushed voice is not clear until its buffer has drained */
  if (xa2_SlotIsClear(iFurthest) == FALSE)
    return -1;

  return iFurthest;
}

/***************************************************************************/
/* RIFF reading */
/***************************************************************************/

/* Reads the format and data chunks of a RIFF WAVE. The caller owns both
 * returned blocks, and owns them even when this fails part way. pbCompressed
 * reports a fact chunk, which is what the code above uses to refuse a track
 * this backend cannot decode.
 */
static BOOL xa2_ReadRiff(HMMIO hmmio, WAVEFORMATEX** ppFormat, UBYTE** ppData, UDWORD* pudwBytes, BOOL* pbCompressed)
{
  MMCKINFO waveChunk, factChunk, formatChunk, dataChunk;
  UDWORD udwFormatSize;

  *ppFormat = nullptr;
  *ppData = nullptr;
  *pudwBytes = 0;
  *pbCompressed = FALSE;

  waveChunk.fccType = mmioFOURCC('W', 'A', 'V', 'E');
  if (mmioDescend(hmmio, &waveChunk, nullptr, MMIO_FINDRIFF))
    return FALSE;

  /* a fact chunk means the data is compressed */
  factChunk.ckid = mmioFOURCC('f', 'a', 'c', 't');
  if (mmioDescend(hmmio, &factChunk, &waveChunk, MMIO_FINDCHUNK) == 0 && factChunk.cksize >= sizeof(DWORD))
    *pbCompressed = TRUE;

  mmioSeek(hmmio, 0, SEEK_SET);
  if (mmioDescend(hmmio, &waveChunk, nullptr, MMIO_FINDRIFF))
    return FALSE;

  formatChunk.ckid = mmioFOURCC('f', 'm', 't', ' ');
  if (mmioDescend(hmmio, &formatChunk, &waveChunk, MMIO_FINDCHUNK))
    return FALSE;

  if (formatChunk.cksize < sizeof(PCMWAVEFORMAT))
    return FALSE;

  /* The format chunk is WAVEFORMATEX plus a variable tail, so it is read as
   * bytes. A bare PCM one is shorter than the struct, hence the floor.
   */
  udwFormatSize = formatChunk.cksize < sizeof(WAVEFORMATEX) ? sizeof(WAVEFORMATEX) : formatChunk.cksize;

  *ppFormat = (WAVEFORMATEX*)new (std::nothrow) UBYTE[udwFormatSize];
  if (*ppFormat == nullptr)
    return FALSE;

  memset(*ppFormat, 0, udwFormatSize);
  if (mmioRead(hmmio, (char*)*ppFormat, formatChunk.cksize) != static_cast<LONG>(formatChunk.cksize))
    return FALSE;

  mmioAscend(hmmio, &formatChunk, 0);

  dataChunk.ckid = mmioFOURCC('d', 'a', 't', 'a');
  if (mmioDescend(hmmio, &dataChunk, &waveChunk, MMIO_FINDCHUNK))
    return FALSE;

  if (dataChunk.cksize == 0)
    return FALSE;

  *ppData = new (std::nothrow) UBYTE[dataChunk.cksize];
  if (*ppData == nullptr)
    return FALSE;

  if (mmioRead(hmmio, (char*)*ppData, dataChunk.cksize) != static_cast<LONG>(dataChunk.cksize))
    return FALSE;

  *pudwBytes = dataChunk.cksize;

  mmioAscend(hmmio, &dataChunk, 0);

  return TRUE;
}

/***************************************************************************/

/* Widens 8 bit unsigned PCM to 16 bit signed, and folds stereo to mono when
 * asked. XAudio2 takes 8 bit directly, but 16 bit is its optimal format and,
 * more to the point, one format for every track means one pool of voices:
 * SetSourceSampleRate can retune a voice, but nothing can restate its sample
 * size or channel count.
 */
static BOOL xa2_Normalise(const WAVEFORMATEX* psFormat, const UBYTE* pRaw, UDWORD udwRawBytes, BOOL bToMono, UBYTE** ppOut,
                          UDWORD* pudwOutBytes, WORD* puChannels)
{
  const WORD uChannels = psFormat->nChannels;
  UDWORD i;
  SWORD* psOut;

  if (psFormat->wFormatTag != WAVE_FORMAT_PCM)
    return FALSE;

  if (uChannels != 1 && uChannels != 2)
    return FALSE;

  if (psFormat->wBitsPerSample != 8 && psFormat->wBitsPerSample != 16)
    return FALSE;

  const UDWORD udwFrames = udwRawBytes / (psFormat->wBitsPerSample / 8) / uChannels;
  if (udwFrames == 0)
    return FALSE;

  const WORD uOutChannels = bToMono == TRUE ? 1 : uChannels;

  psOut = new (std::nothrow) SWORD[udwFrames * uOutChannels];
  if (psOut == nullptr)
    return FALSE;

  for (i = 0; i < udwFrames; i++)
  {
    SDWORD iLeft, iRight;

    if (psFormat->wBitsPerSample == 8)
    {
      /* 8 bit wave data is unsigned, centred on 128 */
      iLeft = (static_cast<SDWORD>(pRaw[i * uChannels]) - 128) << 8;
      iRight = uChannels == 2 ? (static_cast<SDWORD>(pRaw[i * uChannels + 1]) - 128) << 8 : iLeft;
    }
    else
    {
      auto psRaw = reinterpret_cast<const SWORD*>(pRaw);
      iLeft = psRaw[i * uChannels];
      iRight = uChannels == 2 ? psRaw[i * uChannels + 1] : iLeft;
    }

    if (uOutChannels == 1)
      psOut[i] = static_cast<SWORD>(uChannels == 2 ? (iLeft + iRight) / 2 : iLeft);
    else
    {
      psOut[i * 2] = static_cast<SWORD>(iLeft);
      psOut[i * 2 + 1] = static_cast<SWORD>(iRight);
    }
  }

  *ppOut = reinterpret_cast<UBYTE*>(psOut);
  *pudwOutBytes = udwFrames * uOutChannels * static_cast<UDWORD>(sizeof(SWORD));
  *puChannels = uOutChannels;

  return TRUE;
}

/***************************************************************************/
/* library */
/***************************************************************************/

BOOL sound_InitLibrary(void)
{
  WAVEFORMATEX sFormat;
  XAUDIO2_VOICE_DETAILS sDetails;
  SDWORD i;

  memset(g_aSlots, 0, sizeof(g_aSlots));

  InitializeCriticalSection(&g_csFinished);
  g_udwFinishedMask = 0;
  g_bFinishedInit = TRUE;

  if (FAILED(XAudio2Create(&g_pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR)))
  {
    Neuron::DebugTrace("sound_InitLibrary: XAudio2Create failed\n");
    g_pXAudio2 = nullptr;
    goto initError;
  }

  if (FAILED(g_pXAudio2->CreateMasteringVoice(&g_pMasterVoice)))
  {
    Neuron::DebugTrace("sound_InitLibrary: CreateMasteringVoice failed\n");
    g_pMasterVoice = nullptr;
    goto initError;
  }

  g_pMasterVoice->GetVoiceDetails(&sDetails);
  g_udwOutputChannels = sDetails.InputChannels;

  /* The pooled voices are 16 bit mono. The rate here is only where they
   * start: each is retuned to the track it is given.
   */
  memset(&sFormat, 0, sizeof(sFormat));
  sFormat.wFormatTag = WAVE_FORMAT_PCM;
  sFormat.nChannels = 1;
  sFormat.nSamplesPerSec = KHZ22;
  sFormat.wBitsPerSample = 16;
  sFormat.nBlockAlign = 2;
  sFormat.nAvgBytesPerSec = KHZ22 * 2;

  for (i = 0; i < XA2_MAX_SLOTS; i++)
  {
    g_aCallbacks[i].iSlot = i;
    g_aSlots[i].udwGeneration = 1;
    g_aSlots[i].udwRate = KHZ22;

    /* the stream and music voices take the file's own format, so they are
     * built when one starts rather than here
     */
    if (i == XA2_SLOT_STREAM || i == XA2_SLOT_MUSIC)
      continue;

    if (FAILED(g_pXAudio2->CreateSourceVoice(&g_aSlots[i].pVoice, &sFormat, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &g_aCallbacks[i],
                                             nullptr, nullptr)))
    {
      Neuron::DebugTrace("sound_InitLibrary: CreateSourceVoice failed for slot {}\n", i);
      g_aSlots[i].pVoice = nullptr;
      goto initError;
    }
  }

  sound_SetGlobalVolume(g_iGlobalVolume);

  return TRUE;

initError:

  sound_ShutdownLibrary();
  return FALSE;
}

/***************************************************************************/

void sound_ShutdownLibrary(void)
{
  SDWORD i;

  for (i = 0; i < XA2_MAX_SLOTS; i++)
  {
    if (g_aSlots[i].pVoice != nullptr)
    {
      /* DestroyVoice waits for the audio thread, so no callback can still be
       * in flight once it returns
       */
      g_aSlots[i].pVoice->Stop(0, XAUDIO2_COMMIT_NOW);
      g_aSlots[i].pVoice->DestroyVoice();
      g_aSlots[i].pVoice = nullptr;
    }
    g_aSlots[i].psTrack = nullptr;
    g_aSlots[i].psSample = nullptr;
    g_aSlots[i].bBusy = FALSE;
    g_aSlots[i].iPending = 0;
  }

  for (i = 0; i < XA2_FIXED_SLOTS; i++)
  {
    if (g_apStreamData[i] != nullptr)
    {
      delete[] g_apStreamData[i];
      g_apStreamData[i] = nullptr;
    }
  }

  if (g_pMasterVoice != nullptr)
  {
    g_pMasterVoice->DestroyVoice();
    g_pMasterVoice = nullptr;
  }

  if (g_pXAudio2 != nullptr)
  {
    g_pXAudio2->Release();
    g_pXAudio2 = nullptr;
  }

  if (g_bFinishedInit == TRUE)
  {
    g_bFinishedInit = FALSE;
    g_udwFinishedMask = 0;
    DeleteCriticalSection(&g_csFinished);
  }
}

/***************************************************************************/
/* tracks */
/***************************************************************************/

BOOL sound_ReadTrackFromBuffer(TRACK* psTrack, void* pBuffer, UDWORD udwSize)
{
  MMIOINFO mmioInfo;
  HMMIO hmmio;
  WAVEFORMATEX* psFormat = nullptr;
  UBYTE* pRaw = nullptr;
  UBYTE* pSamples = nullptr;
  UDWORD udwRawBytes = 0, udwBytes = 0, udwRate = KHZ22;
  WORD uChannels = 1;
  TRACKDATA* psData;
  BOOL bOK;

  memset(&mmioInfo, 0, sizeof(MMIOINFO));
  mmioInfo.fccIOProc = FOURCC_MEM;
  mmioInfo.cchBuffer = udwSize;
  mmioInfo.pchBuffer = static_cast<HPSTR>(pBuffer);

  hmmio = mmioOpen(nullptr, &mmioInfo, MMIO_READ);
  if (!hmmio)
    return FALSE;

  bOK = xa2_ReadRiff(hmmio, &psFormat, &pRaw, &udwRawBytes, &psTrack->bCompressed);
  mmioClose(hmmio, 0);

  if (bOK == TRUE)
  {
    udwRate = psFormat->nSamplesPerSec;
    bOK = xa2_Normalise(psFormat, pRaw, udwRawBytes, TRUE, &pSamples, &udwBytes, &uChannels);
  }

  delete[] (UBYTE*)psFormat;
  delete[] pRaw;

  if (bOK == FALSE || udwRate == 0)
  {
    delete[] pSamples;
    Neuron::Fatal("sound_ReadTrackFromBuffer: cannot decode {}", psTrack->pName);
    return FALSE;
  }

  psData = new (std::nothrow) TRACKDATA[1];
  if (psData == nullptr)
  {
    delete[] pSamples;
    return FALSE;
  }

  psData->pSamples = pSamples;
  psData->udwBytes = udwBytes;
  psData->udwRate = udwRate;

  /* duration in milliseconds, which sound_GetTrackTime hands out */
  psTrack->iTime = static_cast<SDWORD>(udwBytes / sizeof(SWORD) * 1000 / udwRate) + 1;
  psTrack->pMem = psData;

  return TRUE;
}

/***************************************************************************/

void sound_FreeTrack(TRACK* psTrack)
{
  auto psData = static_cast<TRACKDATA*>(psTrack->pMem);
  SDWORD i;

  if (psData == nullptr)
  {
    Neuron::DebugTrace("sound_FreeTrack: invalid data pointer\n");
    return;
  }

  /* XAudio2 keeps the pointer a submitted buffer was given until the buffer
   * has been consumed, so nothing may still be reading these samples
   */
  for (i = 0; i < XA2_MAX_SLOTS; i++)
  {
    SDWORD j;

    if (g_aSlots[i].psTrack == psTrack)
    {
      xa2_ReleaseSlot(i, FALSE);
      xa2_WaitForSlotClear(i);
    }

    for (j = g_aSlots[i].iPending - 1; j >= 0; j--)
    {
      if (g_aSlots[i].aPending[j].psTrack == psTrack)
      {
        SDWORD k;
        for (k = j + 1; k < g_aSlots[i].iPending; k++)
          g_aSlots[i].aPending[k - 1] = g_aSlots[i].aPending[k];
        g_aSlots[i].iPending--;
      }
    }
  }

  delete[] psData->pSamples;
  delete[] psData;

  psTrack->pMem = nullptr;
}

/***************************************************************************/
/* playing */
/***************************************************************************/

BOOL sound_Play2DSample(TRACK* psTrack, AUDIO_SAMPLE* psSample, BOOL bQueued)
{
  const SDWORD iSlot = bQueued == TRUE ? XA2_SLOT_QUEUE : XA2_SLOT_FX;
  SDWORD iBaseVol;

  psSample->iSample = iSlot;

  iBaseVol = audio_GetSampleMixVol(psSample, AUDIO_VOL_MAX, FALSE);

  if (xa2_PlayOnFixedSlot(iSlot, psTrack, psSample, iBaseVol, sound_TrackLooped(psSample->iTrack)) == FALSE)
  {
    psSample->iSample = SAMPLE_NOT_ALLOCATED;
    return FALSE;
  }

  return TRUE;
}

/***************************************************************************/

BOOL sound_Play3DSample(TRACK* psTrack, AUDIO_SAMPLE* psSample)
{
  const SDWORD iSlot = xa2_AcquirePoolSlot();
  VOICE_SLOT* psSlot;
  SDWORD iBaseVol;

  if (iSlot < 0)
  {
    psSample->iSample = SAMPLE_NOT_ALLOCATED;
    return FALSE;
  }

  psSlot = &g_aSlots[iSlot];

  /* A new sound on the slot invalidates any handle held for the last one.
   * The wrap keeps the shifted handle positive, and skips zero so a pool
   * handle can never look like a fixed slot index.
   */
  psSlot->udwGeneration = psSlot->udwGeneration >= XA2_GENERATION_MAX ? 1 : psSlot->udwGeneration + 1;

  psSlot->bPositional = TRUE;
  psSlot->bMusic = FALSE;
  psSlot->iRadius = sound_TrackAudibleRadius(psSample->iTrack);
  psSlot->x = psSample->x;
  psSlot->y = psSample->y;
  psSlot->z = psSample->z;

  psSample->iSample = xa2_Handle(iSlot);

  iBaseVol = audio_GetSampleMixVol(psSample, AUDIO_VOL_MAX, TRUE);

  if (xa2_StartSlot(iSlot, psTrack, psSample, iBaseVol, sound_TrackLooped(psSample->iTrack)) == FALSE)
  {
    psSlot->bPositional = FALSE;
    psSample->iSample = SAMPLE_NOT_ALLOCATED;
    return FALSE;
  }

  return TRUE;
}

/***************************************************************************/

/* Tears a per-file voice down. The stream and music slots build theirs when
 * they are given a file, because they have to take its own channel count.
 */
static void xa2_StopStreamSlot(SDWORD iSlot, BOOL bNotify)
{
  VOICE_SLOT* psSlot = &g_aSlots[iSlot];

  xa2_ReleaseSlot(iSlot, bNotify);

  if (psSlot->pVoice != nullptr)
  {
    psSlot->pVoice->DestroyVoice();
    psSlot->pVoice = nullptr;
  }

  if (g_apStreamData[iSlot] != nullptr)
  {
    delete[] g_apStreamData[iSlot];
    g_apStreamData[iSlot] = nullptr;
  }
}

/***************************************************************************/
/*
 * Reads a WAV whole and plays it on one of the per-file slots.
 *
 * Whole rather than streamed: the material is small -- the largest file in the
 * game is 737 KB and all of sequenceAudio is 2.5 MB -- so a buffer thread
 * would be machinery for nothing. Worth revisiting if the music served from
 * disk turns out to be large.
 */
/***************************************************************************/

static BOOL xa2_StartStreamSlot(SDWORD iSlot, char szFileName[], SDWORD iVol, BOOL bMusic, BOOL bLoop)
{
  VOICE_SLOT* psSlot = &g_aSlots[iSlot];
  HMMIO hmmio;
  WAVEFORMATEX* psFormat = nullptr;
  UBYTE* pRaw = nullptr;
  UDWORD udwRawBytes = 0, udwBytes = 0, udwRate = KHZ22;
  WORD uChannels = 1;
  BOOL bCompressed = FALSE;
  WAVEFORMATEX sVoiceFormat;
  XAUDIO2_BUFFER sBuffer;
  BOOL bOK;

  if (g_pXAudio2 == nullptr)
    return FALSE;

  hmmio = mmioOpen(szFileName, nullptr, MMIO_READ | MMIO_ALLOCBUF);
  if (!hmmio)
  {
    Neuron::DebugTrace("xa2_StartStreamSlot: cannot open {}\n", szFileName);
    return FALSE;
  }

  bOK = xa2_ReadRiff(hmmio, &psFormat, &pRaw, &udwRawBytes, &bCompressed);
  mmioClose(hmmio, 0);

  /* Stereo is kept here. The sequence audio has a stereo track in it, and
   * unlike the pooled voices this one is built to fit what it is given.
   */
  if (bOK == TRUE)
  {
    udwRate = psFormat->nSamplesPerSec;
    bOK = xa2_Normalise(psFormat, pRaw, udwRawBytes, FALSE, &g_apStreamData[iSlot], &udwBytes, &uChannels);
  }

  delete[] (UBYTE*)psFormat;
  delete[] pRaw;

  if (bOK == FALSE || udwRate == 0)
  {
    delete[] g_apStreamData[iSlot];
    g_apStreamData[iSlot] = nullptr;
    Neuron::DebugTrace("xa2_StartStreamSlot: cannot decode {}\n", szFileName);
    return FALSE;
  }

  memset(&sVoiceFormat, 0, sizeof(sVoiceFormat));
  sVoiceFormat.wFormatTag = WAVE_FORMAT_PCM;
  sVoiceFormat.nChannels = uChannels;
  sVoiceFormat.nSamplesPerSec = udwRate;
  sVoiceFormat.wBitsPerSample = 16;
  sVoiceFormat.nBlockAlign = static_cast<WORD>(uChannels * sizeof(SWORD));
  sVoiceFormat.nAvgBytesPerSec = sVoiceFormat.nSamplesPerSec * sVoiceFormat.nBlockAlign;

  if (FAILED(g_pXAudio2->CreateSourceVoice(&psSlot->pVoice, &sVoiceFormat, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &g_aCallbacks[iSlot],
                                           nullptr, nullptr)))
  {
    Neuron::DebugTrace("xa2_StartStreamSlot: CreateSourceVoice failed for {}\n", szFileName);
    psSlot->pVoice = nullptr;
    delete[] g_apStreamData[iSlot];
    g_apStreamData[iSlot] = nullptr;
    return FALSE;
  }

  memset(&sBuffer, 0, sizeof(sBuffer));
  sBuffer.Flags = XAUDIO2_END_OF_STREAM;
  sBuffer.AudioBytes = udwBytes;
  sBuffer.pAudioData = g_apStreamData[iSlot];
  sBuffer.LoopCount = bLoop == TRUE ? XAUDIO2_LOOP_INFINITE : 0;

  psSlot->psTrack = nullptr;
  psSlot->bBusy = TRUE;
  psSlot->bPositional = FALSE;
  psSlot->bMusic = bMusic;
  psSlot->iBaseVol = iVol * sound_GetMaxVolume() / AUDIO_VOL_RANGE;

  xa2_ApplySlotOutput(iSlot);

  if (FAILED(psSlot->pVoice->SubmitSourceBuffer(&sBuffer, nullptr)) || FAILED(psSlot->pVoice->Start(0, XAUDIO2_COMMIT_NOW)))
  {
    Neuron::DebugTrace("xa2_StartStreamSlot: cannot start {}\n", szFileName);
    xa2_StopStreamSlot(iSlot, FALSE);
    return FALSE;
  }

  return TRUE;
}

/***************************************************************************/
/*
 * sound_PlayStream
 *
 * Unlike QMixer's, this honours the volume it is given: the old one took iVol
 * and then ignored it, scaling by whatever track zero's volume happened to be.
 */
/***************************************************************************/

BOOL sound_PlayStream(AUDIO_SAMPLE* psSample, char szFileName[], SDWORD iVol)
{
  /* one stream at a time, which is what every caller already assumes */
  xa2_StopStreamSlot(XA2_SLOT_STREAM, TRUE);

  if (xa2_StartStreamSlot(XA2_SLOT_STREAM, szFileName, iVol, FALSE, FALSE) == FALSE)
  {
    psSample->iSample = SAMPLE_NOT_ALLOCATED;
    return FALSE;
  }

  psSample->iSample = XA2_SLOT_STREAM;
  g_aSlots[XA2_SLOT_STREAM].psSample = psSample;

  return TRUE;
}

/***************************************************************************/
/*
 * Music. Its own slot, because the CD it replaces was a device of its own:
 * starting a briefing's audio must not stop the music, and the music staying
 * paused across a video is the point of cdAudio_Pause. No AUDIO_SAMPLE is
 * involved -- nothing above waits on music finishing.
 */
/***************************************************************************/

BOOL sound_PlayMusic(char szFileName[], SDWORD iVol, BOOL bLoop)
{
  xa2_StopStreamSlot(XA2_SLOT_MUSIC, FALSE);

  return xa2_StartStreamSlot(XA2_SLOT_MUSIC, szFileName, iVol, TRUE, bLoop);
}

/***************************************************************************/

void sound_StopMusic(void) { xa2_StopStreamSlot(XA2_SLOT_MUSIC, FALSE); }

/***************************************************************************/

/* Stop on a source voice holds its position, so this is a pause and not a
 * stop: the buffer stays queued and Start picks up where it left off.
 */
void sound_PauseMusic(void)
{
  if (g_aSlots[XA2_SLOT_MUSIC].pVoice != nullptr)
    g_aSlots[XA2_SLOT_MUSIC].pVoice->Stop(0, XAUDIO2_COMMIT_NOW);
}

/***************************************************************************/

void sound_ResumeMusic(void)
{
  if (g_aSlots[XA2_SLOT_MUSIC].pVoice != nullptr)
    g_aSlots[XA2_SLOT_MUSIC].pVoice->Start(0, XAUDIO2_COMMIT_NOW);
}

/***************************************************************************/

void sound_StopSample(SDWORD iSample)
{
  const SDWORD iSlot = xa2_SlotFromHandle(iSample);

  /* A handle that no longer names a live sound is not an error: the sound it
   * was issued for has already finished and reported itself.
   */
  if (iSlot < 0)
    return;

  if (iSlot == XA2_SLOT_STREAM || iSlot == XA2_SLOT_MUSIC)
    xa2_StopStreamSlot(iSlot, TRUE);
  else
    xa2_ReleaseSlot(iSlot, TRUE);
}

/***************************************************************************/

void sound_StopAll(void)
{
  SDWORD i;

  /* Called from audio_Shutdown, which frees the sample lists itself, so
   * nothing is notified here.
   */
  for (i = 0; i < XA2_MAX_SLOTS; i++)
  {
    if (i == XA2_SLOT_STREAM || i == XA2_SLOT_MUSIC)
      xa2_StopStreamSlot(i, FALSE);
    else
      xa2_ReleaseSlot(i, FALSE);
  }
}

/***************************************************************************/

BOOL sound_QueueSamplePlaying(void) { return g_aSlots[XA2_SLOT_QUEUE].bBusy; }

/***************************************************************************/
/* positioning */
/***************************************************************************/

void sound_SetPlayerPos(SDWORD iX, SDWORD iY, SDWORD iZ)
{
  g_iListenerX = iX;
  g_iListenerY = iY;
  g_iListenerZ = iZ;
}

/***************************************************************************/
/*
 * sound_SetPlayerOrientation
 *
 * Orientation given as an angle in degrees about the vertical axis. QMixer was
 * handed a direction vector of (-sin, cos, 0) built from it; what is kept here
 * is the perpendicular, pointing to the listener's right, because that is what
 * a sound's pan is taken from.
 */
/***************************************************************************/

void sound_SetPlayerOrientation(SDWORD iX, SDWORD iY, SDWORD iZ)
{
  const double dAngle = static_cast<double>(iZ) * PI / 180.0;

  iX;
  iY;

  g_fRightX = static_cast<float>(cos(dAngle));
  g_fRightY = static_cast<float>(sin(dAngle));
}

/***************************************************************************/

void sound_SetObjectPosition(SDWORD iSample, SDWORD iX, SDWORD iY, SDWORD iZ)
{
  const SDWORD iSlot = xa2_SlotFromHandle(iSample);

  if (iSlot < 0)
    return;

  g_aSlots[iSlot].x = iX;
  g_aSlots[iSlot].y = iY;
  g_aSlots[iSlot].z = iZ;
}

/***************************************************************************/
/* frame */
/***************************************************************************/

void sound_Update(void)
{
  UDWORD udwFinished;
  SDWORD i;

  if (g_pXAudio2 == nullptr)
    return;

  /* Take the completions XAudio2's thread has raised since the last frame.
   * Everything below this line runs on the game thread.
   */
  EnterCriticalSection(&g_csFinished);
  udwFinished = g_udwFinishedMask;
  g_udwFinishedMask = 0;
  LeaveCriticalSection(&g_csFinished);

  for (i = 0; i < XA2_MAX_SLOTS; i++)
  {
    VOICE_SLOT* psSlot = &g_aSlots[i];
    AUDIO_SAMPLE* psSample;

    if ((udwFinished & (1u << i)) == 0)
      continue;

    psSample = psSlot->psSample;

    psSlot->psTrack = nullptr;
    psSlot->psSample = nullptr;
    psSlot->bPositional = FALSE;
    psSlot->bBusy = psSlot->iPending > 0 ? TRUE : FALSE;

    if (psSample != nullptr && sound_GetSystemActive() == TRUE)
      sound_FinishedCallback(psSample);

    if (i == XA2_SLOT_STREAM || i == XA2_SLOT_MUSIC)
      xa2_StopStreamSlot(i, FALSE);
  }

  /* Start anything queued on the fixed slots. This runs every frame rather
   * than only after a completion, because a slot also falls idle when an
   * immediate start was refused by a voice that had not finished draining.
   */
  xa2_PumpFixedSlot(XA2_SLOT_QUEUE);
  xa2_PumpFixedSlot(XA2_SLOT_FX);

  /* Refresh attenuation and pan for everything positional. QMixer applied its
   * distance mapping continuously as positions were fed in; this is where
   * that happens now.
   */
  for (i = XA2_FIXED_SLOTS; i < XA2_MAX_SLOTS; i++)
  {
    if (g_aSlots[i].bBusy == TRUE && g_aSlots[i].bPositional == TRUE)
      xa2_ApplySlotOutput(i);
  }
}

/***************************************************************************/
