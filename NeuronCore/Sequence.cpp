#include "pch.h"

// Standard include file
#include "Frame.h"
#include "Sequence.h"

// Sound include files
#define SEQUENCE_SOUND
#ifdef SEQUENCE_SOUND
#include <dsound.h>
#endif

// ESCAPE include file
#include "STREAMER.H"

/***************************************************************************/
/*
 *	Local Definitions
 */
/***************************************************************************/

using A_STRUCT = struct _aStruct
{
  BOOL bBool;
};

#define TEXT_BOXES
#define VIDEO_WIDTH 640
#define VIDEO_HEIGHT 480

/* The pixel layout frames are decoded into: RGB565, no alpha. The display is
 * 32 bit, and the blit into it converts, so the decoder's format no longer
 * has to follow whatever the card handed back.
 */
#define SEQ_ALPHA_POS   0
#define SEQ_ALPHA_BITS  0
#define SEQ_RED_POS     11
#define SEQ_RED_BITS    5
#define SEQ_GREEN_POS   5
#define SEQ_GREEN_BITS  6
#define SEQ_BLUE_POS    0
#define SEQ_BLUE_BITS   5

/* Every colour's low bit, used to halve a pixel's brightness without letting
 * one channel bleed into the next. Was derived from the surface's masks.
 */
#define SEQ_LOW_BIT_MASK ((UDWORD)(0xffff - (1 << SEQ_BLUE_POS) - (1 << SEQ_GREEN_POS) - (1 << SEQ_RED_POS)))

/***************************************************************************/
/*
 *	local Variables
 */
/***************************************************************************/

/* 
	The following variables/structures have scope over the entire module as the 
	sound callback function needs access to them,  as well as the the main() 
	function. 
*/

int LastUpdated;
LPMOVIEHANDLE mhandle = nullptr;
LPVIDEOHANDLE vhandle = nullptr;
LPSOUNDHANDLE shandle = nullptr;
LPBYTE localBuffer;
int movieWidth, movieHeight;

LPDIRECTSOUNDBUFFER lpDSSB = nullptr;
LPBYTE soundbuffer1 = nullptr;
LPBYTE soundbuffer2 = nullptr;

/* FMV audio used to borrow the IDirectSound that QMixer had created for
 * itself, handed over by audio_GetDirectSoundObj. The game's mixer is XAudio2
 * now and has no DirectSound object to lend, so this module keeps its own.
 * It is created on first use and released in seq_ShutDown. The codec writes
 * PCM straight into a DirectSound buffer, so replacing that with an XAudio2
 * voice means changing the decoder's callback -- work that belongs with the
 * replacement of WINSTR.LIB, not here.
 */
static LPDIRECTSOUND lpSeqDS = nullptr;
ULONG temp;
BOOL bPlayerOn;
BOOL bSmallVideo = FALSE;
BOOL bTextBoxes = FALSE;
/***************************************************************************/
/*
 *	Local ProtoTypes
 */
/***************************************************************************/

void SoundCallBackFunc(LPSOUNDHANDLE shandle);

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/

/* Returns the module's DirectSound object, creating it the first time. A null
 * return is not fatal: both callers treat it the way they treated a movie
 * with no sound track, and play the video silently.
 */
static LPDIRECTSOUND seq_GetDirectSound(void)
{
  if (lpSeqDS != nullptr)
    return lpSeqDS;

  if (DirectSoundCreate(nullptr, &lpSeqDS, nullptr) != DS_OK)
  {
    Neuron::DebugTrace("seq_GetDirectSound: DirectSoundCreate failed\n");
    lpSeqDS = nullptr;
    return nullptr;
  }

  if (lpSeqDS->SetCooperativeLevel(frameGetWinHandle(), DSSCL_PRIORITY) != DS_OK)
  {
    Neuron::DebugTrace("seq_GetDirectSound: SetCooperativeLevel failed\n");
    lpSeqDS->Release();
    lpSeqDS = nullptr;
    return nullptr;
  }

  return lpSeqDS;
}

