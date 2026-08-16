#include "pch.h"

// Standard include file
#include "Frame.h"
#include "Sequence.h"

#include "MovieStream.h"
#include "Screen.h"

#include <string>

/***************************************************************************/
/*
 * The FMV player, on Media Foundation.
 *
 * This was 840 lines driving WINSTR.LIB, the 1997 Eidos streaming library,
 * with its soundtrack hand-fed into a DirectSound ring buffer through a
 * double-buffer callback. Phase 6 re-encoded the `.rpl` assets to H.264/AAC in
 * MP4 and replaced the decoder; what is left here is the same eleven functions
 * Sequence.h has always declared, implemented over one Neuron::MovieStream.
 *
 * Sequence.h did not change, so SeqDisp.cpp and IntelMap.cpp still drive the
 * timing themselves and still see frame numbers at 25 fps.
 *
 * Two things went with the old decoder. The DirectSound object this module
 * created for itself is gone -- the movie soundtrack is an XAudio2 voice on the
 * game's own graph now, so it obeys the volume sliders. And the double buffer,
 * its callback and the SSDM_* state machine are gone with it: the track is
 * decoded whole at open and submitted once.
 */
/***************************************************************************/

#define VIDEO_WIDTH 640
#define VIDEO_HEIGHT 480

/***************************************************************************/
/*
 *	local Variables
 */
/***************************************************************************/

/* Deliberately heap allocated, and deliberately never deleted.
 *
 * A file-scope MovieStream is destroyed during CRT static teardown -- after
 * systemShutdown has released the XAudio2 engine and after COM has gone.
 * Destroying a source voice whose engine no longer exists corrupts the heap,
 * and that is exactly what "Invalid address specified to RtlValidateHeap" on
 * exit was: the movie's voice outliving the mixer that owned it.
 *
 * Leaking one object at process exit costs nothing; freeing it at the wrong
 * moment costs a corrupted heap. seq_ShutDown releases everything inside it
 * while the subsystems are still alive, and systemShutdown calls that before
 * AudioSystem::Shutdown.
 */
static Neuron::MovieStream* g_movie = nullptr;

static Neuron::MovieStream& seq_Movie()
{
  if (g_movie == nullptr)
    g_movie = new Neuron::MovieStream();
  return *g_movie;
}

/* Null-safe, so the query functions do not build a player just to be told
 * there is no movie playing.
 */
static BOOL seq_MovieOpen(void) { return (g_movie != nullptr && g_movie->IsOpen()) ? TRUE : FALSE; }

/* Set when the caller asked for the small in-HCI window rather than a
 * full-screen sequence, exactly as the old bSmallVideo did.
 */
static BOOL g_smallVideo = FALSE;

/***************************************************************************/
/*
 *	Source
 */
/***************************************************************************/

/* The name arrives ready to open: SeqDisp.cpp assembles the path and the game
 * data names the movies directly, so there is nothing to translate here.
 */
static BOOL seq_OpenMovie(char* filename, PERF_MODE perfMode)
{
  g_smallVideo = (perfMode != VIDEO_PERF_FULLSCREEN);
  return seq_Movie().Open(filename) ? TRUE : FALSE;
}

/* The unused parameters are Sequence.h's, and Sequence.h is deliberately
 * unchanged: the old decoder needed a start time to seed its own clock and a
 * video mode to pick a pixel layout, and neither survives. They are left
 * unnamed rather than removed so SeqDisp.cpp and IntelMap.cpp keep compiling
 * against the interface they already call.
 */

//buffer render for the small in-HCI video window
BOOL seq_SetSequenceForBuffer(char* filename, VIDEO_MODE, int, PERF_MODE perfMode) { return seq_OpenMovie(filename, perfMode); }

//decode into a local buffer, then blit it into the back buffer
BOOL seq_SetSequence(char* filename, int, char*, PERF_MODE perfMode) { return seq_OpenMovie(filename, perfMode); }

int seq_ClearMovie(void)
{
  if (g_movie != nullptr)
    g_movie->Close();
  return TRUE;
}

/*
 * Decode one frame into a caller-supplied 16 bit buffer.
 *
 * The buffer belongs to pie_DownLoadBufferToScreen, which still takes RGB565,
 * so this is the one path that converts. Phase 6's B4 replaces that blit with
 * a textured quad and the conversion goes with it.
 */
int seq_RenderOneFrameToBuffer(char* lpSF, int skip, SDWORD, SDWORD)
{
  if (!seq_MovieOpen())
    return VIDEO_FRAME_ERROR;

  const int frame = g_movie->DecodeNextFrame(skip);
  if (frame < 0)
    return VIDEO_FINISHED;

  const UDWORD* source = g_movie->FramePixels();
  if (source == nullptr || lpSF == nullptr)
    return frame;

  const int width = g_movie->Width();
  const int height = g_movie->Height();
  auto* dest = reinterpret_cast<UWORD*>(lpSF);

  for (int i = 0; i < width * height; i++)
  {
    const UDWORD pixel = source[i];
    const UDWORD r = (pixel >> 16) & 0xff;
    const UDWORD g = (pixel >> 8) & 0xff;
    const UDWORD b = pixel & 0xff;
    dest[i] = static_cast<UWORD>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
  }

  return frame;
}

/*
 * Decode one frame and blit it into the back buffer.
 *
 * The frame arrives as X8R8G8B8 and the back buffer is X8R8G8B8, so unlike
 * every version of this function before it there is no pixel conversion at
 * all -- the rows are copied.
 */
