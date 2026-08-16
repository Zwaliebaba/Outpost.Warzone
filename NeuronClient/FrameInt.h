/*
 * FrameInt.h
 *
 * Internal definitions for the framework library.
 *
 */
#ifndef _frameint_h
#define _frameint_h
#include "Surface.h"
#include "Screen.h"

/* Define the style and extended style of the window.
 * Need these to calculate the size the window should be when returning to
 * window mode.
 *
 * create a title bar, minimise button on the title bar,
 * automatic ShowWindow, get standard system menu on title bar
 */
#define WIN_STYLE (WS_CAPTION | WS_MINIMIZEBOX | WS_VISIBLE | WS_SYSMENU)

#define WIN_EXSTYLE	 WS_EX_APPWINDOW	// Go on task bar when iconified

/* Program hInstance */
extern HINSTANCE hInstance;

/* Handle for the main window */
extern HWND hWndMain;

/* Initialise the double buffered display */

extern BOOL screenInitialise(UDWORD width, // Display width
                             UDWORD height, // Display height
                             UDWORD bitDepth, // Display bit depth - recorded
                             // only, the display is always 32 bit
                             BOOL fullScreen, // Whether to start windowed
                             // or full screen.
                             BOOL bVidMem, // No longer used - the managed
                             // pool decides where resources live
                             BOOL bCreateDevice, // Whether to create a device at all
                             HANDLE hWindow); // The main windows handle

/* Release the Direct3D objects */
extern void screenShutDown(void);

/* Deal with windows messages to maintain the state of the keyboard and mouse */
extern void inputProcessMessages(UINT message, WPARAM wParam, LPARAM lParam);

/* This is called once a frame so that the system can tell
 * whether a key was pressed this turn or held down from the last frame.
 */
extern void inputNewFrame(void);

/* The list of surfaces structure */
using SURFACE_LIST = struct _surface_list
{
  LPSURFACE psSurface;
  struct _surface_list* psNext;
};

/* The list of surfaces */
extern SURFACE_LIST* psSurfaces;

/* Release all the allocated surfaces */
extern void surfShutDown(void);

/* Free current currently open widget file */
BOOL FreeCurrentWDG(void);

/* The Direct3D objects */
extern LPDIRECT3D9 psD3D;
extern LPDIRECT3DDEVICE9 psD3DDevice;

/* The Current screen size and bit depth */
extern UDWORD screenWidth;
extern UDWORD screenHeight;
extern UDWORD screenDepth;

/* Which modes the library can run in.
 *
 * MODE_8BITFUDGE - an 8 bit back buffer expanded into a true colour window -
 * went with DirectDraw; the display is 32 bit either way round now, so the
 * library is always MODE_BOTH.
 */
using DISPLAY_MODES = enum _display_modes
{
  MODE_BOTH,
  // Can run both windowed and full screen
  MODE_WINDOWED,
  // Can only run windowed, not full screen
  MODE_FULLSCREEN,
  // Can only run full screen not windowed
};

/* The current screen mode (full screen/windowed) */
extern SCREEN_MODE screenMode;

/* Which mode (of operation) the library is running in */
extern DISPLAY_MODES displayMode;

/* The Pixel format of the back buffer */
extern SCREEN_PIXELFORMAT sBackBufferPixelFormat;

// The possible flip states
using FLIP_STATE = enum _flip_state
{
  FLIP_IDLE,
  FLIP_STARTED,
  FLIP_FINISHED,
};
extern FLIP_STATE screenFlipState;

// The critical section for the screen flipping
extern CRITICAL_SECTION sScreenFlipCritical;

// The semaphore for the screen flipping
extern HANDLE hScreenFlipSemaphore;

#endif