//buffer render for software_window 3DFX_window and 3DFX_fullscreen modes
// D3D_WINDOWED video size 16bit uses screen pixel mode
// 3DFX_WINDOWED video size 16bit BGR 565 mode
// 3DFX_FULLSCREEN 640 * 480 BGR 565 mode
BOOL seq_SetSequenceForBuffer(char* filename, VIDEO_MODE mode, int startTime, PERF_MODE perfMode)
{
  long width, height;
  long precision, channels;
  long compression;
  BOOL bCompression;
  DSBUFFERDESC DS_bd;
  WAVEFORMATEX pcmwf;
  LPDIRECTSOUND lpDS = nullptr;

  mhandle = nullptr;
  vhandle = nullptr;
  shandle = nullptr;
  lpDSSB = nullptr;

  if (perfMode == VIDEO_PERF_FULLSCREEN)
    bSmallVideo = FALSE;
  else
    bSmallVideo = TRUE;

  /*
	// Initialise the movie with a 2MB buffersize
	*/
  if (Streamer_InitMovie(&mhandle, nullptr, 0, filename, 2 << 20, SIM_NONE) != STREAMER_OK)
    return FALSE;

  movieWidth = Movie_GetXSize(mhandle);
  movieHeight = Movie_GetYSize(mhandle);

  /*
  // Initialise the video playback environment
  */

  if (Streamer_InitVideo(&vhandle, mhandle, movieWidth, movieHeight, 0, 0, 0, 0, 0, 0, DFLAG_INVIEWPORT,
                         // | DFLAG_DOUBLED, | DFLAG_CLEAROUTPUT | //DFLAG_DOUBLED | 
                         &width, &height) != STREAMER_OK)
    return FALSE;
  Streamer_SetVideoPitch(vhandle, movieWidth, movieHeight);

#ifdef SEQUENCE_SOUND
  // check for the presence of audio within the rpl file
  if (Movie_GetSoundChannels(mhandle) && Movie_GetSoundPrecision(mhandle) && Movie_GetSoundRate(mhandle) && ((lpDS = seq_GetDirectSound()) != nullptr))
  {
    //if so initialise Direct sound playback
    precision = Movie_GetSoundPrecision(mhandle);
    channels = Movie_GetSoundChannels(mhandle);
    if (precision == 4)
    {
      compression = 4;
      bCompression = FALSE;
    }
    else
    {
      compression = 1;
      bCompression = TRUE;
    }

    if (Streamer_InitSound(SoundCallBackFunc, &shandle, 16384, compression, bCompression, 4096, channels) != STREAMER_OK)
      return FALSE;
    // Attempt to create an instance of direct sound - 
    // for information on DirectSound refer to Microsoft documentation

    memset(&pcmwf, 0, sizeof(pcmwf));

    pcmwf.wFormatTag = WAVE_FORMAT_PCM;
    pcmwf.nChannels = static_cast<WORD>(Movie_GetSoundChannels(mhandle));
    pcmwf.nSamplesPerSec = Movie_GetSoundRate(mhandle);
    pcmwf.wBitsPerSample = 16; // there are always 16 bits after ADPCM is decompressed or if its raw PCM
    pcmwf.nBlockAlign = (pcmwf.nChannels * pcmwf.wBitsPerSample) / 8;
    pcmwf.nAvgBytesPerSec = pcmwf.nSamplesPerSec * pcmwf.nBlockAlign;
    pcmwf.cbSize = 0;

    memset(&DS_bd, 0, sizeof(DS_bd));
    DS_bd.dwSize = sizeof (DS_bd);
    DS_bd.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME; //DSBCAPS_CTRLDEFAULT;
    DS_bd.dwBufferBytes = 32768;
    DS_bd.lpwfxFormat = &pcmwf;

    if (lpDS->CreateSoundBuffer(&DS_bd, &lpDSSB, nullptr) != DS_OK)
      return FALSE;

    if (lpDSSB->Lock(0, 16384, (void**)&soundbuffer1, &temp, nullptr, nullptr, 0) != DS_OK)
      return FALSE;

    soundbuffer2 = soundbuffer1 + 16384;

    Streamer_SetSoundBuffer(shandle, 0, soundbuffer1);
    Streamer_SetSoundBuffer(shandle, 1, soundbuffer2);
  }
#endif //SEQUENCE_SOUND

  /*
  // This does the preload of movie data from the file and fills the audio double buffers (which
  //  is why they were locked)
  */
  if (Streamer_InitStreaming(mhandle, vhandle, shandle) != STREAMER_OK)
    return FALSE;

#ifdef SEQUENCE_SOUND
  if (shandle)
  {
    lpDSSB->Unlock(soundbuffer1, 16384, nullptr, 0);

    Streamer_SetSoundDecodeMode(shandle, SSDM_IDLE);
  }
  LastUpdated = SSDM_SECONDBUFFER;
#endif //SEQUENCE_SOUND

  /* Frames are decoded as RGB565 and converted when they are written to the
   * 32 bit display. The Direct3D 6 code asked the destination surface what
   * 16 bit layout the card had given it and told the codec that, which is
   * what the two pages of bit mask scanning here used to do.
   */
  if (Streamer_SetPixelFormat(vhandle, SPF_BPP16, SEQ_ALPHA_POS, SEQ_ALPHA_BITS, SEQ_RED_POS, SEQ_RED_BITS, SEQ_GREEN_POS, SEQ_GREEN_BITS,
                              SEQ_BLUE_POS, SEQ_BLUE_BITS) != STREAMER_OK)
    return FALSE;

#ifdef SEQUENCE_SOUND
  if (shandle)
  {
    // Begin sound playback
    if (lpDSSB->Play(0, 0, DSBPLAY_LOOPING) != DS_OK)
      return VIDEO_SOUND_ERROR;
  }
#endif //SEQUENCE_SOUND

  return TRUE;
}