int seq_RenderOneFrame(int skip, SDWORD subMin, SDWORD subMax)
{
  SCREEN_LOCK sLock;
  SDWORD borderX, borderY;
  SDWORD subYstart, subYend, subXstart, subXend;

  if (!seq_MovieOpen())
    return VIDEO_FRAME_ERROR;

  const int frame = g_movie->DecodeNextFrame(skip);
  if (frame < 0)
    return VIDEO_FINISHED;

  const UDWORD* source = g_movie->FramePixels();
  if (source == nullptr)
    return VIDEO_FINISHED;

  const SDWORD movieWidth = g_movie->Width();
  const SDWORD movieHeight = g_movie->Height();

  /* The movies are 320x240 against a 640x480 playback area, so a full-screen
   * sequence draws every pixel twice in each direction. The old decoder did
   * this itself -- seq_SetSequence asked Streamer_InitVideo for DFLAG_DOUBLED
   * whenever the movie was no more than half the playback size, and then
   * doubled movieWidth and movieHeight to match. Media Foundation has no
   * equivalent flag, so the doubling happens in the blit instead.
   *
   * Nearest-neighbour, because DFLAG_DOUBLED was pixel replication and this is
   * meant to look like what shipped rather than better than it.
   */
  const SDWORD scale = ((movieWidth <= VIDEO_WIDTH / 2) && (movieHeight <= VIDEO_HEIGHT / 2) && !g_smallVideo) ? 2 : 1;
  const SDWORD drawWidth = movieWidth * scale;
  const SDWORD drawHeight = movieHeight * scale;

  BOOL bTextBoxes = FALSE;
  if ((!g_smallVideo) && (subMin <= subMax))
    bTextBoxes = TRUE;

  if (!screenLockBackBuffer(&sLock))
  {
    Neuron::Fatal("Sequence player back buffer lock failed");
    return VIDEO_SURFACE_ERROR;
  }

  /* Centred horizontally on what is actually drawn. Vertically the divisor is
   * 4 rather than 2, which biases the picture upwards on purpose: it is the
   * original placement, and it leaves the lower part of the screen clear for
   * the subtitle overlay SeqDisp.cpp draws after this returns.
   */
  borderX = (static_cast<SDWORD>(sLock.width) - drawWidth) / 2;
  borderY = (static_cast<SDWORD>(sLock.height) - drawHeight) / 4;

  if (borderX < 0)
    borderX = 0;
  if (borderY < 0)
    borderY = 0;

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
    subYstart = drawHeight;
    subYend = drawHeight;
    subXstart = drawWidth;
    subXend = drawWidth;
  }

#define SEQ_ROW(y) ((UDWORD*)(sLock.pPixels + sLock.pitch * (y)) + borderX)

  /* Walking output rows rather than source rows is what lets the doubling and
   * the subtitle band share one set of coordinates: the band is expressed in
   * the 640x480 playback space, which is the space being written.
   */
  for (SDWORD j = 0; j < drawHeight; j++)
  {
    const SDWORD row = borderY + j;
    if (row < 0 || row >= static_cast<SDWORD>(sLock.height))
      continue;

    const UDWORD* src = source + static_cast<size_t>(j / scale) * movieWidth;
    UDWORD* blitDest = SEQ_ROW(row);

    /* Rows outside the subtitle band copy straight through. Inside it the
     * picture is darkened so white text stays legible over a bright frame,
     * which is what the 565 low-bit-mask shift used to do.
     */
    const BOOL darken = (bTextBoxes && j >= subYstart && j < subYend);

    for (SDWORD i = 0; i < drawWidth; i++)
    {
      if (borderX + i >= static_cast<SDWORD>(sLock.width))
        break;

      UDWORD pixel = src[i / scale];
      if (darken && (i > subXstart) && (i < subXend))
        pixel = (pixel >> 1) & 0x007f7f7f;

      blitDest[i] = pixel;
    }
  }

#undef SEQ_ROW

  screenUnlockBackBuffer();
  return frame;
}

/* The old decoder streamed from disk on a data-rate budget and this kept its
 * cyclic buffer fed between frames. Media Foundation does its own reading, so
 * there is nothing left to top up.
 */
BOOL seq_RefreshVideoBuffers(void) { return seq_MovieOpen(); }

/* Called at the end of every sequence, and now also from systemShutdown before
 * the mixer goes. Everything COM or XAudio2 owns is released here, while the
 * subsystems that created it are still up.
 */
BOOL seq_ShutDown(void)
{
  if (g_movie != nullptr)
    g_movie->Close();
  return TRUE;
}

BOOL seq_GetFrameSize(SDWORD* pWidth, SDWORD* pHeight)
{
  if (seq_MovieOpen())
  {
    *pWidth = g_movie->Width();
    *pHeight = g_movie->Height();
    return TRUE;
  }

  *pWidth = 0;
  *pHeight = 0;
  return FALSE;
}

int seq_GetCurrentFrame(void) { return seq_MovieOpen() ? g_movie->CurrentFrame() : -1; }

int seq_GetFrameTimeInClicks(void) { return seq_MovieOpen() ? g_movie->FrameTimeMs() : 40; }

int seq_GetTotalFrames(void) { return seq_MovieOpen() ? g_movie->TotalFrames() : -1; }
