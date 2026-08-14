/***************************************************************************/
/*
 * sequence.h
 *
 * video streaming to game surfaces.
 *
 */
/***************************************************************************/

#ifndef _sequence_h
#define _sequence_h

/***************************************************************************/

#include <dsound.h>

/***************************************************************************/
/*
 *	Global Definitions
 */
/***************************************************************************/

using VIDEO_MODE = enum _video_mode
{
  VIDEO_SOFT_WINDOW,
  // video size 16bit rgb 555 mode (convert to 8bit from the buffer)
  VIDEO_SOFT_FULLSCREEN,
  // directX 640 * 480 16bit rgb mode render through local buffer to back buffer
  VIDEO_D3D_WINDOW,
  // video 16bit screen pixel mode
  VIDEO_D3D_FULLSCREEN,
  // 640 * 480 screen pixel mode
  VIDEO_3DFX_WINDOW,
  // video 16bit BGR 565 mode
  VIDEO_3DFX_FULLSCREEN // 640 * 480 BGR 565 mode
};

using PERF_MODE = enum _perf_mode
{
  VIDEO_PERF_FULLSCREEN,
  // Normal alternate line video
  VIDEO_PERF_WINDOW,
  // 320 240 centred
  VIDEO_PERF_SKIP_FRAMES // 320 240 centred and display every 4th frame
};

#define MAX_BAD_DATA 20

#define FRAMES_TO_SKIP 4

#define VIDEO_FINISHED -1
#define VIDEO_FRAME_WAIT -2
#define VIDEO_FRAME_ERROR -3
#define VIDEO_SOUND_ERROR -4
#define VIDEO_SURFACE_ERROR -5
/***************************************************************************/
/*
 *	Global Variables
 */
/***************************************************************************/

/***************************************************************************/
/*
 *	Global ProtoTypes
 */
/***************************************************************************/
/* Frames are decoded as RGB565 and converted on the way to the 32 bit
 * display, so neither of these takes a pixel format any more: the decoder is
 * told 565 and the conversion is fixed.
 */

/* Neither takes an IDirectSound any more. FMV audio used to be played through
 * the object QMixer had created for the game's mixer; the mixer is XAudio2
 * now, so Sequence.cpp creates and owns its own.
 */

//decode into a caller supplied 16 bit buffer
extern BOOL seq_SetSequenceForBuffer(char* filename, VIDEO_MODE mode, int startTime, PERF_MODE perfMode);
extern int seq_RenderOneFrameToBuffer(char* lpSF, int skip, SDWORD boxMin, SDWORD boxMax);

//decode into a local buffer, then blit it into the back buffer
extern BOOL seq_SetSequence(char* filename, int startTime, char* lpBF, PERF_MODE perfMode);
extern int seq_RenderOneFrame(int skip, SDWORD boxMin, SDWORD boxMax);

extern int seq_ClearMovie(void);

//setup monitoring and control
extern BOOL seq_RefreshVideoBuffers(void);
extern BOOL seq_ShutDown(void);
extern BOOL seq_GetFrameSize(SDWORD* pWidth, SDWORD* pHeight);
extern int seq_GetCurrentFrame(void);
extern int seq_GetFrameTimeInClicks(void);
extern int seq_GetTotalFrames(void);

#endif // _sequence_h