/*
 * directX fullscreeen render uses local buffer to store previous frame data
 * directX 640 * 480 16bit rgb mode render through local buffer to back buffer
 */
BOOL seq_SetSequence(char* filename, int startTime, char* lpBF, PERF_MODE perfMode)
{
  long width, height;
  long precision, channels;
  long compression;
  BOOL bCompression;

  DSBUFFERDESC DS_bd;
  WAVEFORMATEX pcmwf;
  LPDIRECTSOUND lpDS = nullptr;

  mhandle = nullptr;
  vhandle = nullptr;
  shandle = nullptr;
  lpDSSB == nullptr;

  if (perfMode == VIDEO_PERF_FULLSCREEN)
    bSmallVideo = FALSE;
  else
    bSmallVideo = TRUE;

  /*
  // Initialise the movie with a 2MB buffersize
  */
  if (Streamer_InitMovie(&mhandle, nullptr, 0, filename, 2 << 20, SIM_NONE) != STREAMER_OK)
    return FALSE;

  movieWidth = Movie_GetXSize(mhandle);
  movieHeight = Movie_GetYSize(mhandle);

  //malloc a buffer for video playback
  localBuffer = (LPBYTE)lpBF; //always 16bit
  memset(localBuffer, 0, VIDEO_WIDTH * VIDEO_HEIGHT * sizeof(WORD));
  /*
  // Initialise the video playback environment
  */
  if (((movieWidth <= (VIDEO_WIDTH / 2)) && (movieHeight <= (VIDEO_HEIGHT / 2))) && !bSmallVideo) //render doubled
  {
    if (Streamer_InitVideo(&vhandle, mhandle, movieWidth, movieHeight, 0, 0, 0, 0, 0, 0, DFLAG_INVIEWPORT | DFLAG_DOUBLED,
                           // | DFLAG_CLEAROUTPUT | //DFLAG_DOUBLED | 
                           &width, &height) != STREAMER_OK)
      return FALSE;
    movieWidth *= 2;
    movieHeight *= 2;
  }
  else
  {
    if (Streamer_InitVideo(&vhandle, mhandle, movieWidth, movieHeight, 0, 0, 0, 0, 0, 0, DFLAG_INVIEWPORT,
                           // | DFLAG_CLEAROUTPUT | //DFLAG_DOUBLED | 
                           &width, &height) != STREAMER_OK)
      return FALSE;
  }
  if (Streamer_SetVideoPitch(vhandle, movieWidth, movieHeight) != STREAMER_OK)
    return FALSE;

#ifdef SEQUENCE_SOUND
  // check for the presence of audio
  if (Movie_GetSoundChannels(mhandle) && Movie_GetSoundPrecision(mhandle) && Movie_GetSoundRate(mhandle) && ((lpDS = seq_GetDirectSound()) != nullptr))
  {
    //if so initialise Direct sound playback
    precision = Movie_GetSoundPrecision(mhandle);
    channels = Movie_GetSoundChannels(mhandle);
    if (precision == 4)
    {
      compression = 4;
      bCompression = FALSE;
    }
    else
    {
      compression = 1;
      bCompression = TRUE;
    }

    if (Streamer_InitSound(SoundCallBackFunc, &shandle, 16384, compression, bCompression, 4096, channels) != STREAMER_OK)
      return FALSE;
    // Attempt to create an instance of direct sound - 
    // for information on DirectSound refer to Microsoft documentation

    memset(&pcmwf, 0, sizeof(pcmwf));

    pcmwf.wFormatTag = WAVE_FORMAT_PCM;
    pcmwf.nChannels = static_cast<WORD>(Movie_GetSoundChannels(mhandle));
    pcmwf.nSamplesPerSec = Movie_GetSoundRate(mhandle);
    pcmwf.wBitsPerSample = 16; // there are always 16 bits after ADPCM is decompressed or if its raw PCM
    pcmwf.nBlockAlign = (pcmwf.nChannels * pcmwf.wBitsPerSample) / 8;
    pcmwf.nAvgBytesPerSec = pcmwf.nSamplesPerSec * pcmwf.nBlockAlign;
    pcmwf.cbSize = 0;

    memset(&DS_bd, 0, sizeof(DS_bd));
    DS_bd.dwSize = sizeof (DS_bd);
    DS_bd.dwFlags = DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME; //DSBCAPS_CTRLDEFAULT;
    DS_bd.dwBufferBytes = 32768;
    DS_bd.lpwfxFormat = &pcmwf;

    bPlayerOn = FALSE;

    if (lpDS->CreateSoundBuffer(&DS_bd, &lpDSSB, nullptr) != DS_OK)
      return FALSE;

    if (lpDSSB->Lock(0, 32768, (void**)&soundbuffer1, &temp, nullptr, nullptr, 0) != DS_OK)
      return FALSE;

    soundbuffer2 = soundbuffer1 + 16384;

    Streamer_SetSoundBuffer(shandle, 0, soundbuffer1);
    Streamer_SetSoundBuffer(shandle, 1, soundbuffer2);
  }
#endif //SEQUENCE_SOUND

  /*
  // This does the preload of movie data from the file and fills the audio double buffers (which
  //  is why they were locked)
  */
  if (Streamer_InitStreaming(mhandle, vhandle, shandle) != STREAMER_OK)
    return FALSE;

#ifdef SEQUENCE_SOUND
  if (shandle)
  {
    lpDSSB->Unlock(soundbuffer1, 16384, nullptr, 0);

    Streamer_SetSoundDecodeMode(shandle, SSDM_IDLE);
  }
  LastUpdated = SSDM_SECONDBUFFER;
#endif //SEQUENCE_SOUND

  /* Frames are decoded as RGB565 and converted when they are written to the
   * 32 bit display. The Direct3D 6 code asked the destination surface what
   * 16 bit layout the card had given it and told the codec that, which is
   * what the two pages of bit mask scanning here used to do.
   */
  if (Streamer_SetPixelFormat(vhandle, SPF_BPP16, SEQ_ALPHA_POS, SEQ_ALPHA_BITS, SEQ_RED_POS, SEQ_RED_BITS, SEQ_GREEN_POS, SEQ_GREEN_BITS,
                              SEQ_BLUE_POS, SEQ_BLUE_BITS) != STREAMER_OK)
    return FALSE;

#ifdef SEQUENCE_SOUND
  if (shandle)
  {
    // Begin sound playback
    if (lpDSSB->Play(0, 0, DSBPLAY_LOOPING) != DS_OK)
      return VIDEO_SOUND_ERROR;
  }
#endif //SEQUENCE_SOUND

  return TRUE;
}

int seq_ClearMovie(void)
{
  /* Shutdown Sound, Video and Movie*/
  Streamer_ShutDownSound(&shandle);
  Streamer_ShutDownVideo(&vhandle);
  Streamer_ShutDownMovie(&mhandle);
  shandle == nullptr;
  vhandle == nullptr;
  mhandle == nullptr;
  return TRUE;
}

/*
 * buffer render for software_window 3DFX_window and 3DFX_fullscreen modes
 * SOFT_WINDOWED video size 16bit rgb 555 mode (convert to 8bit from the buffer)
 * 3DFX_WINDOWED video size 16bit BGR 565 mode
 * 3DFX_FULLSCREEN 640 * 480 BGR 565 mode
 */
int seq_RenderOneFrameToBuffer(char* lpSF, int skip, SDWORD subMin, SDWORD subMax)
{
  STRESULT sRes = STREAMER_OK;
  int frame;
  DWORD CurrentPos, CurrentWritePos;
  WORD* blitSrc;
  SDWORD i, j;
  SDWORD subYstart, subYend, subXstart, subXend;
  WORD pixel;

  if ((mhandle == nullptr) || (vhandle == nullptr))
    return VIDEO_FRAME_ERROR;

  bTextBoxes = FALSE;
#ifdef TEXT_BOXES
  if ((!bSmallVideo) && (subMin <= subMax))
    bTextBoxes = TRUE;
#endif

  /*
  // Calculate how far through the movie we should be based on the time elasped since playback started
  // If we are not going to decode audio and video then call Streamer_Stream() with a frames to play setting of 0
  // this will replenish the cyclic buffer if it is needed.
  */
  /*
  // Stream one frame of video to the surface
  */
  sRes = Streamer_Stream(mhandle, vhandle, shandle, nullptr, skip, (LPBYTE)lpSF, nullptr, nullptr, 0);

  frame = Movie_GetCurrentFrame(mhandle);

#ifdef TEXT_BOXES
  //fade region for sequences
  if (bTextBoxes)
  {
    subYstart = subMin - 10;
    subYend = subMax + 6;
    subXstart = 14;
    subXend = 624;

    if (subYstart < 0)
      subYstart = 0;
    if (subYend > VIDEO_HEIGHT)
      subYend = VIDEO_HEIGHT;

    blitSrc = (WORD*)lpSF + movieWidth * subYstart;
    for (j = subYstart; j < subYend; j++)
    {
      for (i = 0; i < movieWidth; i++)
      {
        pixel = blitSrc[i];
        if ((i > subXstart) && (i < subXend))
        {
          pixel &= SEQ_LOW_BIT_MASK;
          pixel >>= 1;
          pixel += 0x2;
        }
        blitSrc[i] = pixel;
      }
      blitSrc += movieWidth;
    }
  }
#endif

#ifdef SEQUENCE_SOUND
  if (shandle)
  {
    if (Streamer_GetSoundDecodeMode(shandle) == SSDM_IDLE)
    {
      if (lpDSSB->GetCurrentPosition(&CurrentPos, &CurrentWritePos) != DS_OK)
        return VIDEO_SOUND_ERROR;

      if ((LastUpdated == SSDM_SECONDBUFFER) && (CurrentPos > 16384))
      {
        Streamer_SetSoundDecodeMode(shandle, SSDM_FIRSTBUFFER);

        lpDSSB->Lock(0, 16384, (void**)&soundbuffer1, &temp, nullptr, nullptr, 0);
      }
      else if ((LastUpdated == SSDM_FIRSTBUFFER) && (CurrentPos < 16384))
      {
        Streamer_SetSoundDecodeMode(shandle, SSDM_SECONDBUFFER);

        lpDSSB->Lock(16384, 16384, (void**)&soundbuffer2, &temp, nullptr, nullptr, 0);
      }
    }
  }
#endif //SEQUENCE_SOUND

  if (sRes == STREAMER_OK)
    return frame;
  return VIDEO_FINISHED;
}

/*
 * Decode one frame into the local buffer and blit it into the back buffer.
 *
 * The frame arrives as RGB565 and the back buffer is X8R8G8B8, so unlike the
 * DirectDraw version this converts as it copies rather than moving words.
 */
int seq_RenderOneFrame(int skip, SDWORD subMin, SDWORD subMax)
{
  SCREEN_LOCK sLock;
  STRESULT sRes = STREAMER_OK;
  WORD* blitSrc;
  UDWORD* blitDest;
  int i, j;
  WORD pixel;
  int frame;
  DWORD CurrentPos, CurrentWritePos;
  SDWORD borderX, borderY;
  SDWORD subYstart, subYend, subXstart, subXend;
  SDWORD row;

  if ((mhandle == nullptr) || (vhandle == nullptr))
    return VIDEO_FRAME_ERROR;

  if (bPlayerOn == FALSE)
    bPlayerOn = TRUE;

  bTextBoxes = FALSE;
#ifdef TEXT_BOXES
  if ((!bSmallVideo) && (subMin <= subMax))
    bTextBoxes = TRUE;
#endif

  // Calculate how far through the movie we should be based on the time elasped since playback started
  // If we are not going to decode audio and video then call Streamer_Stream() with a frames to play setting of 0
  // this will replenish the cyclic buffer if it is needed.

  sRes = Streamer_Stream(mhandle, vhandle, shandle, nullptr, skip, localBuffer, nullptr, nullptr, 0);

  frame = Movie_GetCurrentFrame(mhandle);
  if (sRes == STREAMER_FINISHEDAUDIO)
  {
    Neuron::DebugTrace("STREAMER_FINISHEDAUDIO {}\n",sRes);
    shandle = nullptr;
    sRes = STREAMER_OK;
  }
  else if (sRes != STREAMER_OK)
    Neuron::DebugTrace("STREAMER_STREAM ERROR {} {}\n",sRes,frame);

  // We lock the back buffer before blitting video to it.
  if (!screenLockBackBuffer(&sLock))
  {
    Neuron::Fatal("Sequence player back buffer lock failed");
    return VIDEO_SURFACE_ERROR;
  }

  if (!bSmallVideo)
  {
    borderX = (static_cast<SDWORD>(sLock.width) - VIDEO_WIDTH) / 2;
    borderY = (static_cast<SDWORD>(sLock.height) - VIDEO_HEIGHT) / 4;
  }
  else
  {
    borderX = (static_cast<SDWORD>(sLock.width) - VIDEO_WIDTH / 2) / 2;
    borderY = (static_cast<SDWORD>(sLock.height) - VIDEO_HEIGHT / 2) / 4;
  }

  if (borderX < 0)
    borderX = 0;
  if (borderY < 0)
    borderY = 0;

  blitSrc = (WORD*)localBuffer;
  row = borderY;

  if (bTextBoxes)
  {
    subYstart = subMin - 10; //only ever in fullscreen mode
    subYend = subMax + 6;
    subXstart = 14;
    subXend = 624;

    if (subYstart < 0)
      subYstart = 0;
    if (subYend > VIDEO_HEIGHT)
      subYend = VIDEO_HEIGHT;
  }
  else
  {
    subYstart = movieHeight;
    subYend = movieHeight;
    subXstart = movieWidth;
    subXend = movieWidth;
  }

  /* The row stepping here is the one behaviour change. The DirectDraw
   * version started from lpSurface + borderY * lPitch counted in words, so
   * it stepped twice as far down the surface as it meant to; that only
   * showed up above 480 lines, where borderY stops being zero. */
#define SEQ_ROW(y) ((UDWORD*)(sLock.pPixels + sLock.pitch * (y)) + borderX)

  //blit videoBuffer to the back buffer
  for (j = 0; j < subYstart; j++, row++)
  {
    if (row < 0 || row >= static_cast<SDWORD>(sLock.height))
    {
      blitSrc += movieWidth;
      continue;
    }
    blitDest = SEQ_ROW(row);
    for (i = 0; i < movieWidth; i++)
    {
      if (borderX + i < static_cast<SDWORD>(sLock.width))
        blitDest[i] = screen565To32(blitSrc[i]);
    }
    blitSrc += movieWidth;
  }
  if (bTextBoxes)
  {
    for (j = subYstart; j < subYend; j++, row++)
    {
      if (row < 0 || row >= static_cast<SDWORD>(sLock.height))
      {
        blitSrc += movieWidth;
        continue;
      }
      blitDest = SEQ_ROW(row);
      for (i = 0; i < movieWidth; i++)
      {
        pixel = blitSrc[i];
        if ((i > subXstart) && (i < subXend))
        {
          pixel &= static_cast<WORD>(SEQ_LOW_BIT_MASK);
          pixel >>= 1;
          pixel += 0x2;
        }
        if (borderX + i < static_cast<SDWORD>(sLock.width))
          blitDest[i] = screen565To32(pixel);
      }
      blitSrc += movieWidth;
    }
    for (j = subYend; j < movieHeight; j++, row++)
    {
      if (row < 0 || row >= static_cast<SDWORD>(sLock.height))
      {
        blitSrc += movieWidth;
        continue;
      }
      blitDest = SEQ_ROW(row);
      for (i = 0; i < movieWidth; i++)
      {
        if (borderX + i < static_cast<SDWORD>(sLock.width))
          blitDest[i] = screen565To32(blitSrc[i]);
      }
      blitSrc += movieWidth;
    }
  }

#undef SEQ_ROW

  // We can unlock the surface now as we have finished with it,
  // until the next decode is required
  screenUnlockBackBuffer();

#ifdef SEQUENCE_SOUND
  if (shandle)
  {
    if (Streamer_GetSoundDecodeMode(shandle) == SSDM_IDLE)
    {
      if (lpDSSB->GetCurrentPosition(&CurrentPos, &CurrentWritePos) != DS_OK)
        return VIDEO_SOUND_ERROR;

      if ((LastUpdated == SSDM_SECONDBUFFER) && (CurrentPos > 16384))
      {
        Streamer_SetSoundDecodeMode(shandle, SSDM_FIRSTBUFFER);

        lpDSSB->Lock(0, 16384, (void**)&soundbuffer1, &temp, nullptr, nullptr, 0);
      }
      else if ((LastUpdated == SSDM_FIRSTBUFFER) && (CurrentPos < 16384))
      {
        Streamer_SetSoundDecodeMode(shandle, SSDM_SECONDBUFFER);

        lpDSSB->Lock(16384, 16384, (void**)&soundbuffer2, &temp, nullptr, nullptr, 0);
      }
    }
  }
#endif //SEQUENCE_SOUND

  if (sRes == STREAMER_OK)
    return frame;
  return VIDEO_FINISHED;
}

BOOL seq_RefreshVideoBuffers(void)
{
  STRESULT sRes;
  int badDataCount;
  DWORD CurrentPos, CurrentWritePos;

  if ((mhandle == nullptr) || (vhandle == nullptr))
    return FALSE;

  // Here we attempt to replenish the cyclic buffer but asking 
  //   streamer_stream() to stream zero frames,  this should be 
  //   done when it is not yet time to decode more video 
  /* This ensures that there is sufficient data in the buffer
     to satisfy the streamer */
  /* keep trying until the bad data rate problem disappears,
     through sufficient data in buffer */
  badDataCount = 0;
  do
  {
    sRes = Streamer_Stream(mhandle, vhandle, shandle, nullptr, 0, nullptr, nullptr, nullptr, 0);
    badDataCount++;
  }
  while ((sRes == STREAMER_BADDATARATE) && (badDataCount < MAX_BAD_DATA));

#ifdef SEQUENCE_SOUND
  if (shandle)
  {
    if (Streamer_GetSoundDecodeMode(shandle) == SSDM_IDLE)
    {
      if (lpDSSB->GetCurrentPosition(&CurrentPos, &CurrentWritePos) != DS_OK)
        return VIDEO_SOUND_ERROR;

      if ((LastUpdated == SSDM_SECONDBUFFER) && (CurrentPos > 16384))
      {
        Streamer_SetSoundDecodeMode(shandle, SSDM_FIRSTBUFFER);

        lpDSSB->Lock(0, 16384, (void**)&soundbuffer1, &temp, nullptr, nullptr, 0);
      }
      else if ((LastUpdated == SSDM_FIRSTBUFFER) && (CurrentPos < 16384))
      {
        Streamer_SetSoundDecodeMode(shandle, SSDM_SECONDBUFFER);

        lpDSSB->Lock(16384, 16384, (void**)&soundbuffer2, &temp, nullptr, nullptr, 0);
      }
    }
  }
#endif //SEQUENCE_SOUND

  if (sRes == STREAMER_OK)
    return TRUE;
  return FALSE;
}

BOOL seq_ShutDown(void)
{
  if (lpDSSB)
  {
    lpDSSB->Stop();
    lpDSSB->Release();
  }
  /* Shutdown Sound, Video and Movie*/
  Streamer_ShutDownSound(&shandle);
  Streamer_ShutDownVideo(&vhandle);
  Streamer_ShutDownMovie(&mhandle);
  lpDSSB = nullptr;
  shandle == nullptr;
  vhandle == nullptr;
  mhandle == nullptr;

  if (lpSeqDS)
  {
    lpSeqDS->Release();
    lpSeqDS = nullptr;
  }

  return TRUE;
}

void SoundCallBackFunc(LPSOUNDHANDLE shandle)
{
  long state;

  state = Streamer_GetSoundDecodeMode(shandle);

  if (state == SSDM_FIRSTBUFFER) { lpDSSB->Unlock(soundbuffer1, 16384, nullptr, 0); }
  else if (state == SSDM_SECONDBUFFER) { lpDSSB->Unlock(soundbuffer2, 16384, nullptr, 0); }

  Streamer_SetSoundDecodeMode(shandle, SSDM_IDLE);
  LastUpdated = state;
}

BOOL seq_GetFrameSize(SDWORD* pWidth, SDWORD* pHeight)
{
  if (mhandle)
  {
    *pWidth = movieWidth;
    *pHeight = movieHeight;
    return TRUE;
  }
  *pWidth = 0;
  *pHeight = 0;
  return FALSE;
}

int seq_GetCurrentFrame(void)
{
  if (mhandle)
    return Movie_GetCurrentFrame(mhandle);
  return -1;
}

int seq_GetFrameTimeInClicks(void)
{
  float frameTime;
  if (mhandle)
  {
    frameTime = 1000.0f / Movie_GetFrameRate(mhandle);
    return static_cast<int>(frameTime);
  }
  return 40;
}

int seq_GetTotalFrames(void)
{
  if (mhandle)
    return Movie_GetTotalFrames(mhandle);
  return -1;
}
